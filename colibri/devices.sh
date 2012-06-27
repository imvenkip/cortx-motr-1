#!/bin/bash

i=0
sudo fdisk -l 2>&1 |  grep "doesn't contain a valid partition table"  | awk '{print $2}' | grep "/dev/*" | while read line
do
echo $i $line >> ./devices.conf
i=`expr $i + 1`
done
