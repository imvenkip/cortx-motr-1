COLIBRI_CORE_ROOT=`dirname $0`/../../..
COLIBRI_C2T1FS_MOUNT_DIR=/tmp/test_c2t1fs_`date +"%d-%m-%Y_%T"`
COLIBRI_C2T1FS_TEST_DIR=/tmp/test_c2t1fs_$$
COLIBRI_IOSERVICE_ENDPOINT="172.16.220.181:23124:1"
COLIBRI_C2T1FS_ENDPOINT="172.16.220.180:23124:1"
COLIBRI_MODULE=$COLIBRI_CORE_ROOT/build_kernel_modules/kcolibri.ko
COLIBRI_MODULE_TRACE_MASK=0x00
COLIBRI_GALOIS_MODULE=$COLIBRI_CORE_ROOT/../galois/src/linux_kernel/kgalois.ko
COLIBRI_TEST_LOGFILE=`pwd`/bulkio_`date +"%Y-%m-%d_%T"`.log
POOL_WIDTH=3
TM_MIN_RECV_QUEUE_LEN=2
MAX_RPC_MSG_SIZE=0

prepare_testdir()
{
        stob_domain=$COLIBRI_STOB_DOMAIN
        db_path=$COLIBRI_DB_PATH

        echo "Cleaning up test directory ..."
        rm -rf $COLIBRI_C2T1FS_TEST_DIR  &> /dev/null

        echo "Creating test directory ..."
        mkdir $COLIBRI_C2T1FS_TEST_DIR &> /dev/null

        if [ $? -ne "0" ]
        then
                echo "Failed to create test directory."
                return 1
        fi

        return 0
}
