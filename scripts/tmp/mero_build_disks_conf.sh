#!/bin/bash
set -eux

CWD=$(cd "$( dirname "$0")" && pwd)
SCRIPT_DISKS_FIND="$CWD/disks_find.sh"
SCRIPT_DISKS_LIST2YAML="$CWD/disks_list2yaml.sh"

cmdline_help()
{
	cat << EOF
Usage: $0 [-?] [-h] [-e]
-?
-h	this help
-e	print input example

This script will create disks.conf for m0d '-d' option on each host.
File format:
<pool width>
<HDDs_per_controller>
<titan block0>
<titan_block1>
...
<titan_blockNNN>
0

where <titan_block> is equal to
<titan_nodes_nr>
<titan_name>
<titan_node_name_1>
...
<titan_node_name{titan_nodes_nr}>


Use "$0 -e" and "$0 -e | $0" to see the example"
EOF
}

example_print()
{
	cat << EOF
12
6
2
sjT04
sjT04-s1
sjT04-s2
0
EOF
}

run_remote_sudo()
{
	hostname="$1"
	cmd_full="$2"
	cmd="$(basename $cmd_full)"
	copy_of_cmd="copy-of-$cmd"
	shift 2

	scp "$cmd_full" "$hostname:$copy_of_cmd"
	ssh -n "$hostname" sudo "./$copy_of_cmd"
	ssh -n "$hostname" rm -f "$copy_of_cmd"
}

get_node_HDDs()
{
	node="$1"

	run_remote_sudo "$node" "$SCRIPT_DISKS_FIND"
}

disks_file()
{
	node="$1"

	echo "disks-$node.txt"
}

disks_list_file()
{
	node="$1"

	echo "disks-list-$node.txt"
}

disks_conf()
{
	node="$1"

	echo "disks-$node.conf"
}

main()
{
	while getopts "?he" opt; do
		case "$opt" in
		h|\?)	cmdline_help
			exit 0;;
		e)	example_print
			exit 0;;
		esac
	done

	read pool_width
	read HDDs_per_controller
	s=1
	titan_names=()
	while true; do
		read titan_nodes_nr
		if [ $titan_nodes_nr -eq 0 ]; then
			break
		fi
		read titan_name
		if [ "${#titan_names[@]}" -gt 0 ]; then
			for name in "${titan_names[@]}"; do
				if [ "$name" == "$titan_name" ]; then
					echo "$titan_name already processed."
					exit 1
				fi
			done
		fi
		titan_names+=("$titan_name")
		titan_nodes=()
		for i in $(seq 1 $titan_nodes_nr); do
			read titan_node_name
			titan_nodes+=("$titan_node_name")
		done
		for node in "${titan_nodes[@]}"; do
			get_node_HDDs "$node" | sort > "$(disks_file $node)"
		done
		for node1 in "${titan_nodes[@]}"; do
			for node2 in "${titan_nodes[@]}"; do
				rc=0
				diff -u "$(disks_file $node1)" \
					"$(disks_file $node2)" || rc=$?
				if [ "$rc" -ne 0 ]; then
					cat << EOF
Disks on $titan_name, nodes $node1 and $node2 are different.
See $(disks_file $node1) and $(disks_file $node1) files.
EOF
					exit 1
				fi
			done
		done
		cp "$(disks_file $node1)" "$(disks_file $titan_name)"
		for node in "${titan_nodes[@]}"; do
			rm "$(disks_file $node)"
		done
		# Filter disks: select rotational with size > 0
		awk '{	if ($2 == 1 && $4 != 0)
				print $1
		}' $(disks_file $titan_name) > $(disks_list_file $titan_name)
		disks_nr="$(cat $(disks_list_file $titan_name) | wc -l)"
		disks_nr_min="$(expr $titan_nodes_nr \* $HDDs_per_controller)"
		if [ $disks_nr -lt $disks_nr_min ]; then
			cat << EOF
$titan_name needs $disks_nr_min = $titan_nodes_nr * $HDDs_per_controller disks,
but only $disks_nr found (see file $(disks_list_file $titan_name)).
EOF
			exit 1
		fi

		i=0
		for node in "${titan_nodes[@]}"; do
			let i+="$HDDs_per_controller"
			head -n "$i" "$(disks_list_file $titan_name)" |	    \
			    tail -n "$HDDs_per_controller" |		    \
			    "$SCRIPT_DISKS_LIST2YAML" -s "$s" -0 /dev/null> \
					"$(disks_conf $node)"
			let s+="$HDDs_per_controller"
		done
	done
	s="$(expr $s - 1)"
	if [ "$s" -ne "$pool_width" ]; then
		cat << EOF
Number of HDDs used ($s) should be equal to the pool width ($pool_width).
EOF
		exit 1
	fi
	for name1 in "${titan_names[@]}"; do
	    for name2 in "${titan_names[@]}"; do
		if [ "$name1" != "$name2" ]; then
		    while read line1; do
			while read line2; do
			    if [ "$line1" == "$line2" ]; then
				    cat << EOF
Titans $name1 and $name2 have the same disk "$line1" which
shouldn't be possible. See files $(disks_file $name1) and $(disks_file $name2) \
for reference.
EOF
			    fi
			done < "$(disks_file $name2)"
		    done < "$(disks_file $name1)"
		fi
	    done
	done
}

main "$@"
