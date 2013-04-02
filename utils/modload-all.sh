#! /bin/sh

if [ "$(id -u)" -ne 0 ]; then
    echo "Must be run as root"
    exit 1
fi

# Assign a node uuid for the user space UT (addb/ut/addb_ut.c)
NODE_UUID="abcdef01-2345-6789-0123-456789ABCDEF"

. /home/nikita/p/m/m0t1fs/linux_kernel/st/common.sh

unload_all() {
    modunload
    modunload_galois
}
trap unload_all EXIT

modprobe_lnet
modload_galois
modload || exit $?

