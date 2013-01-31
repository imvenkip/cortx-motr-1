#!/bin/bash

NODE_UUID=02e94b88-19ab-4166-b26b-91b51f22ad91

. `dirname $0`/../../m0t1fs/linux_kernel/st/common.sh

CONSOLE_PATH=`dirname $0`/../
OUTPUT_FILE=/tmp/console.output
YAML_FILE=/tmp/console.yaml
prog_start="$MERO_CORE_ROOT/console/st/server"
prog_exec="$MERO_CORE_ROOT/console/st/.libs/lt-server"

start_server()
{
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

cat <<EOF >$YAML_FILE
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
}

stop_server()
{
	killproc $prog_exec &>> /dev/null
	sleep 1
	modunload
	modunload_galois
}

test_reply()
{
	request=$1

	reply=`cat $OUTPUT_FILE | grep cons_notify_type | awk '{print $3}'`
	if [ $reply != $request ]; then
		echo "Wrong reply received"
		exit 1
	else
		echo "Send/Receive for opcode $request successfull"
	fi
}

console_st()
{
	COMMAND="$CONSOLE_PATH/bin/m0console "
	# Test console test fop, opcode 9 with input provided as xcode read format
	$COMMAND -f 9 -v -d '(65, 22, (144, 233), "abcde")' &> $OUTPUT_FILE
	test_reply 9
	# Test console test fop, opcode 9 with input provided as yaml file
	$COMMAND -f 9 -v -i -y $YAML_FILE &> $OUTPUT_FILE
	test_reply 9
}

start_server

console_st

stop_server
