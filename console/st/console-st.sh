#!/bin/bash

NODE_UUID=02e94b88-19ab-4166-b26b-91b51f22ad91

. `dirname $0`/../../m0t1fs/linux_kernel/st/common.sh

CONSOLE_PATH=`dirname $0`/../
OUTPUT_FILE=/tmp/console.output
YAML_FILE9=/tmp/console9.yaml
YAML_FILE41=/tmp/console41.yaml
TEST_DIR=/var/tmp/console-st
prog_start="$MERO_CORE_ROOT/console/st/server"
prog_exec="$MERO_CORE_ROOT/console/st/.libs/lt-server"

start_server()
{
	modprobe lnet
	modload_galois >& /dev/null
        echo 8 > /proc/sys/kernel/printk
        modload || return $?

	. /etc/rc.d/init.d/functions

	$CONSOLE_PATH/st/server -v 2>&1 1>/dev/null &
	sleep 1
        status $prog_exec
        if [ $? -eq 0 ]; then
		echo "Service started."
        else
		echo "Service failed to start."
		return 1
        fi

	if [ ! -d "$TEST_DIR" ]; then
		mkdir "$TEST_DIR"
		if [ $? -ne 0 ]; then
			echo "Cannot create test directory"
			exit 1
		fi
	fi

cat <<EOF >$YAML_FILE9
server  : 0@lo:12345:34:1
client  : 0@lo:12345:34:*

Test FOP:
     - cons_test_type : A
       cons_test_id : 495
       cons_seq : 144
       cons_oid : 233
       cons_size : 5
       cons_buf : abcde
EOF

cat <<EOF >$YAML_FILE41
server  : 0@lo:12345:34:1
client  : 0@lo:12345:34:*

Write FOP:
  - fvv_read : 10
    fvv_write : 11
    f_container : 12
    f_key : 13
    id_nr : 1
    nbd_len : 5
    nbd_data : Hello
    cis_nr : 1
    ci_nr : 1
    ci_index : 17
    ci_count : 100
    au64s_nr : 1
    au64s_data : 257
    crw_flags : 21
EOF

}

stop_server()
{
	killproc $prog_exec &>> /dev/null
	sleep 1
	modunload
	modunload_galois

	rm -rf $YAML_FILE9 $YAML_FILE41 $OUTPUT_FILE $TEST_DIR
}

test_reply()
{
	request=$1

	reply=`cat $OUTPUT_FILE | grep replied | awk '{print $5}'`
	if [ $reply -ne $request ]
	then
		echo "Wrong reply received"
		return 1
	fi

	return 0
}

console_st()
{
	cd $TEST_DIR

	COMMAND="$CONSOLE_PATH/bin/m0console "
	# Test console test fop, opcode 9 with input provided as xcode read format
	$COMMAND -f 9 -v -d '(65, 22, (144, 233), "abcde")' &> $OUTPUT_FILE
	test_reply 08
	if [ $? -eq 0 ]
	then
		echo "Console test fop (Request 9, Reply 8): successfull"
	fi

	# Test console test fop, opcode 9 with input provided as yaml file
	$COMMAND -f 9 -v -i -y $YAML_FILE9 &> $OUTPUT_FILE
	test_reply 08
	if [ $? -eq 0 ]
	then
		echo "Console test fop (Request 9, Reply 8): successfull"
	fi

	$COMMAND -f 41 -v -i -y $YAML_FILE41 &> $OUTPUT_FILE
	test_reply 43
	if [ $? -eq 0 ]
	then
		echo "Write request fop (Request 43, Reply 41): successfull"
	fi
}

start_server

console_st

stop_server
