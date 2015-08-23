#!/bin/sh

#set -x

. `dirname $0`/common.sh
. `dirname $0`/m0t1fs_common_inc.sh
. `dirname $0`/m0t1fs_client_inc.sh
. `dirname $0`/m0t1fs_server_inc.sh

pool_mach_test()
{
	local ios_eps
	rc=0

	echo "Testing pool machine.."
	for ((i=0; i < ${#IOSEP[*]}; i++)) ; do
		ios_eps="$ios_eps -S ${lnet_nid}:${IOSEP[$i]}"
	done

####### Query
	trigger="$MERO_CORE_ROOT/pool/m0poolmach -O Query -T device -N 1 -I 1
                         -C ${lnet_nid}:${POOL_MACHINE_CLI_EP} $ios_eps"
	echo $trigger
	eval $trigger
	rc=$?
	if [ $rc != 0 ] ; then
		echo "m0poolmach failed: $rc"
	else
		echo "m0poolmach done."
	fi

####### Set
	trigger="$MERO_CORE_ROOT/pool/m0poolmach -O Set -T device -N 1 -I 1 -s 1
                         -C ${lnet_nid}:${POOL_MACHINE_CLI_EP} $ios_eps"
	echo $trigger
	eval $trigger
	rc=$?
	if [ $rc != 0 ] ; then
		echo "m0poolmach failed: $rc"
	else
		echo "m0poolmach done."
	fi

####### Query again
	trigger="$MERO_CORE_ROOT/pool/m0poolmach -O Query -T device -N 1 -I 1
                         -C ${lnet_nid}:${POOL_MACHINE_CLI_EP} $ios_eps"
	echo $trigger
	eval $trigger
	rc=$?
	if [ $rc != 0 ] ; then
		echo "m0poolmach failed: $rc"
	else
		echo "m0poolmach done."
	fi

####### Set again. This set request should get error
	trigger="$MERO_CORE_ROOT/pool/m0poolmach -O Set -T device -N 1 -I 1 -s 0
                         -C ${lnet_nid}:${POOL_MACHINE_CLI_EP} $ios_eps"
	echo $trigger
	eval $trigger
	rc=$?
	if [ $rc != 0 ] ; then
		echo "m0poolmach failed: $rc"
	else
		echo "m0poolmach done."
	fi

####### Set again. This set request should get error
	trigger="$MERO_CORE_ROOT/pool/m0poolmach -O Set -T device -N 1 -I 1 -s 1
                         -C ${lnet_nid}:${POOL_MACHINE_CLI_EP} $ios_eps"
	echo $trigger
	eval $trigger
	rc=$?
	if [ $rc != 0 ] ; then
		echo "m0poolmach failed: $rc"
	else
		echo "m0poolmach done."
	fi

####### Set again. This set request should get error
	trigger="$MERO_CORE_ROOT/pool/m0poolmach -O Set -T device -N 1 -I 1 -s 2
                         -C ${lnet_nid}:${POOL_MACHINE_CLI_EP} $ios_eps"
	echo $trigger
	eval $trigger
	rc=$?
	if [ $rc != 0 ] ; then
		echo "m0poolmach failed: $rc"
	else
		echo "m0poolmach done."
	fi

####### Query again
	trigger="$MERO_CORE_ROOT/pool/m0poolmach -O Query -T device -N 1 -I 1
                         -C ${lnet_nid}:${POOL_MACHINE_CLI_EP} $ios_eps"
	echo $trigger
	eval $trigger
	rc=$?
	if [ $rc != 0 ] ; then
		echo "m0poolmach failed: $rc"
	else
		echo "m0poolmach done."
	fi

	return $rc
}

main()
{
	NODE_UUID=`uuidgen`
	local multiple_pools=0
	mero_service start $multiple_pools
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

# this msg is used by Jenkins as a test success criteria;
# it should appear on STDOUT
if [ $? -eq 0 ] ; then
    echo "poolmach: test status: SUCCESS"
fi
