#!/bin/bash

i=0
echo Device: > ./devices.conf
sudo fdisk -l 2>&1 |  grep "doesn't contain a valid partition table"  | awk '{print $2}' | grep "/dev/*" | while read line
do
echo "       - id: $i" >> ./devices.conf
echo "         device_path: $line" >> ./devices.conf
i=`expr $i + 1`
done
