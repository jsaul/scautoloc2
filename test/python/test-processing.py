#!/usr/bin/env python
# -*- coding: utf-8 -*-

import sys
import os
import seiscomp.client
import seiscomp.autoloc
import scstuff.util


class App(seiscomp.client.Application):
    def __init__(self, argc, argv):
        seiscomp.client.Application.__init__(self, argc, argv)
        self.setMessagingEnabled(False)
        self.setDatabaseEnabled(False, False)
        self.setLoggingToStdErr(True)

    def createCommandLineDescription(self):
        self.commandline().addGroup("Config")
        self.commandline().addStringOption(
            "Config", "event,E", "ID of event to process")

    def run(self):
        try:
            eventID = self.commandline().optionString("event")
        except RuntimeError:
            raise ValueError("Must specify event ID")

        autoloc = seiscomp.autoloc.Autoloc3()
        stationConfigFilename = os.path.join(
            eventID, "config", "station.conf")
        autoloc.setStationConfigFilename(stationConfigFilename)

        stationLocationFilename = os.path.join(
            eventID, "config", "station-locations.txt")
        inventory = seiscomp.autoloc.inventoryFromStationLocationFile(
            stationLocationFilename)
        autoloc.setInventory(inventory)

        # The SeisComP config
        scconfig = self.configuration()
        autoloc.setConfig(scconfig)

        # The Autoloc config
        config = seiscomp.autoloc.Config()
        config.gridConfigFile = os.path.join(eventID, "config", "grid.conf")
        config.useManualOrigins = True
        config.useImportedOrigins = True
        config.pickLogEnable = True
        config.pickLogFile = "pick.log"
        autoloc.setConfig(config)

        if not autoloc.init():
            raise RuntimeError("autoloc.init() failed")

        autoloc.setProcessingEnabled(True)

        xmlFilename = os.path.join(eventID, "objects.xml")
        ep = scstuff.util.readEventParametersFromXML(xmlFilename)

        objectQueue = seiscomp.autoloc.PublicObjectQueue()
        objectQueue.fill(ep)

        for obj in objectQueue:
            if self.isExitRequested():
                break
            status = autoloc.feed(obj)

        return True


def test_app():
    # argv = sys.argv
    argv = [sys.argv[0], "--debug", "--event", "gfz2022oogo"]
    app = App(len(argv), argv)
    return app()


if __name__ == "__main__":
    test_app()
