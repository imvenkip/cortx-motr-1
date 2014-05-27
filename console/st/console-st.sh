#!/bin/bash
set -eu

umask 0002

[ `id -u` -eq 0 ] || die 'Must be run by superuser'

## CAUTION: This path will be removed by superuser.
SANDBOX_DIR=${SANDBOX_DIR:-~/_sandbox.console-st}

M0_CORE_DIR=`readlink -f $0`
M0_CORE_DIR=${M0_CORE_DIR%/*/*/*}

CLIENT=$M0_CORE_DIR/console/bin/m0console
SERVER=$M0_CORE_DIR/console/st/server
SERVER_EXEC=$M0_CORE_DIR/console/st/.libs/lt-server

OUTPUT_FILE=$SANDBOX_DIR/client.log
YAML_FILE9=$SANDBOX_DIR/req-9.yaml
YAML_FILE41=$SANDBOX_DIR/req-41.yaml
SERVER_EP_ADDR='0@lo:12345:34:1'
CLIENT_EP_ADDR='0@lo:12345:34:*'

NODE_UUID=02e94b88-19ab-4166-b26b-91b51f22ad91   # required by `common.sh'
. $M0_CORE_DIR/m0t1fs/linux_kernel/st/common.sh  # modload_galois

die() { echo "$@" >&2; exit 1; }

start_server()
{
	modprobe lnet
	modload_galois &>/dev/null
	echo 8 >/proc/sys/kernel/printk
	modload

	set +eu
	. /etc/rc.d/init.d/functions  # import `status' function definition
	set -eu

	echo -n 'Running m0mkfs... ' >&2
	##
	## NOTE: The list of options passed to m0mkfs command should
	## correspond to the content of `server_argv' array in
	## console/st/server.c, but not in `-s' parameters department:
	## m0mkfs does not call m0_cs_default_stypes_init(), which
	## registers "ds1" and "ds2" service types, so we do not pass
	## these services to m0mkfs.
	##
	$M0_CORE_DIR/utils/mkfs/m0mkfs -T AD -D console_st_srv.db \
	    -S console_st_srv.stob -A linuxstob:console_st_srv-addb.stob \
	    -w 10 -e lnet:$SERVER_EP_ADDR -s ioservice -q 2 -m $((1 << 17)) \
	    &>$SANDBOX_DIR/mkfs.log || die 'm0mkfs failed'
	echo 'OK' >&2

	$SERVER -v &>$SANDBOX_DIR/server.log &
	sleep 1
	status $SERVER_EXEC || die 'Service failed to start'
	echo 'Service started' >&2
}

create_yaml_files()
{
	cat <<EOF >$YAML_FILE9
server  : $SERVER_EP_ADDR
client  : $CLIENT_EP_ADDR

Test FOP:
     - cons_test_type : A
       cons_test_id : 495
       cons_seq : 144
       cons_oid : 233
       cons_size : 5
       cons_buf : abcde
EOF

	## The content of `Write FOP' below should correspond to
	## m0_fop_cob_writev definition (see ioservice/io_fops.h).
	cat <<EOF >$YAML_FILE41
server  : $SERVER_EP_ADDR
client  : $CLIENT_EP_ADDR

Write FOP:
  - # m0_fop_cob_writev.c_rwv :: m0_fop_cob_rw
    ## m0_fop_cob_rw.crw_version :: m0_fv_version
    fvv_read : 10
    fvv_write : 11
    ## m0_fop_cob_rw.crw_gfid :: m0_fid
    f_container : 12
    f_key : 13
    ## m0_fop_cob_rw.crw_fid :: m0_fid
    f_container : 14
    f_key : 15
    ## m0_fop_cob_rw.crw_desc :: m0_io_descs
    id_nr : 1
    ### m0_io_descs.id_descs :: m0_net_buf_desc_data
    #### m0_net_buf_desc_data.bdd_desc :: m0_net_buf_desc
    nbd_len : 5
    nbd_data : Hello
    ###
    bdd_used : 5
    ## m0_fop_cob_rw.crw_ivec :: m0_io_indexvec
    ci_nr : 1
    ### m0_io_indexvec.ci_iosegs :: m0_ioseg
    ci_index : 17
    ci_count : 100
    ## m0_fop_cob_rw.crw_addb_ctx_id :: m0_addb_uint64_seqo
    au64s_nr : 1
    au64s_data : 257
    crw_flags : 21
    ## m0_fop_cob_rw.crw_di_data :: m0_buf
    b_nob : 0
    b_addr : 0
EOF
}

stop_server()
{
	killproc $SERVER_EXEC &>/dev/null && wait || true
	modunload
	modunload_galois
}

check_reply()
{
	expected="$1"
	actual=`awk '/replied/ {print $5}' $OUTPUT_FILE`
	[ -z "$actual" ] && die 'Reply not found'
	[ $actual -eq $expected ] || die 'Invalid reply'
}

test_fop()
{
	[ $# -gt 3 ] || die 'test_fop: Wrong number of arguments'
	local message="$1"; shift
	local request="$1"; shift
	local reply="$1"; shift

	echo -n "$message: " >&2
	$CLIENT -f $request -v "$@" &>$OUTPUT_FILE
	check_reply $reply
	echo OK >&2
}

run_st()
{
	test_fop 'Console test fop, xcode input' 9  8 \
		-d '(65, 22, (144, 233), "abcde")'
	create_yaml_files
	test_fop 'Console test fop, YAML input' 9  8 -i -y $YAML_FILE9

	## This test case does not work: $SERVER crashes while
	## processing the fop (opcode 41, m0_fop_cob_writev).
	## See https://jira.xyratex.com/browse/MERO-294 or
	## https://trello.com/c/ZdjHaHXc for details.
	if false; then
	    test_fop 'Write request fop' 41 43 -i -y $YAML_FILE41
	fi
}

## -------------------------------------------------------------------
## main
## -------------------------------------------------------------------

rm -rf $SANDBOX_DIR
mkdir $SANDBOX_DIR
cd $SANDBOX_DIR

start_server
run_st
stop_server

cd - >/dev/null
rm -r $SANDBOX_DIR
