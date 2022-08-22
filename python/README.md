This folder contains miscellaneous utility programs written in
Python.

# find-duplicate-stations.py

This script is related to a specific issue in scautoloc, which at
the time of writing requires a fix. Namely, occasionally two P picks
of the same station are associated to an origin. One usually with a
large residual and thus weight=0, but nevertheless, there should
only be one pick for any given phase per station.  The script
searches the SeisComP database for origins that suffer from said
issue.
