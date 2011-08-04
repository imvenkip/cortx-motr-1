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

. c2t1fs/st/common.sh

MODLIST="lib/linux_kernel/klibc2.ko \
         addb/linux_kernel/kaddb.ko \
         fop/linux_kernel/kfop.ko \
         net/linux_kernel/knetc2.ko \
         utils/linux_kernel/kutc2.ko"

log='/var/log/kern'
if [ ! -e "$log" ]; then
    log='/var/log/messages'
fi
tailseek=$(( $(stat -c %s "$log") + 1 ))

# currently, kernel UT runs as part of loading kutc2 module
modload
modunload

tail -c+$tailseek "$log" | grep ' kernel: '
