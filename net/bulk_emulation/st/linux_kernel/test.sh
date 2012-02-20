#! /bin/sh

# Small wrapper to run kernel bulkping sunrpc ST

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

log='/var/log/kern'
if [ ! -e "$log" ]; then
    log='/var/log/messages'
fi
tailseek=$(( $(stat -c %s "$log") + 1 ))

modload_galois
modload
# insert ST module separately to pass parameters
insmod net/bulk_emulation/st/linux_kernel/knetst.ko verbose passive_size=56000

# use bulkping client here and run various tests
net/bulk_emulation/st/bulkping -c -t bulk-sunrpc -v
net/bulk_emulation/st/bulkping -c -t bulk-sunrpc -v -n 8 -d 56000

rmmod knetst
modunload
modunload_galois

sleep 1
tail -c+$tailseek "$log" | grep ' kernel: '
