#!/bin/bash

# Script to start Clovis CmdLine tool (m0clovis).

usage()
{
	cat <<.
Usage:

$ sudo m0clovis_start.sh [local|remote] ["(m0clovis index commands)"]
.
}

conf=$1
shift 1
all=$*

function m0clovis_cmd_start()
{
	# Assembly command
	local exec="`dirname $0`/../../m0clovis/m0clovis"
	if [ ! -f $exec ];then
		echo "Can't find m0clovis"
		return 1
	fi

	local args="-l $LOCAL_EP -h $HA_EP -c $CONFD_EP \
		    -p '$PROF_OPT' -f '$PROC_FID'"
	local cmdline="$exec $args $all"
	# Run it
	#echo Running m0clovis command line tool...
	#echo "# $cmdline" >/dev/tty
	eval $cmdline || {
		err=$?
		echo "Clovis CmdLine utility returned $err!"
		return $err
	}

	return $?
}

case "$conf" in
	local)
		. `dirname $0`/clovis_local_conf.sh
		;;
	remote)
		. `dirname $0`/clovis_remote_conf.sh
		;;
	*)
		usage
		exit
esac

m0clovis_cmd_start
exit $?

# Local variables:
# sh-basic-offset: 8
# sh-indentation: 8
# tab-width: 8
# End:
