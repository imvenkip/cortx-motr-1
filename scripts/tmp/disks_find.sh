#!/bin/bash

DISK_SEARCH_CMD="ls /dev/disk/by-id/scsi-35* | grep -v part"

cmdline_help()
{
	cat << EOF
Usage: $0 [-?] [-h] [-c cmd]
-?
-h	this help
-c cmd	disk search command
	Default command is \`$DISK_SEARCH_CMD'

This script will find and analyze all disks matching pattern.
Output format:
	<device> <is_rotational> <have_valid_MBR> <size>
Example:

/dev/disk/by-id/scsi-350000123456789ab 1 0 4000787030016
/dev/disk/by-id/scsi-35000023456789abc 1 1 4000787030016
/dev/disk/by-id/scsi-3500003456789abcd 0 1 100030242816
/dev/disk/by-id/scsi-350000456789abcde 0 1 100030242816

SECURITY NOTE: 'cmd' will be executed without any checking.
EOF
}

main()
{
	while getopts "?hc:" opt; do
		case "$opt" in
		h|\?)	cmdline_help
			exit 0;;
		c)	DISK_SEARCH_CMD="$OPTARG"
		esac
	done
	for disk in `eval $DISK_SEARCH_CMD`; do
		dev="$(readlink -f $disk)"
		dev_basename="$(basename $dev)"
		rotational="$(cat /sys/block/$dev_basename/queue/rotational)"
		if [ "$(partprobe -ds $dev)" == "" ]; then
			have_valid_MBR=0
		else
			have_valid_MBR=1
		fi
		size="$(blockdev --getsize64 $dev)"
		echo "$disk $rotational $have_valid_MBR $size"
	done
}

main "$@"
