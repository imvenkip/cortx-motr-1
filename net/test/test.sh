#! /bin/sh
# see original file at core/utils/linux_kernel/ut.sh

# Small wrapper to run colibri network benchmark node module

if [ "$(id -u)" -ne 0 ]; then
    echo "Must be run as root"
    exit 1
fi

d="`git rev-parse --show-cdup`"
if [ -n "$d" ]; then
    cd "$d"
fi

. c2t1fs/linux_kernel/st/common.sh

MODLIST="build_kernel_modules/kcolibri.ko"
MODMAIN="net/test/linux_kernel/net_test_node.ko"

log='/var/log/kern'
if [ ! -e "$log" ]; then
    log='/var/log/messages'
fi
tailseek=$(( $(stat -c %s "$log") + 1 ))

# currently, kernel UT runs as part of loading kutc2 module
modload_galois
modload
insmod $MODMAIN $*
rmmod $MODMAIN
modunload
modunload_galois

tail -c+$tailseek "$log" | grep ' kernel: '
