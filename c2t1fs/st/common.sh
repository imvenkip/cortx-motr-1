#! /bin/sh

MODLIST="lib/linux_kernel/klibc2.ko \
         addb/linux_kernel/kaddb.ko \
         db/linux_kernel/kdb.ko \
         fop/linux_kernel/kfop.ko \
         net/ksunrpc/ksunrpc.ko \
         c2t1fs/c2t1fs.ko \
         c2t1fs/c2t1fs_loop.ko"

abort()
{
    msg="$1"
    echo "$1 Aborting."
    exit 1
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
	rmmod $REPLY                || abort "Error unloading $m."
    done
}