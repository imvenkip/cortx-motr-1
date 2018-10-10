#!/bin/bash

# Script for starting or stopping Clovis system tests

random_mode=
tests=
debugger=
gdbparams=${GDBPARAMS:-}

# Get the location of this script and look for c0st and kernel
# module in known locations (should changed to a more robust way)
st_util_dir=$(readlink -f $0)
mero_src=$(echo $(dirname $st_util_dir) \
         | sed -r -e 's#/?clovis/st/utils/?$##' -e 's#^/usr/s?bin##')

. $mero_src/m0t1fs/linux_kernel/st/m0t1fs_common_inc.sh

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
	esac

	local st_kmod=$mero_src/clovis_st_kmod.ko
	local st_kmod_args="clovis_local_addr=$LOCAL_EP \
			    clovis_ha_addr=$HA_EP \
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

clovis_st_run_debugger()
{
    # Support gdb only currently
    local gdbinit=${mero_src:+-x ${mero_src}/.gdbinit}
    local binary=$2
    shift 2

    echo "gdb $gdbinit --args $binary $@"  >/dev/tty
    gdb $gdbinit $gdbparams --args $binary $@
}

# user space mode
function clovis_st_start_u()
{
	local idx_service=-1
	case $1 in
		"KVS")
			idx_service=1
			;;
		"CASS")
			idx_service=2
			;;
	esac

	# Debugger
	if [ X$2 != X ]; then
		debugger=$2
	fi

	# Assembly command
	local st_exec=
	if [ X$debugger != X ]; then
		st_exec="$mero_src/clovis/st/user_space/.libs/lt-c0st"
	else
		st_exec="$mero_src/clovis/st/user_space/c0st"
	fi
	if [ ! -f $st_exec ];then
		echo "Can't find $st_exec"
		return 1
	fi

	local st_args="-m $CLOVIS_LOCAL_EP -h $CLOVIS_HA_EP \
		       -p $CLOVIS_PROF_OPT -f $CLOVIS_PROC_FID \
		       -I $idx_service"
	if [ $random_mode -eq 1 ]; then
		st_args="$st_args -r"
	fi
	if [ X$tests != X ]; then
		st_args="$st_args -t $tests"
	fi
	local st_u="$st_exec $st_args"

	if [ $idx_service -eq 1 ]; then
		#create DIX metadata
		local m0dixinit="$mero_src/dix/utils/m0dixinit"
		local cmd

		local pverid=$(echo $DIX_PVERID | tr -d ^)

		if [ ! -f $m0dixinit ] ; then
			echo "Can't find m0dixinit"
			return 1
		fi

		cmd="$m0dixinit -l $CLOVIS_LOCAL_EP -H $CLOVIS_HA_EP \
	     	     -p '$CLOVIS_PROF_OPT' -I '$pverid' -d '$pverid' -a create"
		echo $cmd
		eval "$cmd"

		#create indices for composite extents
		local c0composite="$mero_src/clovis/st/utils/c0composite"
		local cmd

		if [ ! -f $c0composite ] ; then
			echo "Can't find c0composite"
			return 1
		fi

		cmd="$c0composite $CLOVIS_LOCAL_EP $CLOVIS_HA_EP \
		     $CLOVIS_PROF_OPT $CLOVIS_PROC_FID"
		echo $cmd
		eval "$cmd"


	fi

	# Run it
	if [ X$debugger != X ];then
		echo Running system tests in gdb...
		clovis_st_run_debugger $debugger $st_u
	else
		echo Running system tests ...
		echo "# $st_u" >/dev/tty
		eval $st_u || {
			echo "Failed to run Clovis ST !!"
			return 1
		}
	fi
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
	local st_exec="$mero_src/clovis/st/user_space/c0st"
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

# Local variables:
# sh-basic-offset: 8
# sh-indentation: 8
# tab-width: 8
# End:
