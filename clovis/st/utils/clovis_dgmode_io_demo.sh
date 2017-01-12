#!/bin/sh

#set -x

clovis_st_util_dir=`dirname $0`
m0t1fs_st_dir=$clovis_st_util_dir/../../../m0t1fs/linux_kernel/st

# Re-use as many m0t1fs system scripts as possible
. $m0t1fs_dir/common.sh
. $m0t1fs_dir/m0t1fs_common_inc.sh
. $m0t1fs_dir/m0t1fs_client_inc.sh
. $m0t1fs_dir/m0t1fs_server_inc.sh
. $m0t1fs_dir/m0t1fs_sns_common_inc.sh

# Read/Write an object via Clovis
object_write()
{
	local cmd_args="$LOCAL_EP $CONFD_EP '$PROF_OPT' '$PROC_FID'"

	echo -n "  Enter ID of object to operate on: "
	read KEY
	cmd_args="$cmd_args $KEY"

	echo -n "Enter the name of the file to copy from: "
	read SRC_FILE

	echo -n "Enter the blocksize of the object: "
	read BLOCKSIZE

	echo -n "Enter the number of blocks to copy: "
	read BLOCKCOUNT

	cmd_args="$cmd_args $SRC_FILE $BLOCKSIZE $BLOCKCOUNT"

	# Use c0cp to write data to the object
	local cmd_exec="${clovis_st_util_dir}/c0cp"
	local cmd="$cmd_exec $cmd_args"

	# Run it
	# echo "  # $cmd" >/dev/tty

	eval $cmd || {
		echo "  Failed to run command c0cp"
		return 1
	}

	return 0
}

object_read()
{
	local cmd_args="$LOCAL_EP $CONFD_EP '$PROF_OPT' '$PROC_FID'"

	echo -n "  Enter ID of object to operate on: "
	read KEY
	cmd_args="$cmd_args $KEY"

	echo -n "  Enter the blocksize of the object: "
	read BLOCKSIZE

	echo -n "  Enter the number of blocks to read: "
	read BLOCKCOUNT

	cmd_args="$cmd_args $BLOCKSIZE $BLOCKCOUNT"

	echo -n "  Enter the name of the file to output to: "
	read OUTPUT

	cmd_args="$cmd_args > $OUTPUT"

	# Use c0cat to read data from the object
	local cmd_exec="${clovis_st_util_dir}/c0cat"
	local cmd="$cmd_exec $cmd_args"

	# Run it
	# echo "  # $cmd" >/dev/tty

	eval $cmd || {
		echo "  Failed to run command c0cat"
		return 1
	}

	return 0
}

# Dgmode IO

dgmode_io()
{
	local rc=0
	local fail_device=1

	echo "Starting DGMODE Read Demo ..."

	echo -n "1. Inject device failure ... "
	#pool_mach_set_failure $fail_device &>> /dev/null  || {
	pool_mach_set_failure $fail_device || {
		echo "failed."
		return $?
	}
	echo "done"

	echo "2. Check device states"
	eval pool_mach_query $fail_device

	echo "3. $io after failure: "
	case "$io" in
		read)
			object_read
			;;
		write)
			object_write
			;;
		*)
			echo "clovis_dgmode_io_demo [read|write]"
			return 1
	esac
	if [ $? != 0 ];then
		echo "Failed"
		return $?
	else
		echo "Passed"
	fi

	#echo -n "3. SNS Reparing ... "
	#eval sns_repair &>> /dev/null || {
	#eval sns_repair || {
	#	echo "failed."
	#	return $?
	#}

	#eval pool_mach_query $fail_device &>> /dev/null || {
	#	echo "failed"
	#	return $?
	#}
	#echo "done"

	#echo -n "4. IO after SNS repair ..."
	#$dir/c0cat || {
	#	echo "failed."
	#	return $?
	#}
	#echo "Passed"
}

# Get local address
modprobe lnet
lctl network up &>> /dev/null
LOCAL_NID=`lctl list_nids | head -1`
LOCAL_EP=$LOCAL_NID:12345:33:100
CONFD_EP=$LOCAL_NID:12345:33:100
PROF_OPT='<0x7000000000000001:0>'
PROC_FID='<0x7200000000000000:0>'

# ios_eps
ios_eps=""
for ((i=0; i < ${#IOSEP[*]}; i++)) ; do
	ios_eps="$ios_eps -S ${lnet_nid}:${IOSEP[$i]}"
done
# Read or Write?
io=$1
echo "DGMODE IO Demo ... "
dgmode_io
