#!/bin/bash

cmdline_help()
{
	cat << EOF
Usage: $0 [-?] [-h] [-e] [-s num] [-0 /path/to/stob0]
-?
-h		   this help
-e		   print input example
-s num		   start disk numbers from num, 0 by default
-0 /path/to/stob0  use given path as path for stob0

This script will read list of disks from stdin and print
yaml-formatted list of disks to stdout.

Example:
$ scripts/disks_list2yaml.sh -e
/dev/loop0
/dev/loop1
/dev/loop2
$ scripts/disks_list2yaml.sh -e | scripts/disks_list2yaml.sh
Device:
   - id: 0
     filename: /dev/loop0
   - id: 1
     filename: /dev/loop1
   - id: 2
     filename: /dev/loop2
$ scripts/disks_list2yaml.sh -e | scripts/disks_list2yaml.sh -s 5
Device:
   - id: 5
     filename: /dev/loop0
   - id: 6
     filename: /dev/loop1
   - id: 7
     filename: /dev/loop2
$ scripts/disks_list2yaml.sh -e | scripts/disks_list2yaml.sh -s 5 -0 /dev/null
Device:
   - id: 0
     filename: /dev/null
   - id: 5
     filename: /dev/loop0
   - id: 6
     filename: /dev/loop1
   - id: 7
     filename: /dev/loop2
EOF
}

example_print()
{
	cat << EOF
/dev/loop0
/dev/loop1
/dev/loop2
EOF
}

print_id_filename()
{
	id="$1"
	filename="$2"
	cat << EOF
   - id: $id
     filename: $filename
EOF
}

main()
{
	i=0
	stob0=
	while getopts "?hs:0:e" opt; do
		case "$opt" in
		h|\?)	cmdline_help
			exit 0;;
		s)	i="$OPTARG";;
		0)	stob0="$OPTARG";;
		e)	example_print
			exit 0
		esac
	done

	echo "Device:"
	if [ -n "$stob0" ]; then
		print_id_filename 0 "$stob0"
		if [ $i -eq 0 ]; then
			((++i))
		fi
	fi
	while read disk; do
		print_id_filename "$((i++))" "$disk"
	done
}

main "$@"
