#!/usr/bin/env bash
set -eu

#set -x
#export PS4='+ ${FUNCNAME[0]:+${FUNCNAME[0]}():}line ${LINENO}: '

## CAUTION: This path will be removed by superuser.
SANDBOX_DIR=${SANDBOX_DIR:-/var/mero/sandbox.stob-st}

M0_TRACE_IMMEDIATE_MASK=${M0_TRACE_IMMEDIATE_MASK:-all}
M0_TRACE_LEVEL=${M0_TRACE_LEVEL:-warn}
M0_TRACE_PRINT_CONTEXT=${M0_TRACE_PRINT_CONTEXT:-}

MAX_RPC_MSG_SIZE=163840
TM_MIN_RECV_QUEUE_LEN=2

error() { echo "$@" >&2; stop 1; }
say() { echo "$@" | tee -a $SANDBOX_DIR/confd/m0d.log; }

M0_SRC_DIR=`readlink -f $0`
M0_SRC_DIR=${M0_SRC_DIR%/*/*}

. $M0_SRC_DIR/utils/functions # die, sandbox_init, report_and_exit

## Path to the file with configuration string for confd.
CONF_FILE=$SANDBOX_DIR/confd/conf.txt

PROC_FID1="<0x7200000000000001:0>"
PROF_OPT="<0x7000000000000001:0>"

start() {
    sandbox_init
    _init
    stub_confdb | $M0_SRC_DIR/utils/m0confgen >$CONF_FILE
}

stop() {
    local rc=${1:-$?}

    trap - EXIT
    _fini
    if [ $rc -eq 0 ]; then
        sandbox_fini
    else
        report_and_exit stob-domain-recreate-on-corruption $rc
    fi
}

_init() {
    lnet_up
    m0_modules_insert
    mkdir -p $SANDBOX_DIR/confd
}

_fini() {
    m0_modules_remove
}

stub_confdb() {
    cat <<EOF
(root-0 verno=1 profiles=[profile-0])
(profile-0 filesystem=filesystem-0)
(filesystem-0 rootfid=(11, 22) redundancy=2
    params=["pool_width=3", "nr_data_units=1", "nr_parity_units=1",
            "unit_size=4096"]
    mdpool=pool-0 imeta_pver=pver-0 nodes=[node-0] pools=[pool-0] racks=[rack-0])
(node-0 memsize=16000 nr_cpu=2 last_state=3 flags=2 pool_id=pool-0
    processes=[process-0])
(process-0 cores=[3] mem_limit_as=0 mem_limit_rss=0 mem_limit_stack=0
    mem_limit_memlock=0 endpoint="$M0D1_ENDPOINT"
    services=[service-0, service-3, service-4])
(service-0 type=4 endpoints=["$M0D1_ENDPOINT"] sdevs=[])
(service-3 type=1 endpoints=["$M0D1_ENDPOINT"] sdevs=[sdev-0])
(service-4 type=3 endpoints=["$M0D1_ENDPOINT"] sdevs=[])
(pool-0 order=0 pvers=[pver-0, pver_f-11])
(pver-0 N=2 K=1 P=4 tolerance=[0, 0, 0, 0, 1] rackvs=[objv-0])
(pver_f-11 id=0 base=pver-0 allowance=[0, 0, 0, 0, 1])
(objv-0 real=rack-0 children=[objv-1])
(objv-1 real=enclosure-0 children=[objv-2])
(objv-2 real=controller-0 children=[])
(rack-0 encls=[enclosure-0] pvers=[pver-0])
(enclosure-0 ctrls=[controller-0] pvers=[pver-0])
(controller-0 node=node-0 disks=[] pvers=[pver-0])
(sdev-0 dev_idx=0 iface=4 media=1 bsize=4096 size=596000000000 last_state=3
    flags=4 filename="/dev/sdev0")
EOF
}

_mkfs() {
    local ep=$M0D1_ENDPOINT
    local fid=$PROC_FID1
    local path=$SANDBOX_DIR/confd
    local OPTS="${1:-} -D $path/db -T AD -S $path/stobs\
    -A linuxstob:$path/addb-stobs -e lnet:$ep\
    -m $MAX_RPC_MSG_SIZE -q $TM_MIN_RECV_QUEUE_LEN -c $CONF_FILE\
    -w 3 -P $PROF_OPT -f $fid"

    echo $M0_SRC_DIR/utils/mkfs/m0mkfs $OPTS
    $M0_SRC_DIR/utils/mkfs/m0mkfs $OPTS >>$path/mkfs.log ||
    error 'm0mkfs failed'
}

[ `id -u` -eq 0 ] || die 'Must be run by superuser'

trap stop EXIT

echo "Prepare"
start
cd $SANDBOX_DIR
say "mkfs"
_mkfs
say "mkfs (truncate addb-stobs id file)"
cd confd/addb-stobs
truncate -s 0 id
cd ../../
_mkfs
say "mkfs (truncate stobs id file)"
cd confd/stobs
truncate -s 0 id
cd ../../
_mkfs
echo "mkfs force"
_mkfs -F
say "mkfs force (truncate addb-stobs id file)"
cd confd/addb-stobs
truncate -s 0 id
cd ../../
_mkfs -F
say "mkfs force (truncate stobs id file)"
cd confd/stobs
truncate -s 0 id
cd ../../
_mkfs -F
say "Stop"
stop
report_and_exit stob-domain-recreate-on-corruption $?