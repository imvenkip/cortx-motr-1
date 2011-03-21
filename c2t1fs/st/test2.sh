# Using simple layout N/K = 3/1 (with 1 parity and 1 spare unit).

set -x

. common.sh
. fs_common.sh

cd ../..
pwd
ulimit -c unlimited

# 1. mount
fsmount

# 2. test read/write
#arg0 max_count_=3
#arg1 max_bs_=49152
fsrwtest 3 49152

# 3. umount
fsumount

echo ======================done=====================
