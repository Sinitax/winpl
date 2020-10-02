#!/bin/bash

SCRIPTPATH="$(dirname $(readlink -f "$0"))"
LD_PRELOAD="$SCRIPTPATH/winpreload.so" xclock

