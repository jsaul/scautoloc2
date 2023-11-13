import time
import seiscomp.autoloc


stationConfigTextFirst = """# comment
*  *       1  60
GE *       1  90
GX *       0  90
"""

stationConfigTextSecond = """# comment
*  *       1  60
GE *       1  90
GX *       1  90
"""


def test_stationconfig(verbose=False):
    """
    Test basic StationConfig functionality

    - black/white listing
    - detect change
    - reload
    """
    stationConfigFileName = "/tmp/station.conf"
    with open(stationConfigFileName, "w") as f:
        f.write(stationConfigTextFirst)
    stationConfig = seiscomp.autoloc.StationConfig()
    stationConfig.setFilename(stationConfigFileName)
    assert stationConfig.filename() == stationConfigFileName
    stationConfig.read()

    item = stationConfig.get("GE", "RUE")
    assert item.usage
    assert item.maxNucDist == 90

    item = stationConfig.get("GX", "WANN")
    assert not item.usage

    item = stationConfig.get("IU", "ANMO")
    assert item.usage
    assert item.maxNucDist == 60

    assert not stationConfig.hasChanged()

    # Write out the same file again with changed mtime
    time.sleep(1)  # enforce timestamp change
    with open(stationConfigFileName, "w") as f:
        f.write(stationConfigTextSecond)
    assert stationConfig.hasChanged()
    stationConfig.read()

    item = stationConfig.get("GX", "WANN")
    assert item.usage


if __name__ == "__main__":
    verbose = True
    test_stationconfig(verbose)
