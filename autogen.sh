#!/bin/bash
autoreconf --install --force
if ! [ -f ../galois/configure ]; then
    pushd ../galois > /dev/null
    sh autogen.sh
    popd > /dev/null
fi
