#!/usr/bin/env python
# -*- coding: utf-8 -*-

def test_import():
    """
    Basic test if the import succeeds.
    """
    import seiscomp.autoloc

    assert "fill" in dir(seiscomp.autoloc.PublicObjectQueue)
