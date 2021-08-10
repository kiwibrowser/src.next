#!/bin/sh
gn gen out/public_arm64 --args='cc_wrapper="ccache"'
