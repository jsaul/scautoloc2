# SeisComP scautoloc development version

This repository contains the development version of scautoloc.

The goal of the development version is to be able to run two
versions of scautoloc in parallel. This is the reason why this
separate repository was created after starting with the
[scautoloc-improvements](https://github.com/SeisComP/main/tree/scautoloc-improvements)
branch. After installation, there will be `scautoloc2` in addition
to `scautoloc`. Please note that if you don't want the origins
created by `scautoloc2` to become preferred in your production
workflow, you need to configure `scevent` accordingly. Otherwise you
will end up with a parallel history of origins, each of which can
become preferred.

The goal of this development version is a general rework of
`scautoloc`.

* Move most functionality to a library
* Expose the library functions as Python wrappers
* Code cleanups
* Fixes of known bugs
* Processing of automatic picks from more than one picker.

Some of the new development, especially bug fixes, are planned to be
ported back to the main branch, which will not be abandoned.
However, the ultimate goal is to replace `scautoloc` by
`scautoloc2` at some point.


# Build

The repository cannot be built standalone. It needs to be integrated
into the `seiscomp` build environment and checked out into
`src/base/main`.

```
$ git clone [host]/seiscomp.git
$ cd seiscomp/src/base
$ git clone [host]/main.git
```


## Configuration

For the time being it should be enough to just copy `scautoloc.cfg`
to `scautoloc2.cfg`.


## Compilation

Follow the build instructions from the `seiscomp` repository.
