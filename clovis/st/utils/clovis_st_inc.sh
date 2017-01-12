#!/bin/bash

# Script for starting or stopping Clovis system tests

random_mode=
tests=

# kernel mode
function clovis_st_start_k ()
{
	local idx_service=-1
	case $1 in
		"KVS")
			idx_service=1
			;;
		"CASS")
			idx_service=2
			;;
		"MOCK")
			idx_service=3
			;;

	esac

	local st_kmod=$st_util_dir/../linux_kernel/clovis_st_kmod.ko
	local st_kmod_args="clovis_local_addr=$LOCAL_EP \
			    clovis_ha_addr=$HA_EP \
			    clovis_confd_addr=$CONFD_EP \
			    clovis_prof=$PROF_OPT \
			    clovis_proc_fid=$PROC_FID"

	if [ X$tests != X ]; then
		st_kmod_args="$st_kmod_args clovis_tests=$tests"
	fi

	if [ ! -f $st_kmod ];then
		echo "Can't find Clovis ST kernel module"
		return 1
	fi

	# insmod kernel
	echo -e "insmod $st_kmod $st_kmod_args "
	insmod $st_kmod $st_kmod_args
	if [ $? -ne 0  ] ; then
		echo "Failed to load Clovis system test kernel module"
		return 1
	fi

	return 0
}

function clovis_st_stop_k()
{
	# when remove the kernel module, it will wait till
	# all tests finish
	echo "Stop Clovis system tests ..."
	rmmod clovis_st_kmod
}

# user space mode
function clovis_st_start_u()
{
	# Assembly command
	local st_exec="$st_util_dir/../user_space/c0st"
	if [ ! -f $st_exec ];then
		echo "Can't find c0st"
		return 1
	fi

	local idx_service=-1
	case $1 in
		"KVS")
			idx_service=1
			;;
		"CASS")
			idx_service=2
			;;
		"MOCK")
			idx_service=3
			;;

	esac

	local st_args="-m $LOCAL_EP -h $HA_EP -c $CONFD_EP -p '$PROF_OPT' -f '$PROC_FID' -I $idx_service"
	if [ $random_mode -eq 1 ]; then
		st_args="$st_args -r"
	fi
	if [ X$tests != X ]; then
		st_args="$st_args -t $tests"
	fi
	local st_u="$st_exec $st_args"

	# Run it
	echo Running system tests ...
	echo "# $st_u" >/dev/tty
	eval $st_u || {
		echo "Failed to run Clovis ST !!"
		return 1
	}

	return 0
}

function clovis_st_stop_u ()
{
	echo "Stop Clovis system tests ..."
	pkill -INT -f c0st

	while [ `ps ax | grep -v grep | grep c0st` ]; do
		echo -n '.'
		sleep 2;
	done
}

function clovis_st_list_tests ()
{
	# Assembly command
	local st_exec="$st_util_dir/../user_space/c0st"
	if [ ! -f $st_exec ];then
		echo "Can't find c0st"
		return 1
	fi

	local st_args="-l"
	local st_u="$st_exec $st_args"

	# Run it
	echo Running system tests ...
	echo "# $st_u" >/dev/tty
	eval $st_u || {
		echo "Failed to run Clovis ST !!"
		return 1
	}

	return 0
}

# Get the location of this script and look for c0st and kernel
# module in known locations (should changed to a more robust way)
st_util_dir=`dirname $0`

# Local variables:
# sh-basic-offset: 8
# sh-indentation: 8
# tab-width: 8
# End:
