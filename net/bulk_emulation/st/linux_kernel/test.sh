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

. c2t1fs/st/common.sh

MODLIST="lib/linux_kernel/klibc2.ko \
         addb/linux_kernel/kaddb.ko \
         fop/linux_kernel/kfop.ko \
         net/linux_kernel/knetc2.ko"

tailseek=$(( $(stat -c %s /var/log/kern) + 1 ))

modload
# insert ST module separately to pass parameters
insmod net/bulk_emulation/st/linux_kernel/knetst.ko verbose passive_size=56000

# use bulkping client here and run various tests
net/bulk_emulation/st/bulkping -c -t bulk-sunrpc -v
net/bulk_emulation/st/bulkping -c -t bulk-sunrpc -v -n 8 -d 56000

rmmod knetst
modunload

sleep 1
tail -c+$tailseek /var/log/kern
