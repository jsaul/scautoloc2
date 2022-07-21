#!/usr/bin/env python
# -*- coding: utf-8 -*-

def test_import():
    import seiscomp.autoloc

    assert "fill" in dir(seiscomp.autoloc.PublicObjectQueue)
