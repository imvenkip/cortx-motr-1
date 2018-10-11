#!/bin/bash

clovis_st_util_dir=$(dirname $(readlink -f $0))
m0t1fs_dir="$clovis_st_util_dir/../../../m0t1fs/linux_kernel/st"

. $m0t1fs_dir/common.sh
. $m0t1fs_dir/m0t1fs_common_inc.sh
. $m0t1fs_dir/m0t1fs_client_inc.sh
. $m0t1fs_dir/m0t1fs_server_inc.sh
. $clovis_st_util_dir/clovis_local_conf.sh


SANDBOX_DIR=/var/mero
CLOVIS_TEST_DIR=$SANDBOX_DIR
CLOVIS_TEST_LOGFILE=$SANDBOX_DIR/clovis_`date +"%Y-%m-%d_%T"`.log
CLOVIS_TRACE_DIR=$SANDBOX_DIR/clovis

clean()
{
        multiple_pools=$1
	local i=0
	for ((i=0; i < ${#IOSEP[*]}; i++)) ; do
		# Removes the stob files created in stob domain since
		# there is no support for m0_stob_delete() and after
		# unmounting the client file system, from next mount,
		# fids are generated from same baseline which results
		# in failure of cob_create fops.
		local ios_index=`expr $i + 1`
		rm -rf $CLOVIS_TEST_DIR/d$ios_index/stobs/o/*
	done

        if [ ! -z $multiple_pools ] && [ $multiple_pools == 1 ]; then
		local ios_index=`expr $i + 1`
		rm -rf $CLOVIS_TEST_DIR/d$ios_index/stobs/o/*
        fi
}

dix_init()
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

mero_service_start()
{
	local multiple_pools=0
	mero_service start $multiple_pools
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
	rc=$1
	msg=$2
	clean 0 &>>$CLOVIS_TEST_LOGFILE
	mero_service_stop
	echo $msg
	echo "Test log file available at $CLOVIS_TEST_LOGFILE"
	echo "Clovis trace files are available at: $CLOVIS_TRACE_DIR"
	exit $1
}

main()
{
	sandbox_init

	NODE_UUID=`uuidgen`
	clovis_dgmode_sandbox="$CLOVIS_TEST_DIR/sandbox"
	src_file="$CLOVIS_TEST_DIR/src_file"
	dest_file="$CLOVIS_TEST_DIR/dest_file"
	object_id1=1048580
	object_id2=1048581
	block_size=4096
	block_count=1024
	READ_VERIFY="false"
	CLOVIS_PARAMS="$CLOVIS_LOCAL_EP $CLOVIS_HA_EP $CLOVIS_PROF_OPT $CLOVIS_PROC_FID"

	rm -f $src_file $dest_file

	dd if=/dev/urandom bs=$block_size count=$block_count of=$src_file 2> $CLOVIS_TEST_LOGFILE || {
		error_handling $? "Failed to create a source file"
	}
	mkdir $CLOVIS_TRACE_DIR

	mero_service_start
	dix_init

	# Test c0client utility
/usr/bin/expect  <<EOF
	set timeout 10
	spawn $clovis_st_util_dir/c0client $CLOVIS_PARAMS > /tmp/log
	expect "c0clovis >>"
	send -- "touch $object_id1\r"
	expect "c0clovis >>"
	send -- "write $object_id2 $src_file $block_size $block_count\r"
	expect "c0clovis >>"
	send -- "read $object_id2 $dest_file $block_size $block_count\r"
	expect "c0clovis >>"
	send -- "delete $object_id1\r"
	expect "c0clovis >>"
	send -- "delete $object_id2\r"
	expect "c0clovis >>"
	send -- "quit\r"
EOF
	diff $src_file $dest_file || {
		rc=$?
		error_handling $rc "Files are different"
	}

	$clovis_st_util_dir/c0touch $CLOVIS_PARAMS $object_id1 || {
		error_handling $? "Failed to create a object"
	}
	$clovis_st_util_dir/c0cp $CLOVIS_PARAMS $READ_VERIFY $object_id2 $src_file $block_size $block_count || {
		error_handling $? "Failed to copy object"
	}
	$clovis_st_util_dir/c0cat $CLOVIS_PARAMS $READ_VERIFY $object_id2 $block_size $block_count > $dest_file || {
		error_handling $? "Failed to read object"
	}
	$clovis_st_util_dir/c0unlink $CLOVIS_PARAMS $object_id1 || {
		error_handling $? "Failed to delete object"
	}
	$clovis_st_util_dir/c0unlink $CLOVIS_PARAMS $object_id2 || {
		error_handling $? "Failed to delete object"
	}

	diff $src_file $dest_file || {
		rc = $?
		error_handling $rc "Files are different"
	}

	clean &>>$CLOVIS_TEST_LOGFILE
	mero_service_stop

}

echo "Clovis Utils Test ... "
main
report_and_exit clovis-utils-st $?
