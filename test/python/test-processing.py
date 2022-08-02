#!/usr/bin/env python
# -*- coding: utf-8 -*-

import sys
import os
import traceback
import seiscomp.client
import seiscomp.autoloc
import scstuff.util


class MyAutoloc(seiscomp.autoloc.Autoloc3):

    def _report(self, origin):
        try:
            txt = repr(origin)
        except Exception:
            print("-"*60, file=sys.stderr)
            traceback.print_exc(file=sys.stderr)
            print("-"*60, file=sys.stderr)
        print("XXXX", txt, file=sys.stderr)
        return True


class App(seiscomp.client.Application):
    def __init__(self, argc, argv):
        seiscomp.client.Application.__init__(self, argc, argv)
        self.setMessagingEnabled(False)
        self.setDatabaseEnabled(False, False)
        self.setLoggingToStdErr(True)

        self.trackPicks = []

    def createCommandLineDescription(self):
        self.commandline().addGroup("Config")
        self.commandline().addStringOption(
            "Config", "event,E", "ID of event to process")

    def run(self):
        try:
            eventID = self.commandline().optionString("event")
        except RuntimeError:
            raise ValueError("Must specify event ID")

        autoloc = MyAutoloc()
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

        # TODO: config.useImportedOrigins = True
        config.useImportedOrigins = True

        config.pickLogEnable = True
        config.pickLogFile = "pick.log"
        autoloc.setConfig(config)

        for pickID in self.trackPicks:
            autoloc.trackPick(pickID)

        if not autoloc.init():
            raise RuntimeError("autoloc.init() failed")

        autoloc.setProcessingEnabled(True)

        # for testing:

        xmlFilename = os.path.join(eventID, "objects.xml")
        ep = scstuff.util.readEventParametersFromXML(xmlFilename)

        objectQueue = seiscomp.autoloc.PublicObjectQueue()
        objectQueue.fill(ep)

        for obj in objectQueue:
            if self.isExitRequested():
                break
            author = obj.creationInfo().author()
            if author.startswith("dlpicker"):
                wasEnabled = autoloc.isProcessingEnabled()
                autoloc.setProcessingEnabled(False)
                status = autoloc.feed(obj)
                autoloc.setProcessingEnabled(wasEnabled)
                continue

            status = autoloc.feed(obj)

        return True


def test_app():
    # argv = sys.argv
    argv = [sys.argv[0], "--debug", "--event", "gfz2022oogo"]
    app = App(len(argv), argv)
    app.trackPicks = [
        "20220727.004515.26-AIC-IU.TATO.00.BHZ",
        "20220727.004715.41-AIC-GE.TOLI2..BHZ" ]
    return app()


if __name__ == "__main__":
    test_app()
