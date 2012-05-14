#! /bin/sh

# Small wrapper to run kernel UT

if [ "$(id -u)" -ne 0 ]; then
    echo "Must be run as root"
    exit 1
fi

d="`git rev-parse --show-cdup`"
if [ -n "$d" ]; then
    cd "$d"
fi

. c2t1fs/linux_kernel/st/common.sh

MODLIST="build_kernel_modules/kcolibri.ko \
         utils/linux_kernel/kutc2.ko"

log='/var/log/kern'
if [ ! -e "$log" ]; then
    log='/var/log/messages'
fi
tailseek=$(( $(stat -c %s "$log") + 1 ))

modprobe_lnet
modload_galois
# currently, kernel UT runs as part of loading kutc2 module
modload
# LNet driver UT requires a user space helper
net/lnet/ut/lut_helper
modunload
modunload_galois

tail -c+$tailseek "$log" | grep ' kernel: '
