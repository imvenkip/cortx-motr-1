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
. $mero_src/m0t1fs/linux_kernel/st/m0t1fs_sns_common_inc.sh

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

# Read/Write an object via Clovis
io_conduct()
{
	operation=$1
	source=$2
	dest=$3
	verify=$4

	local cmd_exec
	if [ $operation == "READ" ]
	then
		cmd_args="$CLOVIS_LOCAL_EP $CLOVIS_HA_EP '$CLOVIS_PROF_OPT' '$CLOVIS_PROC_FID' $verify $source"
		cmd_exec="${clovis_st_util_dir}/c0cat"
		cmd_args="$cmd_args $BLOCKSIZE $BLOCKCOUNT"
		local cmd="$cmd_exec $cmd_args > $dest &"
	else
		cmd_args="$CLOVIS_LOCAL_EP $CLOVIS_HA_EP '$CLOVIS_PROF_OPT' '$CLOVIS_PROC_FID' $verify $dest $source"
		cmd_exec="${clovis_st_util_dir}/c0cp"
		cmd_args="$cmd_args $BLOCKSIZE $BLOCKCOUNT"
		local cmd="$cmd_exec $cmd_args &"
	fi
	cwd=`pwd`
	cd $CLOVIS_TRACE_DIR

	eval $cmd
	clovis_pids[$cnt]=$!
	wait ${clovis_pids[$cnt]}
	if [ $? -ne 0 ]
	then
		echo "  Failed to run command $cmd_exec"
		cd $cwd
		return 1
	fi
	cnt=`expr $cnt + 1`
	cd $cwd
	return 0
}

function clovis_st_disk_state_set()
{
	local service_eps=$(service_eps_get)

	service_eps+=($CLOVIS_HA_EP)

	ha_events_post "${service_eps[*]}" $@
}

function dix_init()
{
	local m0dixinit="$M0_SRC_DIR/dix/utils/m0dixinit"
	local pverid=$(echo $DIX_PVERID | tr -d ^)
	if [ ! -f $m0dixinit ] ; then
		echo "Can't find m0dixinit"
		return 1
	fi

	cmd="$m0dixinit -l $CLOVIS_LOCAL_EP -H $CLOVIS_HA_EP \
	    -p '$CLOVIS_PROF_OPT' -I '$pverid' -d '$pverid' -a create"
	echo $cmd
	eval "$cmd"
	if [ $? -ne 0 ]
	then
		echo "Failed to initialise kvs..."
		return 1
	fi
}

function dix_destroy()
{
	local m0dixinit="$M0_SRC_DIR/dix/utils/m0dixinit"
	local pverid=$(echo $DIX_PVERID | tr -d ^)
	if [ ! -f $m0dixinit ] ; then
		echo "Can't find m0dixinit"
		return 1
	fi

	cmd="$m0dixinit -l $CLOVIS_LOCAL_EP -H $CLOVIS_HA_EP \
	    -p '$CLOVIS_PROF_OPT' -I '$pverid' -d '$pverid' -a destroy"
	echo $cmd
	eval "$cmd"
	if [ $? -ne 0 ]
	then
		echo "Failed to destroy kvs..."
		return 1
	fi
}

mero_service_start()
{
	local n=$1
	local k=$2
	local p=$3
	local stride=$4
	local multiple_pools=0
	echo "(N, K, P) = ($n $k $p)"
	mero_service start $multiple_pools $stride $n $k $p
	if [ $? -ne 0 ]
	then
		echo "Failed to start Mero Service..."
		return 1
	fi
	echo "mero service started"

	ios_eps=""
	for ((i=0; i < ${#IOSEP[*]}; i++)) ; do
		ios_eps="$ios_eps -S ${lnet_nid}:${IOSEP[$i]}"
	done
	return 0
}

mero_service_stop()
{
	mero_service stop
	if [ $? -ne 0 ]
	then
		echo "Failed to stop Mero Service..."
		return 1
	fi
}

error_handling()
{
	unmount_and_clean &>>$MERO_TEST_LOGFILE
	mero_service_stop
	echo "Test log file available at $CLOVIS_TEST_LOGFILE"
	echo "Clovis trace files are available at: $CLOVIS_TRACE_DIR"
	exit $1
}

# Local variables:
# sh-basic-offset: 8
# sh-indentation: 8
# tab-width: 8
# End:
