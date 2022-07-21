#!/bin/sh -e

i=autoloc.i

seiscomp_source_d=../../../../..

# Make sure we are in the right directory
test -d $seiscomp_source_d/src
test -d $seiscomp_source_d/build

cxx=$(basename $i .i)_python_wrap.cxx
swig -python -c++ \
    -I$seiscomp_source_d/src/base/common/libs \
    -I$seiscomp_source_d/src/base/main/libs \
    -I$seiscomp_source_d/src/extras/scautoloc2/libs \
    -I$seiscomp_source_d/src/base/common/libs/swig \
    -I$seiscomp_source_d/build/src/base/common/libs \
    -I$seiscomp_source_d/build/src/base/main/libs \
    -I$seiscomp_source_d/build/src/extras/scautoloc2/libs \
    -o $cxx $i 2>&1
