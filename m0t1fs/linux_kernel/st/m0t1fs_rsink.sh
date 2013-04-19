#!/bin/sh

#set -x

ADDB_DUMP_FILE="/tmp/addb_dump_$$.txt"
addb_dump="$MERO_CORE_ROOT/addb/dump/m0addbdump"

CLIENT_CONTEXT1="m0_addb_ct_m0t1fs_op_read"
CLIENT_CONTEXT2="m0_addb_ct_m0t1fs_op_write"

collect_addb_from_all_services()
{
	for ((i=0; i < ${#EP[*]}; i++)) ; do
		cmd="cd $MERO_M0T1FS_TEST_DIR/d$i;
		$addb_dump -T $MERO_STOB_DOMAIN -D db \
		-A addb-stobs >> $ADDB_DUMP_FILE"
		echo $cmd
		eval $cmd
	done
}

rpcsink_addb_st()
{
	cmd="grep -e $CLIENT_CONTEXT1 -e $CLIENT_CONTEXT2 $ADDB_DUMP_FILE"
	echo $cmd
	eval $cmd > /dev/null
	rc=$?

	if [ $rc -eq 0 ]
	then
		echo "Test: RPC sink PASSED"
	else
		echo "Test: RPC sink FAILED"
	fi

	rm -f $ADDB_DUMP_FILE

	return $rc
}
