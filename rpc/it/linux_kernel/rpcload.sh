#! /bin/sh

MODLIST="build_kernel_modules/m0mero.ko"

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

