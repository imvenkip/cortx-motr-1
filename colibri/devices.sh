#!/bin/bash

#This script helps to create a device a configuration file viz. devices.conf,
#in yaml format, as desired by the colibri_setup program.
#The script uses the fdisk command and extracts only the unused devices
#present on the system (i.e without valid partition table).
#Following is the illustration of one of the entries in devices.conf file
#created by the script.
#Device:
#       - id: 1
#	  filename: /dev/sda

i=0
echo "Device:" > ./devices.conf
sudo fdisk -l 2>&1 |  grep "doesn't contain a valid partition table"  | awk '{ print $2 }'| grep "/dev" | grep [a-z]$ | while read line
do
echo "       - id: $i" >> ./devices.conf
echo "         filename: $line" >> ./devices.conf
i=`expr $i + 1`
done
