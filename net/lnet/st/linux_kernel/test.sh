#! /bin/sh

# Small wrapper to run kernel lnetping ST

if [ "$(id -u)" -ne 0 ]; then
    echo "Must be run as root"
    exit 1
fi

d="`git rev-parse --show-cdup`"
if [ -n "$d" ]; then
    cd "$d"
fi

modprobe lnet
if [ $? -ne 0 ]; then
    echo "The lnet module is not loaded"
    exit 1
fi
lctl network up
if [ $? -ne 0 ] ; then
    echo "LNet network not enabled"
    exit 1
fi

# use the first NID configured
NID=`lctl list_nids | head -n 1`
if [ -z "$NID" ] ; then
    echo "No networks available"
    exit 1
fi

. c2t1fs/linux_kernel/st/common.sh

MODLIST="build_kernel_modules/kcolibri.ko"

log='/var/log/kern'
if [ ! -e "$log" ]; then
    log='/var/log/messages'
fi
tailseek=$(( $(stat -c %s "$log") + 1 ))

# Server parameters
SPARM="server_only server_network=$NID"

# Client parameters
CPARM="client_only client_network=$NID loops=2"

modload_galois
modload

# insert ST module separately to pass parameters
STMOD=klnetst
insmod net/lnet/st/linux_kernel/$STMOD.ko verbose passive_size=30720 $SPARM $CPARM

rmmod $STMOD
modunload
modunload_galois

sleep 1
tail -c+$tailseek "$log" | grep ' kernel: '
