#!/bin/sh

#set -x

. `dirname $0`/common.sh
. `dirname $0`/m0t1fs_common_inc.sh
. `dirname $0`/m0t1fs_client_inc.sh
. `dirname $0`/m0t1fs_server_inc.sh

pool_mach_test()
{
	rc=0

	echo "Testing pool machine.."
	for ((i=1; i < ${#EP[*]}; i++)) ; do
		IOSEP="$IOSEP -S ${lnet_nid}:${EP[$i]}"
	done

####### Query
	trigger="$MERO_CORE_ROOT/pool/m0poolmach -O Query -T device -I 1
                         -C ${lnet_nid}:${SNS_CLI_EP} $IOSEP"
	echo $trigger

	if ! $trigger ; then
		echo "m0poolmach failed"
		rc=1
	else
		echo "m0poolmach done."
		rc=0
	fi

####### Set
	trigger="$MERO_CORE_ROOT/pool/m0poolmach -O Set -T device -I 1 -s 1
                         -C ${lnet_nid}:${SNS_CLI_EP} $IOSEP"
	echo $trigger

	if ! $trigger ; then
		echo "m0poolmach failed"
		rc=1
	else
		echo "m0poolmach done."
		rc=0
	fi

####### Query again
	trigger="$MERO_CORE_ROOT/pool/m0poolmach -O Query -T device -I 1
                         -C ${lnet_nid}:${SNS_CLI_EP} $IOSEP"
	echo $trigger

	if ! $trigger ; then
		echo "m0poolmach failed"
		rc=1
	else
		echo "m0poolmach done."
		rc=0
	fi

####### Set again. This set request should get error
	trigger="$MERO_CORE_ROOT/pool/m0poolmach -O Set -T device -I 1 -s 0
                         -C ${lnet_nid}:${SNS_CLI_EP} $IOSEP"
	echo $trigger

	if ! $trigger ; then
		echo "m0poolmach failed"
		rc=1
	else
		echo "m0poolmach done."
		rc=0
	fi

####### Set again. This set request should get error
	trigger="$MERO_CORE_ROOT/pool/m0poolmach -O Set -T device -I 1 -s 1
                         -C ${lnet_nid}:${SNS_CLI_EP} $IOSEP"
	echo $trigger

	if ! $trigger ; then
		echo "m0poolmach failed"
		rc=1
	else
		echo "m0poolmach done."
		rc=0
	fi

####### Set again. This set request should get error
	trigger="$MERO_CORE_ROOT/pool/m0poolmach -O Set -T device -I 1 -s 2
                         -C ${lnet_nid}:${SNS_CLI_EP} $IOSEP"
	echo $trigger

	if ! $trigger ; then
		echo "m0poolmach failed"
		rc=1
	else
		echo "m0poolmach done."
		rc=0
	fi

####### Query again
	trigger="$MERO_CORE_ROOT/pool/m0poolmach -O Query -T device -I 1
                         -C ${lnet_nid}:${SNS_CLI_EP} $IOSEP"
	echo $trigger

	if ! $trigger ; then
		echo "m0poolmach failed"
		rc=1
	else
		echo "m0poolmach done."
		rc=0
	fi

	return $rc
}

main()
{
	NODE_UUID=`uuidgen`
	mero_service start
	if [ $? -ne "0" ]
	then
		echo "Failed to start Mero Service."
		return 1
	fi

	rc=0
	pool_mach_test || {
		echo "Failed: pool machine failure."
		rc=1
	}

	mero_service stop
	if [ $? -ne "0" ]
	then
		echo "Failed to stop Mero Service."
		return 1
	fi

	return $rc
}

trap unprepare EXIT

main
