#!/bin/bash
# set -eux

# The script works with fio 2.2.10 with the following patch
# diff --git a/options.c b/options.c
# index 4798fbf..d2486ad 100644
# --- a/options.c
# +++ b/options.c
# @@ -824,7 +824,7 @@ int set_name_idx(char *target, size_t tlen, char *input, int index)
#         for (cur_idx = 0; cur_idx <= index; cur_idx++)
#                 fname = get_next_name(&str);
#
# -       if (client_sockaddr_str[0]) {
# +       if (0 && client_sockaddr_str[0]) {
#                 len = snprintf(target, tlen, "%s/%s.", fname,
#                                 client_sockaddr_str);
#         } else
#

CONFD_NODE="172.16.1.1"
SERVER_LIST="$(echo 172.16.{1,2}.{1..7})"
CLIENT_LIST="$(echo 10.22.192.{51,52,{59..64},68,69,70})"
TEST_TYPE="write"
NUMBER_OF_CLIENTS=$(echo $CLIENT_LIST | wc -w)
FILE_LID=13
TEST_DURATION="5m"
FILE_PREFIX="0:1000"
FILE_DIR=/mnt/m0t1fs
IO_BLOCK_SIZE="128M"
MAX_IO_SIZE_PER_CLIENT="8T"
THREADS_PER_CLIENT=10

COMMAND=

LID_TO_UNIT_MAP=(-1 4096 8192 16384 32768 65536 131072 262144 \
	         524288 1048576 2097152 4194304 8388608 16777216 33554432)
FIO_BINARY="/root/fio"

function on_each_server()
{
	for node in $SERVER_LIST; do
		ssh -n $node "$@" &
	done
	wait
}

function on_each_client()
{
	for node in $CLIENT_LIST; do
		ssh -n $node "$@" &
	done
	wait
}

function on_each_node()
{
	for node in $CLIENT_LIST $SERVER_LIST; do
		ssh -n $node "$@" &
	done
	wait
}

function on_confd_node()
{
	ssh -n $CONFD_NODE "$@"
}

function scp_to_each_node()
{
	local local_file="$1"
	local remote_file="$2"

	for node in $CLIENT_LIST $SERVER_LIST; do
		scp $local_file $node:$remote_file &
	done
	wait
}

function fio_script()
{
	local client_index=$1
	local client_index0=$(printf %02d $client_index)
	cat << EOF
# tested with patched fio-2.2.10
[global]
name="Concurrent write test"
directory=$FILE_DIR
fallocate=none
fadvise_hint=0
size=$MAX_IO_SIZE_PER_CLIENT
blocksize=$IO_BLOCK_SIZE
blockalign=$IO_BLOCK_SIZE
direct=1
overwrite=1
end_fsync=0
fsync_on_close=0
do_verify=0
invalidate=0
allow_file_create=0
create_on_open=0
file_append=1
clat_percentiles=1

[write]
runtime=$TEST_DURATION
readwrite=$TEST_TYPE
numjobs=$THREADS_PER_CLIENT
thread
group_reporting
ioengine=sync
filename_format=$FILE_PREFIX$client_index0\$jobnum
buffer_pattern=0x0
wait_for_previous
EOF
}

function help()
{
	cat << EOF
Usage:
	$0 [OPTIONS] -c command

Commands:
- configure
- mkfs
- start_servers
- start_clients
- test
- stop_clients
- stop_servers

Helper commands:
-i path_to_rpm   install rpm on clients and servers
-g               print genders file
-f               print fio script for the first client
-v               verbose mode (set -eux)
To speed up mkfs process:
-s  save /var/mero
-r  restore /var/mero

What is hardcoded:
- list of servers           $SERVER_LIST
- list of clients           $CLIENT_LIST
- confd node                $CONFD_NODE
- test type                 $TEST_TYPE
- fio binary                $FIO_BINARY
- genders file

What is configurable:
-C  number of clients       $NUMBER_OF_CLIENTS
-L  file lid                $FILE_LID
    unit size for file      ${LID_TO_UNIT_MAP[$FILE_LID]}
-T  test duration           $TEST_DURATION
-P  file prefix             $FILE_PREFIX
-D  file dir                $FILE_DIR
-B  io block size           $IO_BLOCK_SIZE
-S  max io size per client  $MAX_IO_SIZE_PER_CLIENT
-R  io threads per client   $THREADS_PER_CLIENT
EOF
}

function run_test()
{

# time rm $FILES
# time touch $FILES
# time setfattr -n lid -v 13 $FILES
	for node in $CLIENT_LIST; do
		ssh -n $node $FIO_BINARY --server &
	done
	# Do toucn and setfattr. It can be removed after
	# default lid can be used.
	client_index=0
	files=""
	for node in $CLIENT_LIST; do
		client_index0=$(printf %02d $client_index)
		for f in $(seq 0 $(expr $THREADS_PER_CLIENT - 1)); do
			files="$files $FILE_DIR/$FILE_PREFIX$client_index0$f"
		done
		((++client_index))
		if [ $client_index -eq $NUMBER_OF_CLIENTS ]; then
			break
		fi
	done
	echo "`date` Creating files on $node..."
	ssh -n $node "touch $files"
	ssh -n $node "setfattr -n lid -v $FILE_LID $files"
	echo "`date` Done."
	i=0
	FIO_PARAMS=""
	for node in $CLIENT_LIST; do
		fio_config="$node.fio"
		fio_script $i > "$fio_config"
		FIO_PARAMS="$FIO_PARAMS --client=$node $fio_config"
		((++i))
		if [ $i -eq $NUMBER_OF_CLIENTS ]; then
			break
		fi
	done
	$FIO_BINARY --eta-newline=5 --status-interval=30 $FIO_PARAMS
	for node in $CLIENT_LIST; do
		ssh -n $node pkill -x fio &
	done
	wait
}


function run_command()
{
	command=$1
	case "$COMMAND" in
	"configure")
		# disable addb2 and direct I/O for m0d
		on_each_server sed -e "s/^.*MERO_M0D_EXTRA_OPTS.*$/MERO_M0D_EXTRA_OPTS=\'-g\ -I\'/" -i /etc/sysconfig/mero

		# disable addb2 network submit in kernel module
		on_each_node sed "s/.*MERO_KMOD_EXTRA_PARAMS.*/MERO_KMOD_EXTRA_PARAMS=\'addb2_net_disable=1\'/" -i /etc/sysconfig/mero

		# disable traced
		on_each_node sed 's/^MERO_TRACED/#&/' -i /etc/sysconfig/mero

		# set default layout ID
		# XXX hack, need to read from genders
		ssh -n $CONFD_NODE sed \''s/.*$pool_width $data_units $parity_units.*/          [2: "$pool_width $data_units $parity_units", "'$FILE_LID'"],/'\' -i /usr/libexec/mero/mero-service.functions
		;;
	"mkfs")
		ssh -n $CONFD_NODE systemctl start mero-mkfs@confd
		ssh -n $CONFD_NODE systemctl start mero-server-confd
		i=0; for n in $SERVER_LIST; do ssh -n $n systemctl start mero-mkfs@ios$i & i=`expr $i + 1`; done
		ssh -n $CONFD_NODE systemctl start mero-mkfs@mds &
		ssh -n $CONFD_NODE systemctl start mero-mkfs@ha &
		wait
		;;
	"start_servers")
		ssh -n $CONFD_NODE systemctl start mero-server-confd
		on_each_server systemctl start mero
		;;
	"start_clients")
		on_each_client systemctl start mero-client
		;;
	"stop_clients")
		on_each_client systemctl stop mero-client
		;;
	"stop_servers")
		on_each_server systemctl stop mero
		;;
	"test")
		run_test
		;;
	esac
}

function main()
{
	if [ $# -eq 0 ]; then
		help
	fi
	while getopts "h?c:i:gfvC:L:T:P:D:B:S:R:" opt; do
		case "$opt" in
		h|\?)
			help
			;;
		c)
			COMMAND=$OPTARG
			run_command $COMMAND
			;;
		i)
			local rpm_file="$OPTARG"
			scp_to_each_node "$rpm_file" "$rpm_file"
			on_each_node rpm -U --force $rpm_file
			;;
		g)
			cat genders
			;;
		f)
			fio_script 0
			;;
		v)
			set -eux
			;;
		C)
			NUMBER_OF_CLIENTS=$OPTARG
			;;
		L)
			FILE_LID=$OPTARG
			;;
		T)
			TEST_DURATION=$OPTARG
			;;
		P)
			FILE_PREFIX=$OPTARG
			;;
		D)
			FILE_DIR=$OPTARG
			;;
		B)
			IO_BLOCK_SIZE=$OPTARG
			;;
		S)
			MAX_IO_SIZE_PER_CLIENT=$OPTARG
			;;
		R)
			THREADS_PER_CLIENT=$OPTARG
			;;
		esac
		COMMAND=""
	done
	shift $((OPTIND-1))
}

main "$@"
