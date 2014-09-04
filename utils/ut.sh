#!/bin/bash

### This is transitional script for Jenkins, which is required for inspections
### based on "master before m0run introduction". It can be removed when all such
### inspections are rebased on "master with m0run" and Jenkins config is updated
### to use m0run instead of ut.sh.

self=$(readlink -f $0)
top_srcdir=$(echo $(dirname $self) | sed -r -e 's#/?utils/?$##')

$top_srcdir/utils/m0run m0ut -- "$@"
