#!/bin/sh
set -e
### ------------------------------------------------------------------
### The overview of configuration caching demo is available at
### http://goo.gl/YnrWQ
### ------------------------------------------------------------------

CONF_DB=_db
CONF_CFG=../ut/conf_xc.txt
CONF_KDIR=../../m0t1fs/linux_kernel/st/

error() { echo "$@" >&2; exit 1; }

[ `id -u` -eq 0 ] || error 'Must be run as root'

which dot >/dev/null 2>/dev/null || error 'graphviz package is not installed'

for x in ./objx2db ./confc_test ./confd_test; do
    [ -x $x ] || error "$x: No such executable"
done

[ -r $CONF_CFG ] || error "$CONF_CFG: No such file"

echo -n 'Cleanup ... '
rm -rf $CONF_DB $CONF_DB.errlog $CONF_DB.msglog
rm -f *.dot *.png
echo done.

[ "$1" = "clean" ] && exit

echo -n 'Generating configuration DB ... '
./objx2db -b $CONF_DB -c $CONF_CFG || error 'DB creation failed'
echo done.

echo -n 'confc-user test ... '
./confc_test -c $CONF_CFG >confc_u.dot || error 'confc-user test failed'
echo done.

echo -n 'confd test ... '
./confd_test -b $CONF_DB >confd.dot || error 'confd test failed'
echo done.

echo 'confc-kernel test ... '
dmesg -c >/dev/null
LOCAL_CONF=",local-conf=$(tr -d ' \n' < ${CONF_CFG} | tr , ^)"
cd $CONF_KDIR
./m0t1fs_conf.sh $LOCAL_CONF
cd -
dmesg | grep @@@CONF@@@ >confc_k.dot
echo done.

echo -n 'Running dot ... '
for f in *.dot; do
    sed 's/@@@CONF@@@//g' $f | dot -Tpng -o${f%.dot}.png
done
echo done.
