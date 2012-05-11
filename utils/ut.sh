#! /bin/sh

# Small wrapper to run user-space UT, which depends on kcolibri module

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

unload_all() {
    modunload
    modunload_galois
}
trap unload_all EXIT

modprobe_lnet
modload_galois
modload || exit $?

utils/ut "$@"
