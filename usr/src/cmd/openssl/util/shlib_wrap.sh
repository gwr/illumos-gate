#!/bin/sh

LD_LIBRARY_PATH=${ROOT}/lib:${ROOT}/usr/lib
export LD_LIBRARY_PATH

exec "$@"
exit 2
