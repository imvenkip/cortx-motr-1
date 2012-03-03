#! /bin/sh

MODLIST="build_kernel_modules/kcolibri.ko"

abort()
{
    msg="$1"
    echo "$1 Aborting."
    exit 1
}

modload_galois()
{
    BASE=/root/Colibri-code/mgmt-ops/colibri-milestone/colibri-chk/core/../galois
    if test "x$BASE" = "x"; then
        modprobe galois
    else
        insmod $BASE/src/linux_kernel/galois.ko
    fi
}

modunload_galois()
{
    rmmod galois
}

modload()
{
    for m in $MODLIST ;do
	insmod $m                   || abort "Error loading $m."
    done
}

modunload()
{
    for m in $MODLIST ;do
	echo $m
    done | tac | while read ;do
	rmmod $REPLY                || echo "Error unloading $m."
    done
}
