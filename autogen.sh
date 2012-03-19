#!/bin/bash
autoreconf --install --force
[ -f ../galois/configure ] || (cd ../galois && sh autogen.sh)
