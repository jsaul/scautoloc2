#!/usr/bin/env seiscomp-python
# -*- coding: utf-8 -*-
############################################################################
# Copyright (C) GFZ Potsdam                                                #
# All rights reserved.                                                     #
#                                                                          #
# GNU Affero General Public License Usage                                  #
# This file may be used under the terms of the GNU Affero                  #
# Public License version 3.0 as published by the Free Software Foundation  #
# and appearing in the file LICENSE included in the packaging of this      #
# file. Please review the following information to ensure the GNU Affero   #
# Public License version 3.0 requirements will be met:                     #
# https://www.gnu.org/licenses/agpl-3.0.html.                              #
############################################################################

import sys
import fnmatch
import seiscomp.core
import seiscomp.client
import seiscomp.datamodel
import scstuff.util
import scstuff.dbutil


# This means picks that are very close in time (up to a few samples)
findDuplicatePicks = True


def usage(exitcode=0):
    usagetext = """
    ERROR
    """
    sys.stderr.write("%s\n" % usagetext)
    sys.exit(exitcode)


def loadEventsForTimespan(query, startTime, endTime):
    """
    Load from the database all events within the given time span.
    """

    events = {}
    for obj in query.getEvents(startTime, endTime):
        event = seiscomp.datamodel.Event.Cast(obj)
        if event:
            events[event.publicID()] = event

    seiscomp.logging.debug("loaded %d events" % len(events))
    return events


class App(seiscomp.client.Application):

    def __init__(self, argc, argv):
        seiscomp.client.Application.__init__(self, argc, argv)
        self.setDatabaseEnabled(True, True)
        self.setDaemonEnabled(False)
        self.setLoggingToStdErr(True)
        self.setLoadRegionsEnabled(True)

    def createCommandLineDescription(self):
        self.commandline().addGroup("Config")
        self.commandline().addStringOption(
            "Config", "author,a", "Only consider origins created by author")
        self.commandline().addStringOption(
            "Config", "start", "Start time")
        self.commandline().addStringOption(
            "Config", "end", "End time")
        return True

    def validateParameters(self):
        if seiscomp.client.Application.validateParameters(self) is False:
            return False

        self.setDatabaseEnabled(True, True)
        if self.commandline().hasOption("database"):
            self.setMessagingEnabled(False)
        else:
            self.setMessagingEnabled(True)
        return True

    def run(self):
        try:
            self._startTime = self.commandline().optionString("start")
        except RuntimeError:
            self._startTime = None

        try:
            self._endTime = self.commandline().optionString("end")
        except RuntimeError:
            self._endTime = None

        try:
            self._author = self.commandline().optionString("author")
        except RuntimeError:
            self._author = None

        now = seiscomp.core.Time.GMT()

        if self._startTime:
            self._startTime = scstuff.util.parseTime(self._startTime)
        else:
            self._startTime = now - seiscomp.core.TimeSpan(86400)

        if self._endTime:
            self._endTime = scstuff.util.parseTime(self._endTime)
        else:
            self._endTime = now - seiscomp.core.TimeSpan(86400)


        picks = {}

        events = loadEventsForTimespan(self.query(), self._startTime, self._endTime)
        for eventID in events:
            if self.isExitRequested():
                break

#           print(eventID)

            lines = []


            origins = []
            for obj in self.query().getOrigins(eventID):
                origin = seiscomp.datamodel.Origin.Cast(obj)
                if origin:
                    origins.append(origin)

            for origin in origins:
                author = origin.creationInfo().author()
                if self._author:
                    if not fnmatch.fnmatch(author, self._author):
                        continue

                if self.isExitRequested():
                    break

                self.query().load(origin)

                originID = origin.publicID()
                for obj in self.query().getPicks(originID):
                    pick = seiscomp.datamodel.Pick.Cast(obj)
                    if not pick:
                        continue
                    picks[pick.publicID()] = pick

                items = {}
                duplicate_items = []

                for arr in scstuff.util.ArrivalIterator(origin):
                    pickID = arr.pickID()
                    try:
                        pick = picks[pickID]
                    except KeyError:
                        continue  # ignore
                    nslc = scstuff.util.nslc(pick.waveformID())
                    phcd = pick.phaseHint().code()
                    key = (nslc, phcd)
                    item = (eventID, originID, author, nslc, phcd, pick)
                    if key in items:
                        save = False
                        if findDuplicatePicks:
                            dt = float(pick.time().value()-items[key][-1].time().value())
                            if abs(dt) < 0.3:
                                save = True
                        else:
                            save = True

                        if save:
                            duplicate_items.append(items[key])
                            duplicate_items.append(item)
#                       print(eventID, originID, author, item)
                    else:
                        items[key] = item

                if duplicate_items:
                    for eventID, originID, author, nslc, phcd, pick in duplicate_items:
                        if author.startswith("scautoloc"):
                            line = " ".join([eventID, phcd, pick.publicID()])
                            if line not in lines:
                                lines.append(line)
                                print(line)

        return True


def main():
    app = App(len(sys.argv), sys.argv)
    return app()


if __name__ == "__main__":
    main()
