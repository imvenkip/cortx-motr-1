#!/bin/sh

# Simple shell script that can be run while kernel UT is running.  When the
# UT reached the LNet transport UT (detected by the appearance of the
# /proc/c2_lnet_ut file), the script interacts with the UT, theoretically,
# via the /proc/c2_lnet_ut file.  At present, this interaction is a proof
# of concept.  The c2 lnet UT is known to take several seconds, at least,
# to complete.

while [ ! -f /proc/c2_lnet_ut ]; do
    sleep 1
done

cat /proc/c2_lnet_ut
# user test program is ready
echo 1 > /proc/c2_lnet_ut
cat /proc/c2_lnet_ut
# user test program is done
echo 2 > /proc/c2_lnet_ut
cat /proc/c2_lnet_ut
