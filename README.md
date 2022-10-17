# SeisComP scautoloc development version

This repository contains the development version of scautoloc.

The goal of this development version is a comprehensive rework of
`scautoloc`:

* Move most functionality to a library
* Expose the library functions as Python wrappers
* Fixes of known bugs
* Processing of automatic picks from more than one picker.
* Code cleanups

Some of the new developments, especially bug fixes, are planned to
be ported back to the main branch, which will not be abandoned for
quite a while. However, the ultimate goal is to replace `scautoloc`
by `scautoloc2` at some point.

In order to be able to run the two versions of scautoloc in parallel,
this separate repository was created after starting with the
[scautoloc-improvements](https://github.com/SeisComP/main/tree/scautoloc-improvements)
branch. After installation, there will be `scautoloc2` in addition
to `scautoloc`. Please note that if you don't want the origins
created by `scautoloc2` to become preferred in your production
workflow, you need to configure `scevent` accordingly. Otherwise you
will end up with a parallel history of origins, each of which can
become preferred.


# Build

Note that `scautoloc2` is not part of any distribution. In order to
use it, you need to build SeisComP from source.

The `scautoloc2` repository cannot be built standalone. It needs to
be integrated into the `seiscomp` build environment and cloned
into `src/extras`.

```
$ cd seiscomp/src/extras
$ git clone https://github.com/jsaul/scautoloc2.git
```

After cloning the SWIG Python wrappers must be built. This obviously requires SWIG and it is recommended to use the latest version.

```
$ cd scautoloc2/libs/swig
$ ./swig-compile.sh
$ cd ../../..
```

Finally the `scautoloc2` package is built along with the rest of
SeisComP:

```
$ cd ../../build
$ pwd
# You should now be in seiscomp/build
$ cmake ..
$ make install
```

This should be all.

## Configuration

For the time being it should be enough to just copy `scautoloc.cfg`
to `scautoloc2.cfg`.


## Compilation

Follow the build instructions from the `seiscomp` repository.
