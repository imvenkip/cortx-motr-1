#!/usr/bin/env bash
set -eu
# set -x
export PS4='+ ${FUNCNAME[0]:+${FUNCNAME[0]}():}line ${LINENO}: '

## CAUTION: This path will be removed by superuser.
SANDBOX_DIR=${SANDBOX_DIR:-/var/mero/sandbox.spiel-st}

M0_TRACE_IMMEDIATE_MASK=${M0_TRACE_IMMEDIATE_MASK:-!rpc,formation,fop,memory,rm}
M0_TRACE_LEVEL=${M0_TRACE_LEVEL:-warn+}
M0_TRACE_PRINT_CONTEXT=${M0_TRACE_PRINT_CONTEXT:-}

MAX_RPC_MSG_SIZE=163840
TM_MIN_RECV_QUEUE_LEN=2
DEV_NR=4
DEV_SIZE=$((1024 * 1024))
INSTALLED_FILES=cleanup-on-quit.txt

error() { echo "$@" >&2; stop 1; }

M0_SRC_DIR=`readlink -f $0`
M0_SRC_DIR=${M0_SRC_DIR%/*/*/*}

. $M0_SRC_DIR/utils/functions # die, sandbox_init, report_and_exit

## Path to the file with configuration string for confd.
CONF_FILE=$SANDBOX_DIR/confd/conf.txt
CONF_DISKS=$SANDBOX_DIR/confd/disks.conf

PROC_FID_CNTR=0x7200000000000001
PROC_FID_KEY=0
PROC_FID_KEY2=4
PROC_FID="<$PROC_FID_CNTR:$PROC_FID_KEY>"
PROC_FID2="<$PROC_FID_CNTR:$PROC_FID_KEY2>"
PROF_OPT="<0x7000000000000001:0>"

PYTHON_BOILERPLATE="
if spiel.cmd_profile_set(str(fids['profile'])):
    sys.exit('cannot set profile {0}'.format(fids['profile']))

if spiel.rconfc_start():
    sys.exit('cannot start rconfc')"

iosloopdevs() {
    cat > $CONF_DISKS << EOF
    Device:
EOF
    for i in $(seq $DEV_NR); do
        dd if=/dev/zero of=$SANDBOX_DIR/${i}.img bs=$DEV_SIZE seek=$DEV_SIZE count=1
        losetup -d /dev/loop$i &> /dev/null || true
        losetup /dev/loop$i $SANDBOX_DIR/${i}.img
        cat >> $CONF_DISKS << EOF
       - id: $i
         filename: /dev/loop$i
EOF
    done
}

start() {
    # install "mero" Python module required by m0spiel tool
    cd $M0_SRC_DIR/utils/spiel
    python setup.py install --record $INSTALLED_FILES > /dev/null ||
        die 'Cannot install Python "mero" module'
    sandbox_init
    _init
    m0d_with_rms_start
}

stop() {
    local rc=${1:-$?}

    trap - EXIT
    if mount | grep -q m0t1fs; then umount $SANDBOX_DIR/mnt; fi

    killall -q lt-m0d && wait || rc=$?
    _fini
    if [ $rc -eq 0 ]; then
        sandbox_fini
    else
        say "Spiel test FAILED: $rc"
        exit $rc
    fi
}

_init() {
    lnet_up
    m0_modules_insert
    mkdir -p $SANDBOX_DIR/mnt
    mkdir -p $SANDBOX_DIR/confd
    mkdir -p $SANDBOX_DIR/systest-$$
    iosloopdevs
}

_fini() {
    for i in $(seq $DEV_NR); do
        losetup -d /dev/loop$i
    done
    m0_modules_remove
    cd $M0_SRC_DIR/utils/spiel
    cat $INSTALLED_FILES | xargs rm -rf
    rm -rf build/ $INSTALLED_FILES
}

stub_confdb() {
    cat <<EOF
(root-0 verno=1 profiles=[profile-0])
(profile-0 filesystem=filesystem-0)
(filesystem-0 rootfid=(11, 22) redundancy=1
    params=["pool_width=3", "nr_data_units=1", "nr_parity_units=1",
            "unit_size=4096"]
    mdpool=pool-1 imeta_pver=(0, 0) nodes=[node-0] pools=[pool-0, pool-1]
    racks=[rack-0])
(node-0 memsize=16000 nr_cpu=2 last_state=3 flags=2 pool_id=pool-0
    processes=[process-0, process-1])
(process-0 cores=[3] mem_limit_as=0 mem_limit_rss=0 mem_limit_stack=0
    mem_limit_memlock=0 endpoint="$M0D1_ENDPOINT"
    services=[service-0, service-1, service-2, service-3, service-4, service-5])
(process-1 cores=[3] mem_limit_as=0 mem_limit_rss=0 mem_limit_stack=0
    mem_limit_memlock=0 endpoint="$M0T1FS_ENDPOINT:1"
    services=[service-6])
(service-0 type=@M0_CST_RMS endpoints=["$M0D1_ENDPOINT"] sdevs=[])
(service-1 type=@M0_CST_HA endpoints=["$M0D1_ENDPOINT"] sdevs=[])
(service-2 type=@M0_CST_IOS endpoints=["$M0D1_ENDPOINT"]
    sdevs=[sdev-1, sdev-2, sdev-3, sdev-4])
(service-3 type=@M0_CST_MDS endpoints=["$M0D1_ENDPOINT"] sdevs=[sdev-0])
(service-4 type=@M0_CST_MGS endpoints=["$M0D1_ENDPOINT"] sdevs=[])
(service-5 type=@M0_CST_SSS endpoints=["$M0D1_ENDPOINT"] sdevs=[])
(service-6 type=@M0_CST_RMS endpoints=["$M0T1FS_ENDPOINT:1"] sdevs=[])
(pool-0 order=0 pvers=[pver-0, pver_f-11])
(pver-0 N=2 K=1 P=4 tolerance=[0, 0, 0, 0, 1] rackvs=[objv-0])
(pver_f-11 id=0 base=pver-0 allowance=[0, 0, 0, 0, 1])
(objv-0 real=rack-0 children=[objv-1])
(objv-1 real=enclosure-0 children=[objv-2])
(objv-2 real=controller-0 children=[objv-3, objv-4, objv-5, objv-6])
(objv-3 real=disk-0 children=[])
(objv-4 real=disk-1 children=[])
(objv-5 real=disk-2 children=[])
(objv-6 real=disk-3 children=[])
(rack-0 encls=[enclosure-0] pvers=[pver-0])
(enclosure-0 ctrls=[controller-0] pvers=[pver-0])
(controller-0 node=node-0 disks=[disk-0, disk-1, disk-2, disk-3] pvers=[pver-0])
(disk-0 dev=sdev-1 pvers=[pver-0])
(disk-1 dev=sdev-2 pvers=[pver-0])
(disk-2 dev=sdev-3 pvers=[pver-0])
(disk-3 dev=sdev-4 pvers=[pver-0])
(sdev-0 dev_idx=0 iface=4 media=1 bsize=4096 size=596000000000 last_state=3
    flags=4 filename="/dev/sdev0")
(sdev-1 dev_idx=0 iface=4 media=1 bsize=4096 size=596000000000 last_state=3
    flags=4 filename="/dev/sdev1")
(sdev-2 dev_idx=1 iface=7 media=2 bsize=8192 size=320000000000 last_state=2
    flags=4 filename="/dev/sdev2")
(sdev-3 dev_idx=2 iface=7 media=2 bsize=8192 size=320000000000 last_state=2
    flags=4 filename="/dev/sdev3")
(sdev-4 dev_idx=3 iface=7 media=2 bsize=8192 size=320000000000 last_state=2
    flags=4 filename="/dev/sdev4")
(pool-1 order=0 pvers=[pver-10])
(pver-10 N=1 K=0 P=1 tolerance=[0, 0, 0, 0, 1] rackvs=[objv-10])
(objv-10 real=rack-0 children=[objv-11])
(objv-11 real=enclosure-0 children=[objv-12])
(objv-12 real=controller-0 children=[objv-13])
(objv-13 real=disk-0 children=[])
EOF
}

### m0_spiel_start requires endpoint of RM service. This function starts the
### first m0d instance with rmservice. All Spiel commands from command
### interface part will affect to second m0d instance. Spiel commands from
### configuration management part may affect to both m0d.
m0d_with_rms_start() {
    local path=$SANDBOX_DIR/confd
    local OPTS="-F -D $path/db -T AD -S $path/stobs\
    -A linuxstob:$path/addb-stobs -e lnet:$M0D1_ENDPOINT\
    -m $MAX_RPC_MSG_SIZE -q $TM_MIN_RECV_QUEUE_LEN -c $CONF_FILE\
    -w 3 -P $PROF_OPT -f $PROC_FID -d $CONF_DISKS"

    stub_confdb | $M0_SRC_DIR/utils/m0confgen >$CONF_FILE

    echo "--- `date` ---" >>$path/m0d.log
    cd $path

    ## m0mkfs should be executed only once. It is usually executed
    ## during cluster initial setup.
    echo $M0_SRC_DIR/utils/mkfs/m0mkfs $OPTS
    $M0_SRC_DIR/utils/mkfs/m0mkfs $OPTS >>$path/mkfs.log ||
    error 'm0mkfs failed'

    echo $M0_SRC_DIR/mero/m0d $OPTS
    $M0_SRC_DIR/mero/m0d $OPTS >>$path/m0d.log 2>&1 &
    local PID=$!
    sleep 10
    kill -0 $PID 2>/dev/null ||
    error "Failed to start m0d. See $path/m0d.log for details."
}

test_m0d_start() {
    local path=$SANDBOX_DIR/systest-$$
    local OPTS="-D $path/db -T AD -S $path/stobs\
    -A linuxstob:$path/addb-stobs -e lnet:$M0D2_ENDPOINT -c $CONF_FILE\
    -m $MAX_RPC_MSG_SIZE -q $TM_MIN_RECV_QUEUE_LEN -w 3 -P $PROF_OPT\
    -f $PROC_FID2 -d $CONF_DISKS -H $M0D2_ENDPOINT"

    cd $path

    echo $M0_SRC_DIR/utils/mkfs/m0mkfs $OPTS
    $M0_SRC_DIR/utils/mkfs/m0mkfs $OPTS >>$path/mkfs.log ||
    error 'm0mkfs failed'
    echo $M0_SRC_DIR/mero/m0d $OPTS
    $M0_SRC_DIR/mero/m0d $OPTS >>$path/m0d.log 2>&1 &
    local PID=$!
    sleep 10
    kill -0 $PID 2>/dev/null ||
    error "Failed to start m0d. See $path/m0d.log for details."
}

export_vars() {
    export M0_SPIEL_OPTS="
    -l $M0_SRC_DIR/mero/.libs/libmero.so --client $SPIEL_ENDPOINT"

    export FIDS_LIST="
fids = {'profile'       : Fid(0x7000000000000001, 0),
        'fs'            : Fid(0x6600000000000001, 1),
        'node'          : Fid(0x6e00000000000001, 2),
        'pool'          : Fid(0x6f00000000000001, 9),
        'mdpool'        : Fid(0x6f00000000000001, 10),
        'rack'          : Fid(0x6100000000000001, 6),
        'encl'          : Fid(0x6500000000000001, 7),
        'ctrl'          : Fid(0x6300000000000001, 8),
        'disk0'         : Fid(0x6b00000000000001, 2),
        'disk1'         : Fid(0x6b00000000000001, 3),
        'disk2'         : Fid(0x6b00000000000001, 4),
        'disk3'         : Fid(0x6b00000000000001, 5),
        'disk4'         : Fid(0x6b00000000000001, 6),
        'pver'          : Fid(0x7600000000000001, 10),
        'mdpver'        : Fid(0x7600000000000001, 11),
        'pver_f'        : Fid(0x7640000000000001, 11),
        'rackv'         : Fid(0x6a00000000000001, 2),
        'enclv'         : Fid(0x6a00000000000001, 3),
        'ctrlv'         : Fid(0x6a00000000000001, 4),
        'diskv0'        : Fid(0x6a00000000000001, 5),
        'diskv1'        : Fid(0x6a00000000000001, 6),
        'diskv2'        : Fid(0x6a00000000000001, 7),
        'diskv3'        : Fid(0x6a00000000000001, 8),
        'diskv4'        : Fid(0x6a00000000000001, 9),
        'mdrackv'       : Fid(0x6a00000000000001, 20),
        'mdenclv'       : Fid(0x6a00000000000001, 21),
        'mdctrlv'       : Fid(0x6a00000000000001, 22),
	'mddiskv'       : Fid(0x6a00000000000001, 23),
        'process'       : Fid($PROC_FID_CNTR, $PROC_FID_KEY),
        'process2'      : Fid($PROC_FID_CNTR, $PROC_FID_KEY2),
        'process1'      : Fid(0x7200000000000001, 1),
        'ios'           : Fid(0x7300000000000002, 0),
        'mds'           : Fid(0x7300000000000002, 2),
        'mds2'          : Fid(0x7300000000000002, 3),
        'addb2'         : Fid(0x7300000000000002, 5),
        'sns_repair'    : Fid(0x7300000000000002, 6),
        'sns_rebalance' : Fid(0x7300000000000002, 7),
        'confd'         : Fid(0x7300000000000002, 8),
        'confd2'        : Fid(0x7300000000000002, 9),
        'sdev0'         : Fid(0x6400000000000009, 0),
        'sdev1'         : Fid(0x6400000000000009, 1),
        'sdev2'         : Fid(0x6400000000000009, 2),
        'sdev3'         : Fid(0x6400000000000009, 3),
        'sdev4'         : Fid(0x6400000000000009, 4),
        'rms'           : Fid(0x7300000000000004, 0),
        'rms2'          : Fid(0x7300000000000004, 1),
        'rms3'          : Fid(0x7300000000000004, 2),
        'ha'            : Fid(0x7300000000000004, 4)
}
"
    export SERVICES="
services = ['ios', 'mds2', 'sns_repair', 'sns_rebalance']
"
### enum of service health statuses. see m0_service_health in reqh/reqh_service.h
    export HEALTH="
HEALTH_GOOD, HEALTH_BAD, HEALTH_INACTIVE, HEALTH_UNKNOWN = range(4)
"
}

construct_db() {
    $M0_SRC_DIR/utils/spiel/m0spiel $M0_SPIEL_OPTS <<EOF
$FIDS_LIST
N, K, P = 2, 1, 4
mask = c_uint64(3)
cores = Bitmap(1, pointer(mask))

if spiel.cmd_profile_set(str(fids['profile'])) != 0:
    sys.exit('cannot set profile {0}'.format(fids['profile']))

tx = SpielTx(spiel.spiel)
spiel.tx_open(tx)

commands = [
    ('profile_add', tx, fids['profile']),
    ('filesystem_add', tx, fids['fs'], fids['profile'], 10, fids['profile'],
     fids['mdpool'], Fid(0, 0), ['{0} {1} {2}'.format(P, N, K)]),
    ('pool_add', tx, fids['pool'], fids['fs'], 2),
    ('rack_add', tx, fids['rack'], fids['fs']),
    ('enclosure_add', tx, fids['encl'], fids['rack']),
    ('node_add', tx, fids['node'], fids['fs'], 256L, 2, 10L, 0xff00ff00L,
     fids['pool']),
    ('controller_add', tx, fids['ctrl'], fids['encl'], fids['node']),
    ('disk_add', tx, fids['disk0'], fids['ctrl']),
    ('disk_add', tx, fids['disk1'], fids['ctrl']),
    ('disk_add', tx, fids['disk2'], fids['ctrl']),
    ('disk_add', tx, fids['disk3'], fids['ctrl']),
    ('disk_add', tx, fids['disk4'], fids['ctrl']),
    ('pver_actual_add', tx, fids['pver'], fids['pool'], [0, 0, 0, 0, 1],
     PdclustAttr(N, K, P, 1024*1024, Fid(1, 2))),
    ('rack_v_add', tx, fids['rackv'], fids['pver'], fids['rack']),
    ('enclosure_v_add', tx, fids['enclv'], fids['rackv'], fids['encl']),
    ('controller_v_add', tx, fids['ctrlv'], fids['enclv'], fids['ctrl']),
    ('disk_v_add', tx, fids['diskv1'], fids['ctrlv'], fids['disk1']),
    ('disk_v_add', tx, fids['diskv2'], fids['ctrlv'], fids['disk2']),
    ('disk_v_add', tx, fids['diskv3'], fids['ctrlv'], fids['disk3']),
    ('disk_v_add', tx, fids['diskv4'], fids['ctrlv'], fids['disk4']),
    ('pool_add', tx, fids['mdpool'], fids['fs'], 2),
    ('pver_actual_add', tx, fids['mdpver'], fids['mdpool'], [0, 0, 0, 0, 1],
     PdclustAttr(1, 0, 1, 1024*1024, Fid(1, 2))),
    ('rack_v_add', tx, fids['mdrackv'], fids['mdpver'], fids['rack']),
    ('enclosure_v_add', tx, fids['mdenclv'], fids['mdrackv'], fids['encl']),
    ('controller_v_add', tx, fids['mdctrlv'], fids['mdenclv'], fids['ctrl']),
    ('disk_v_add', tx, fids['mddiskv'], fids['mdctrlv'], fids['disk1']),
    ('pool_version_done', tx, fids['pver']),
    ('process_add', tx, fids['process'], fids['node'], cores, 0L, 0L, 0L, 0L,
     '$M0D1_ENDPOINT'),
    ('process_add', tx, fids['process1'], fids['node'], cores, 0L, 0L, 0L, 0L,
     '$M0T1FS_ENDPOINT:1'),
    ('process_add', tx, fids['process2'], fids['node'], cores, 0L, 0L, 0L, 0L,
     '$M0D2_ENDPOINT'),
    ('service_add', tx, fids['confd'], fids['process'], M0_CST_MGS,
     ['$M0D1_ENDPOINT'], ServiceInfoParameters(confdb_path='$M0D1_ENDPOINT')),
    ('service_add', tx, fids['confd2'], fids['process2'], M0_CST_MGS,
     ['$M0D2_ENDPOINT'], ServiceInfoParameters(confdb_path='$M0D2_ENDPOINT')),
    ('service_add', tx, fids['rms'], fids['process'], M0_CST_RMS,
     ['$M0D1_ENDPOINT'], ServiceInfoParameters()),
    ('service_add', tx, fids['rms2'], fids['process2'], M0_CST_RMS,
     ['$M0D2_ENDPOINT'], ServiceInfoParameters()),
    ('service_add', tx, fids['ha'], fids['process2'], M0_CST_HA,
     ['$M0D2_ENDPOINT'], ServiceInfoParameters()),
    ('service_add', tx, fids['ios'], fids['process2'], M0_CST_IOS,
     ['$M0D2_ENDPOINT'], ServiceInfoParameters()),
    ('service_add', tx, fids['sns_repair'], fids['process2'], M0_CST_SNS_REP,
     ['$M0D2_ENDPOINT'], ServiceInfoParameters()),
    ('service_add', tx, fids['addb2'], fids['process2'], M0_CST_ADDB2,
     ['$M0D2_ENDPOINT'], ServiceInfoParameters()),
    ('service_add', tx, fids['sns_rebalance'], fids['process2'], M0_CST_SNS_REB,
     ['$M0D2_ENDPOINT'], ServiceInfoParameters()),
    ('service_add', tx, fids['mds'], fids['process'], M0_CST_MDS,
     ['$M0D1_ENDPOINT'], ServiceInfoParameters()),
    ('service_add', tx, fids['mds2'], fids['process2'], M0_CST_MDS,
     ['$M0D2_ENDPOINT'], ServiceInfoParameters()),
    ('service_add', tx, fids['rms3'], fids['process1'], M0_CST_RMS,
     ['$M0T1FS_ENDPOINT:1'], ServiceInfoParameters()),
    ('device_add', tx, fids['sdev0'], fids['mds2'], fids['disk0'], 1,
     M0_CFG_DEVICE_INTERFACE_SCSI, M0_CFG_DEVICE_MEDIA_SSD, 1024,
     $((2 * DEV_SIZE))L, 123L, 0x55L, 'dev/loop0'),
    ('device_add', tx, fids['sdev1'], fids['ios'], fids['disk1'], 1,
     M0_CFG_DEVICE_INTERFACE_SCSI, M0_CFG_DEVICE_MEDIA_SSD, 1024,
     $((2 * DEV_SIZE))L, 123L, 0x55L, 'dev/loop1'),
    ('device_add', tx, fids['sdev2'], fids['ios'], fids['disk2'], 2,
     M0_CFG_DEVICE_INTERFACE_SCSI, M0_CFG_DEVICE_MEDIA_SSD, 1024,
     $((2 * DEV_SIZE))L, 123L, 0x55L, 'dev/loop2'),
    ('device_add', tx, fids['sdev3'], fids['ios'], fids['disk3'], 3,
     M0_CFG_DEVICE_INTERFACE_SCSI, M0_CFG_DEVICE_MEDIA_SSD, 1024,
     $((2 * DEV_SIZE))L, 123L, 0x55L, 'dev/loop3'),
    ('device_add', tx, fids['sdev4'], fids['ios'], fids['disk4'], 0,
     M0_CFG_DEVICE_INTERFACE_SCSI, M0_CFG_DEVICE_MEDIA_SSD, 1024,
     $((2 * DEV_SIZE))L, 123L, 0x55L, 'dev/loop4'),
    ('tx_commit', tx)
]

for cmd in commands:
    try:
        rc = getattr(spiel, cmd[0])(*cmd[1:])
        if rc != 0:
            sys.exit('error {0} while {1} executing'.format(rc, cmd[0]))
    except:
        sys.exit('an error occurred while {0} executing: {1}'.format(
            cmd[0], sys.exc_info()[0]))

spiel.tx_close(tx)
EOF
}

validate_health() {
    say 'Validate health'
    $M0_SRC_DIR/utils/spiel/m0spiel $M0_SPIEL_OPTS <<EOF
$FIDS_LIST
$SERVICES
$HEALTH
$PYTHON_BOILERPLATE

for key in services:
    rc = spiel.service_health(fids[key])
    if rc not in [HEALTH_GOOD, HEALTH_UNKNOWN]:
        sys.exit('an error occurred while checking health of {0} service with\
              fid {1}, result code = {2}'.format(key, fids[key], rc))

rc = spiel.process_health(fids['process'])
if rc not in [HEALTH_GOOD, HEALTH_UNKNOWN]:
    sys.exit('an error occurred while checking health of process wwith fid {0},\
             result code {1}'.format(fids[process], rc))

spiel.rconfc_stop()
EOF
}

restart_services() {
    $M0_SRC_DIR/utils/spiel/m0spiel $M0_SPIEL_OPTS <<EOF
$FIDS_LIST
$SERVICES

from itertools import product

$PYTHON_BOILERPLATE

service_commands = list(product(['service_quiesce', 'service_stop',
                                 'service_init', 'service_start'],
                                [fids[x] for x in services]))
for command in service_commands:
    try:
        if getattr(spiel, command[0])(*command[1:]) != 0:
            sys.exit("an error occurred while {0} executing, service fid\
                     {1}".format(command[0], command[1]))
    except:
        sys.exit("an error occurred while {0} executing, service fid \
                 {1}".format(command[0], command[1]))

spiel.rconfc_stop()
EOF
}

reconfig_process() {
    $M0_SRC_DIR/utils/spiel/m0spiel $M0_SPIEL_OPTS <<EOF
$FIDS_LIST
$PYTHON_BOILERPLATE

rc = spiel.process_reconfig(fids['process2'])
if rc != 0:
    sys.exit('Error: process reconfig for process {0}\
             (status {1})'.format(fids['process2'], rc))

spiel.rconfc_stop()
EOF
}

perform_io() {
    local TEST_STR="Hello world"
    local TEST_FILE=$SANDBOX_DIR/mnt/file.txt

    ls $SANDBOX_DIR/mnt
    touch $TEST_FILE || die "m0t1fs: Can't touch file"
    setfattr -n lid -v 5 $TEST_FILE || die "m0t1fs: Can't set an attribute"
    dd if=/dev/zero of=$TEST_FILE bs=1M count=10
    echo $TEST_STR > $TEST_FILE || die "m0t1fs: Can't write to file"
    [ "`cat $TEST_FILE`" == "$TEST_STR" ] || die "IO error"
}

fs_stats_fetch() {
    $M0_SRC_DIR/utils/spiel/m0spiel $M0_SPIEL_OPTS <<EOF
$FIDS_LIST
$PYTHON_BOILERPLATE

fs_stats = FsStats()
rc = spiel.filesystem_stats_fetch(fids['fs'], fs_stats)
if rc != 0:
	sys.exit('Error: filesystem stats fetch for fs {0}\
                 (status {1})'.format(fids['fs'], rc))

spiel.rconfc_stop()
print("  free space       {0:>20}".format(fs_stats.fs_free_disk))
print("  total space      {0:>20}".format(fs_stats.fs_total_disk))
print("  services total   {0:>20}".format(fs_stats.fs_svc_total))
print("  services replied {0:>20}".format(fs_stats.fs_svc_replied))
EOF
}

_mount() {
    local MOUNT_OPTS="-t m0t1fs -o pfid=<0x7200000000000001:1>,profile=$PROF_OPT,ha=$M0D2_ENDPOINT \
none $SANDBOX_DIR/mnt"
    echo "mount $MOUNT_OPTS"
    mount $MOUNT_OPTS || return $?
}

device_commands_check() {
    $M0_SRC_DIR/utils/spiel/m0spiel $M0_SPIEL_OPTS <<EOF
$FIDS_LIST
$PYTHON_BOILERPLATE

device_commands = [('device_detach', fids['disk1']),
                   ('device_format', fids['disk1']),
                   ('device_attach', fids['disk1'])]
for command in device_commands:
    try:
        if getattr(spiel, command[0])(*command[1:]) != 0:
            sys.exit("an error occurred while {0} executing, device fid\
                     {1}".format(command[0], command[1]))
    except:
        sys.exit("an error occurred while {0} executing, device fid \
                 {1}".format(command[0], command[1]))

spiel.rconfc_stop()
EOF
}

## Keep the audience engaged.
say() { echo "$@" | tee -a $SANDBOX_DIR/confd/m0d.log; }

usage() {
    cat <<EOF
Usage: ${0##*/} [COMMAND]

Supported commands:
  run      run system tests (default command)
  insmod   insert Mero kernel modules: m0mero.ko, m0gf.ko
  rmmod    remove Mero kernel modules
  sstart   start Mero user-space services
  sstop    stop Mero user-space services
  help     display this help and exit
EOF
}

## -------------------------------------------------------------------
## main()
## -------------------------------------------------------------------

[ `id -u` -eq 0 ] || die 'Must be run by superuser'

case "${1:-}" in
    run|'') ;;
    insmod) lnet_up; m0_modules_insert; exit;;
    rmmod) m0_modules_remove; exit;;
    sstart) start; exit;;
    sstop) stop; exit;;
    help) usage; exit;;
    *) usage >&2; die;;
esac

trap stop EXIT

echo "Test start"
start

echo 8 >/proc/sys/kernel/printk  # Print kernel messages to the console.

export_vars

say "Construct confc db"
construct_db || stop

say "Test m0d start"
test_m0d_start || stop
validate_health || stop

say "Fetch filesystem stats"
fs_stats_fetch || stop

say "Restart services"
restart_services || stop
validate_health || stop

say "Reconfig Process"
reconfig_process || stop

say "Wait for reconfigure"
sleep 10
grep -q "Restarting" $SANDBOX_DIR/systest-$$/m0d.log ||
	die "Reconfigure is not finished"
validate_health || stop

_mount || stop $?

say "Perform IO"
perform_io || stop
validate_health || stop

say "Device commands"
device_commands_check || stop

say "Stop"
stop
report_and_exit spiel $?
