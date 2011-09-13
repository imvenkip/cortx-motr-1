#!/bin/bash

set -x

./yaml2db -f disks.yaml -D -t parse.txt
./yaml2db -e -D -t emit.txt

diff parse.txt emit.txt
if [ $? == 0 ]
then
	echo "test passed."
else
	echo "test failed."
fi
rm -rf parse.txt emit.txt
