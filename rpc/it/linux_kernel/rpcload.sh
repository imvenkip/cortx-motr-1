#! /bin/sh

MODLIST="lib/linux_kernel/klibc2.ko \
         addb/linux_kernel/kaddb.ko \
         db/linux_kernel/kdb.ko \
         fid/linux_kernel/kfid.ko \
         fol/linux_kernel/kfol.ko \
         fop/linux_kernel/kfop.ko \
         net/linux_kernel/knetc2.ko \
	 sm/linux_kernel/ksm.ko \
         stob/linux_kernel/kstob.ko \
	 cob/linux_kernel/kcob.ko \
	 dtm/linux_kernel/kdtm.ko \
	 xcode/linux_kernel/kxcode.ko \
	 rpc/linux_kernel/krpc.ko"

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
	rmmod $REPLY                || echo "Error unloading $m."
    done
}

