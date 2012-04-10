COLIBRI_CORE_ROOT=`dirname $0`/../../..
COLIBRI_C2T1FS_MOUNT_DIR=/tmp/test_c2t1fs_`date +"%d-%m-%Y_%T"`
COLIBRI_C2T1FS_TEST_DIR=/tmp/test_c2t1fs_$$
COLIBRI_IOSERVICE_ENDPOINT="172.16.220.181:23124:1"
COLIBRI_C2T1FS_ENDPOINT="172.16.220.180:23124:1"
COLIBRI_MODULE=$COLIBRI_CORE_ROOT/build_kernel_modules/kcolibri.ko
COLIBRI_GALOIS_MODULE=$COLIBRI_CORE_ROOT/../galois/src/linux_kernel/kgalois.ko
COLIBRI_TEST_LOGFILE=`pwd`/bulkio_`date +"%d-%m-%Y_%T"`.log
POOL_WIDTH=3


