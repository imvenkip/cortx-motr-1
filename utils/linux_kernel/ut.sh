#! /bin/sh

# Small wrapper to run kernel UT

case "`id -u`" in
0) ;;
*) echo "Must be run as root"
   exit 1
   ;;
esac

d="`git rev-parse --show-cdup`"
case "x$d" in
x) ;;
*) cd "$d"
   ;;
esac

. c2t1fs/st/common.sh

MODLIST="lib/linux_kernel/klibc2.ko \
         utils/linux_kernel/kutc2.ko"

sorig=`stat -c %s /var/log/kern`

# currently, kernel UT runs as part of loading kutc2 module
modload

sdone=`stat -c %s /var/log/kern`
stail=$((sdone - sorig))
case "$stail" in
0)  echo "No output found in /var/log/kern"
    ;;
*)  tail -c$stail /var/log/kern
    ;;
esac

modunload
