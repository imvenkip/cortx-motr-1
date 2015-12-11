#!/bin/sh

#set -x

. `dirname $0`/common.sh
. `dirname $0`/m0t1fs_common_inc.sh
. `dirname $0`/m0t1fs_client_inc.sh
. `dirname $0`/m0t1fs_server_inc.sh
. `dirname $0`/m0t1fs_sns_common_inc.sh

###################################################
# SNS repair is only supported in COPYTOOL mode,
# because ios need to hash gfid to mds. In COPYTOOL
# mode, filename is the string format of gfid.
###################################################
files=(
	0:10000
	0:10001
	0:10002
	0:10003
	0:10004
	0:10005
	0:10006
	0:10007
	0:10008
	0:10009
	0:10010
	0:10011
)

new_files=(
	0:10012
	0:10013
	0:10014
	0:10015
	0:10016
	0:10017
	0:10018
	0:10019
	0:10020
	0:10021
	0:10022
	0:10023
)

bs=(
	32
	32
	32
	32
	32
	32
	32
	32
	32
	10240
	32
	32
)

file_size=(
	50
	70
	30
	0
	40
	0
	60
	90
	10
	15
	5
	80
)

N=3
K=3
P=15
stride=64
src_bs=10M
src_count=17

verify()
{
	local FILE=$1
	local BS=$2
	local COUNT=$3

	local_read $BS $COUNT || return $?
	read_and_verify $FILE $BS $COUNT || return $?

	echo "$FILE verification sucess"
}

verify_all()
{
	local fids=("${!1}")
	local start=$2
	local end=$3

        for ((i=$start; i < $end; i++)) ; do
		verify ${fids[$i]} $((${bs[$i]} * 1024))  ${file_size[$i]} || return $?
        done
}

create_files_and_checksum()
{
	local fids=("${!1}")
	local start=$2
	local end=$3
	local unit_size=$((stride * 1024))

	# With unit size of 32K dd fails for the file "1009".
	# It runs with unit size 64K. A jira MERO-1086 tracks this issue.
	for ((i=$start; i < $end; i++)) ; do
		touch_file $MERO_M0T1FS_MOUNT_DIR/${fids[$i]} $stride
		_dd ${fids[$i]} $((${bs[$i]} * 1024)) ${file_size[$i]}
		verify ${fids[$i]} $((${bs[$i]} * 1024)) ${file_size[$i]}
	done
}

sns_repair_test()
{
	local rc=0
	local fail_device1=1
	local fail_device2=9
	local fail_device3=3
	local unit_size=$((stride * 1024))

	echo "Creating local source file"
	local_write $src_bs $src_count  || return $?

	echo "Starting SNS repair testing ..."

	create_files_and_checksum files[@] 0 ${#files[*]}

	for ((i=0; i < ${#IOSEP[*]}; i++)) ; do
		ios_eps="$ios_eps -S ${lnet_nid}:${IOSEP[$i]}"
	done

	mount

####### Set Failure device
	pool_mach_set_state "failed" $fail_device1 $fail_device2 || return $?

	echo "Device $fail_device1 and $fail_device2 failed. Do dgmode read"
	verify_all files[@] 0 ${#files[*]} || return $?

	pool_mach_query $fail_device1 $fail_device2 || return $?

	echo "*** Start sns repair and it will run in background ****"
	pool_mach_set_state "repairing" $fail_device1 $fail_device2 || return $?
	sns_repair
	sleep 5
	#echo "**** Create files while sns repair is in-progress ****"
	#create_files_and_checksum new_files[@] 0 4

	echo **** Perform read during repair. ****
	verify_all files[@] 0 ${#files[*]} || return $?
	#verify_all new_files[@] 0 4 || return $?

	echo "wait for sns repair"
	wait_for_sns_repair_or_rebalance "repair" || return $?
	pool_mach_set_state "repaired" $fail_device1 $fail_device2 || return $?
	echo "SNS Repair done."

	verify_all files[@] 0 ${#files[*]} || return $?
	#verify_all new_files[@] 0 4 || return $?

####### Query device state
	pool_mach_query $fail_device1 $fail_device2 || return $?

        echo "Starting SNS Re-balance.."
	pool_mach_set_state "rebalancing" $fail_device1 $fail_device2 || return $?
	sns_rebalance || return $?

	wait_for_sns_repair_or_rebalance "rebalance" || return $?
	pool_mach_set_state "online" $fail_device1 $fail_device2 || return $?
	echo "SNS Rebalance done."

	verify_all files[@] 0 ${#files[*]} || return $?
	#verify_all new_files[@] 0 4 || return $?

	pool_mach_set_state "failed" $fail_device3 || return $?

	echo "**** Start sns repair and it will run in background ****"
	pool_mach_set_state "repairing" $fail_device3 || return $?
	sns_repair
	sleep 5
	#echo "**** Create files while sns repair is in-progress ****"
	#create_files_and_checksum new_files[@] 4 8

	echo **** Perform read during repair. ****
	verify_all files[@] 0 ${#files[*]} || return $?
	#verify_all new_files[@] 0 8 || return $?

	echo "wait for sns repair"
	wait_for_sns_repair_or_rebalance "repair" || return $?
	pool_mach_set_state "repaired" $fail_device3 || return $?

	echo "SNS Repair done."
	verify_all files[@] 0 ${#files[*]} || return $?
	#verify_all new_files[@] 0 8 || return $?

	pool_mach_query $fail_device3 || return $?

	verify_all files[@] 0 ${#files[*]} || return $?

	echo "Starting SNS Re-balance.."
	pool_mach_set_state "rebalancing" $fail_device3 || return $?
	sns_rebalance || return $?

	echo "SNS Rebalance done."
	verify_all files[@] 0 ${#files[*]} || return $?
	#verify_all new_files[@] 0 8 || return $?

	echo "wait for SNS Re-balance "
	wait_for_sns_repair_or_rebalance "rebalance" || return $?
	pool_mach_set_state "online" $fail_device3 || return $?

	pool_mach_query $fail_device1 $fail_device2 $fail_device3

	verify_all files[@] 0 ${#files[*]} || return $?

	return $?
}

main()
{
	local rc=0

	NODE_UUID=`uuidgen`
	local multiple_pools=0

	sandbox_init

	mero_service start $multiple_pools  $stride $N $K $P || {
		echo "Failed to start Mero Service."
		return 1
	}

	sns_repair_mount $N $K $P || rc=$?

	if [[ $rc -eq 0 ]] && ! sns_repair_test ; then
		echo "Failed: SNS repair failed.."
		rc=1
	fi

	echo "unmounting and cleaning.."
	unmount_and_clean &>> $MERO_TEST_LOGFILE

	mero_service stop || {
		echo "Failed to stop Mero Service."
		rc=1
	}

	if [ $rc -eq 0 ]; then
		sandbox_fini
	else
		echo "Test log available at $MERO_TEST_LOGFILE."
	fi
	return $rc
}

trap unprepare EXIT
main

# this msg is used by Jenkins as a test success criteria;
# it should appear on STDOUT
if [ $? -eq 0 ] ; then
    echo "sns-multi: test status: SUCCESS"
fi
