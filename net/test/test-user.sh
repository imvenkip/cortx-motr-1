#! /bin/sh

# Small wrapper to run user-space UT, which depends on kcolibri module

if [ "$(id -u)" -ne 0 ]; then
    echo "Must be run as root"
    exit 1
fi

. /work/git/colibri-net-test/core/c2t1fs/linux_kernel/st/common.sh

unload_all() {
    modunload
    modunload_galois
}
trap unload_all EXIT

modprobe_lnet
modload_galois
modload || exit $?

/work/git/colibri-net-test/core/net/test/net_test_console "$@"
