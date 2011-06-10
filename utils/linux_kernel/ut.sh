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
         utils/linux_kernel/kutc2.ko"

tailseek=$(( $(stat -c %s /var/log/kern) + 1 ))

# currently, kernel UT runs as part of loading kutc2 module
modload
modunload

tail -c+$tailseek /var/log/kern
