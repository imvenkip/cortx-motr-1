set -x

. common.sh
. fs_common.sh

cd ../..
pwd
ulimit -c unlimited

fsmount

echo ======================done=====================
