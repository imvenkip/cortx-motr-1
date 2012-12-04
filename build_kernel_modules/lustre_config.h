/* config.h.  Generated from config.h.in by configure.  */
/* config.h.in.  Generated from configure.ac by autoheader.  */

#pragma once

#ifndef __MERO_LUSTRE_CONFIG_H__
#define __MERO_LUSTRE_CONFIG_H__

/* disable libcfs CDEBUG, CWARN */
#define CDEBUG_ENABLED 1

/* disable libcfs ENTRY/EXIT */
#define CDEBUG_ENTRY_EXIT 1

/* enable posix acls for ldiskfs */
#define CONFIG_LDISKFSDEV_FS_POSIX_ACL 1

/* enable fs security for ldiskfs */
#define CONFIG_LDISKFSDEV_FS_SECURITY 1

/* enable extented attributes for ldiskfs */
#define CONFIG_LDISKFSDEV_FS_XATTR 1

/* build ldiskfs as a module */
#define CONFIG_LDISKFS_FS_MODULE 1

/* enable posix acls for ldiskfs */
#define CONFIG_LDISKFS_FS_POSIX_ACL 1

/* enable fs security for ldiskfs */
#define CONFIG_LDISKFS_FS_SECURITY 1

/* enable extended attributes for ldiskfs */
#define CONFIG_LDISKFS_FS_XATTR 1

/* kernel has cpu affinity support */
/* #undef CPU_AFFINITY */

/* Enable Cray XT3 Features */
/* #undef CRAY_XT3 */

/* name of ldiskfs debug program */
#define DEBUGFS "debugfs"

/* Enable DMU OSD */
/* #undef DMU_OSD */

/* name of ldiskfs dump program */
#define DUMPE2FS "dumpe2fs"

/* name of ldiskfs fsck program */
#define E2FSCK "e2fsck"

/* name of ldiskfs e2fsprogs package */
#define E2FSPROGS "e2fsprogs"

/* name of ldiskfs label program */
#define E2LABEL "e2label"

/* do data checksums */
#define ENABLE_CHECKSUM 1

/* Liblustre Can Recover */
#define ENABLE_LIBLUSTRE_RECOVERY 1

/* Use the Pinger */
#define ENABLE_PINGER 1

/* ext4_ext_insert_exent needs 5 arguments */
#define EXT_INSERT_EXTENT_WITH_5ARGS 1

/* register_sysctl_table want 2 args */
/* #undef HAVE_2ARGS_REGISTER_SYSCTL */

/* INIT_WORK use 3 args and store data inside */
/* #undef HAVE_3ARGS_INIT_WORK */

/* sysctl proc_handler wants 5 args */
#define HAVE_5ARGS_SYSCTL_PROC_HANDLER 1

/* add_to_page_cache_lru functions are present */
#define HAVE_ADD_TO_PAGE_CACHE_LRU 1

/* support alder32 checksum type */
#define HAVE_ADLER 1

/* Define to 1 if you have the <arpa/inet.h> header file. */
#define HAVE_ARPA_INET_H 1

/* Define to 1 if you have the <asm/types.h> header file. */
#define HAVE_ASM_TYPES_H 1

/* kernel has block cipher support */
#define HAVE_ASYNC_BLOCK_CIPHER 1

/* panic_notifier_list is atomic_notifier_head */
#define HAVE_ATOMIC_PANIC_NOTIFIER 1

/* bdi_init/bdi_destroy functions are present */
#define HAVE_BDI_INIT 1

/* bdi_register function is present */
#define HAVE_BDI_REGISTER 1

/* Enable BGL Features */
/* #undef HAVE_BGL_SUPPORT */

/* kernel has bio_endio with 2 args */
#define HAVE_BIO_ENDIO_2ARG 1

/* Kernel has bit_spinlock.h */
#define HAVE_BIT_SPINLOCK_H 1

/* struct bio has a bi_hw_segments field */
/* #undef HAVE_BI_HW_SEGMENTS */

/* blkdev_put needs 2 paramters */
#define HAVE_BLKDEV_PUT_2ARGS 1

/* Define to 1 if you have the <blkid/blkid.h> header file. */
#define HAVE_BLKID_BLKID_H 1

/* blk_queue_logical_block_size is defined */
#define HAVE_BLK_QUEUE_LOG_BLK_SIZE 1

/* blk_queue_max_sectors is defined */
/* #undef HAVE_BLK_QUEUE_MAX_SECTORS */

/* blk_queue_max_segments is defined */
#define HAVE_BLK_QUEUE_MAX_SEGMENTS 1

/* call_rcu takes three parameters */
/* #undef HAVE_CALL_RCU_PARAM */

/* kernel has cancel_dirty_page instead of clear_page_dirty */
#define HAVE_CANCEL_DIRTY_PAGE 1

/* kernel has third arg can_sleep in fs/locks.c: flock_lock_file_wait() */
/* #undef HAVE_CAN_SLEEP_ARG */

/* Define to 1 if you have the <catamount/data.h> header file. */
/* #undef HAVE_CATAMOUNT_DATA_H */

/* Define to 1 if you have the `connect' function. */
#define HAVE_CONNECT 1

/* inode_operations->follow_link returns a cookie */
#define HAVE_COOKIE_FOLLOW_LINK 1

/* cpu_online found */
#define HAVE_CPU_ONLINE 1

/* task's cred wrappers found */
#define HAVE_CRED_WRAPPERS 1

/* dentry_open needs 4 paramters */
#define HAVE_DENTRY_OPEN_4ARGS 1

/* dev_get_by_name has 2 args */
#define HAVE_DEV_GET_BY_NAME_2ARG 1

/* kernel has new dev_set_rdonly */
#define HAVE_DEV_SET_RDONLY 1

/* after 2.6.17 dquote use mutex instead if semaphore */
#define HAVE_DQUOTOFF_MUTEX 1

/* dump_trace is exported */
#define HAVE_DUMP_TRACE 1

/* dump_trace want address argument */
/* #undef HAVE_DUMP_TRACE_ADDRESS */

/* Kernel has d_add_unique */
#define HAVE_D_ADD_UNIQUE 1

/* d_obtain_alias exist in kernel */
#define HAVE_D_OBTAIN_ALIAS 1

/* d_rehash_cond is exported by the kernel */
/* #undef HAVE_D_REHASH_COND */

/* Define to 1 if you have the <endian.h> header file. */
#define HAVE_ENDIAN_H 1

/* exportfs_decode_fh has been export */
#define HAVE_EXPORTFS_DECODE_FH 1

/* inode_permission is exported by the kernel */
#define HAVE_EXPORT_INODE_PERMISSION 1

/* Define to 1 if you have the <ext2fs/ext2fs.h> header file. */
/* #undef HAVE_EXT2FS_EXT2FS_H */

/* build ext4 based ldiskfs */
#define HAVE_EXT4_LDISKFS 1

/* kernel has .fh_to_dentry member in export_operations struct */
#define HAVE_FH_TO_DENTRY 1

/* filemap_fdatawrite_range is exported by the kernel */
#define HAVE_FILEMAP_FDATAWRITE_RANGE 1

/* Kernel exports filemap_populate */
/* #undef HAVE_FILEMAP_POPULATE */

/* Define to 1 if you have the <file.h> header file. */
/* #undef HAVE_FILE_H */

/* struct open_intent has a file field */
#define HAVE_FILE_IN_STRUCT_INTENT 1

/* use fops->readv */
/* #undef HAVE_FILE_READV */

/* kernel have file_remove_suid */
#define HAVE_FILE_REMOVE_SUID 1

/* use fops->writev */
/* #undef HAVE_FILE_WRITEV */

/* file_operations .flush method has an fl_owner_t id */
#define HAVE_FLUSH_OWNER_ID 1

/* kernel has fs/quotaio_v1.h */
/* #undef HAVE_FS_QUOTAIO_H */

/* kernel has fs/quota/quotaio_v2.h */
#define HAVE_FS_QUOTA_QUOTAIO_H 1

/* kernel has FS_RENAME_DOES_D_MOVE flag */
#define HAVE_FS_RENAME_DOES_D_MOVE 1

/* fs_struct use path structure */
#define HAVE_FS_STRUCT_USE_PATH 1

/* struct file_operations has flock field */
#define HAVE_F_OP_FLOCK 1

/* Define to 1 if you have the `gethostbyname' function. */
#define HAVE_GETHOSTBYNAME 1

/* Define to 1 if you have the `get_preemption_level' function. */
/* #undef HAVE_GET_PREEMPTION_LEVEL */

/* gfp_t found */
#define HAVE_GFP_T 1

/* kernel exports grab_cache_page_nowait_gfp */
/* #undef HAVE_GRAB_CACHE_PAGE_NOWAIT_GFP */

/* Define this if you enable gss */
/* #undef HAVE_GSS */

/* Define this if you enable gss keyring backend */
/* #undef HAVE_GSS_KEYRING */

/* Define this if the Kerberos GSS library supports gss_krb5_ccache_name */
/* #undef HAVE_GSS_KRB5_CCACHE_NAME */

/* Define this if you have Heimdal Kerberos libraries */
/* #undef HAVE_HEIMDAL */

/* Define to 1 if you have the `inet_ntoa' function. */
#define HAVE_INET_NTOA 1

/* kernel is support network namespaces */
#define HAVE_INIT_NET 1

/* struct inode has i_blksize field */
/* #undef HAVE_INODE_BLKSIZE */

/* struct inode has i_private field */
#define HAVE_INODE_IPRIVATE 1

/* after 2.6.15 inode have i_mutex intead of i_sem */
#define HAVE_INODE_I_MUTEX 1

/* inode_operations->permission has two args */
#define HAVE_INODE_PERMISION_2ARGS 1

/* Define to 1 if you have the <inttypes.h> header file. */
#define HAVE_INTTYPES_H 1

/* Define if return type of invalidatepage should be int */
/* #undef HAVE_INVALIDATEPAGE_RETURN_INT */

/* invalidate_bdev has second argument */
/* #undef HAVE_INVALIDATE_BDEV_2ARG */

/* exported invalidate_inode_pages */
/* #undef HAVE_INVALIDATE_INODE_PAGES */

/* exported invalidate_mapping_pages */
#define HAVE_INVALIDATE_MAPPING_PAGES 1

/* is_compat_task() is available */
#define HAVE_IS_COMPAT_TASK 1

/* kernel has .sendfile */
/* #undef HAVE_KERNEL_SENDFILE */

/* kernel has .slice_read */
#define HAVE_KERNEL_SPLICE_READ 1

/* kernel has .write_begin/end */
#define HAVE_KERNEL_WRITE_BEGIN_END 1

/* kernel __u64 is long long type */
#define HAVE_KERN__U64_LONG_LONG 1

/* kernel has struct kmem_cache */
#define HAVE_KMEM_CACHE 1

/* kmem_cache_create has dtor argument */
/* #undef HAVE_KMEM_CACHE_CREATE_DTOR */

/* kmem_cache_destroy(cachep) return int */
/* #undef HAVE_KMEM_CACHE_DESTROY_INT */

/* Define this if you have MIT Kerberos libraries */
/* #undef HAVE_KRB5 */

/* Define this if the function krb5_get_error_message is available */
/* #undef HAVE_KRB5_GET_ERROR_MESSAGE */

/* Define this if the function krb5_get_init_creds_opt_set_addressless is
   available */
/* #undef HAVE_KRB5_GET_INIT_CREDS_OPT_SET_ADDRESSLESS */

/* support alder32 checksum type */
#define HAVE_LDAP 1

/* Define to 1 if you have the <ldap.h> header file. */
#define HAVE_LDAP_H 1

/* enable use of ldiskfsprogs package */
/* #undef HAVE_LDISKFSPROGS */

/* ldiskfs/xattr.h found */
/* #undef HAVE_LDISKFS_XATTR_H */

/* use libcap */
/* #undef HAVE_LIBCAP */

/* libefence support is requested */
/* #undef HAVE_LIBEFENCE */

/* Define to 1 if you have the `keyutils' library (-lkeyutils). */
/* #undef HAVE_LIBKEYUTILS */

/* use libpthread */
#define HAVE_LIBPTHREAD 1

/* readline library is available */
#define HAVE_LIBREADLINE 1

/* libwrap support is requested */
/* #undef HAVE_LIBWRAP */

/* kernel has include/exportfs.h */
#define HAVE_LINUX_EXPORTFS_H 1

/* Kernel has fiemap.h */
#define HAVE_LINUX_FIEMAP_H 1

/* kernel has include/mm_types.h */
#define HAVE_LINUX_MMTYPES_H 1

/* kernel has include/oom.h */
#define HAVE_LINUX_OOM_H 1

/* linux/posix_acl_xattr.h found */
#define HAVE_LINUX_POSIX_ACL_XATTR_H 1

/* Define to 1 if you have the <linux/random.h> header file. */
#define HAVE_LINUX_RANDOM_H 1

/* Define to 1 if you have the <linux/types.h> header file. */
#define HAVE_LINUX_TYPES_H 1

/* Define to 1 if you have the <linux/unistd.h> header file. */
#define HAVE_LINUX_UNISTD_H 1

/* Define to 1 if you have the <linux/version.h> header file. */
#define HAVE_LINUX_VERSION_H 1

/* lock_map_acquire is defined */
#define HAVE_LOCK_MAP_ACQUIRE 1

/* Enable lru resize support */
#define HAVE_LRU_RESIZE_SUPPORT 1

/* Define this if the Kerberos GSS library supports
   gss_krb5_export_lucid_sec_context */
/* #undef HAVE_LUCID_CONTEXT_SUPPORT */

/* kernel have mapping_cap_writeback_dirty */
#define HAVE_MAPPING_CAP_WRITEBACK_DIRTY 1

/* Define to 1 if you have the <memory.h> header file. */
#define HAVE_MEMORY_H 1

/* kernel module loading is possible */
#define HAVE_MODULE_LOADING_SUPPORT 1

/* Define to 1 if you have the <netdb.h> header file. */
#define HAVE_NETDB_H 1

/* Define to 1 if you have the <netinet/in.h> header file. */
#define HAVE_NETINET_IN_H 1

/* Define to 1 if you have the <netinet/tcp.h> header file. */
#define HAVE_NETINET_TCP_H 1

/* net/netlink.h found */
#define HAVE_NETLINK 1

/* netlink_kernel_create want mutex for callback */
#define HAVE_NETLINK_CBMUTEX 1

/* nlmsg_new takes 2 args */
#define HAVE_NETLINK_NL2 1

/* netlink is support network namespace */
#define HAVE_NETLINK_NS 1

/* nlmsg_multicast needs 5 argument */
#define HAVE_NLMSG_MULTICAST_5ARGS 1

/* netlink brouacast is want to have gfp paramter */
#define HAVE_NL_BROADCAST_GFP 1

/* node_to_cpumask is exported by the kernel */
/* #undef HAVE_NODE_TO_CPUMASK */

/* is kernel export nr_pagecache */
/* #undef HAVE_NR_PAGECACHE */

/* has completion vector */
#define HAVE_OFED_IB_COMP_VECTOR 1

/* ib_dma_map_single defined */
#define HAVE_OFED_IB_DMA_MAP 1

/* has completion vector */
#define HAVE_OFED_RDMA_CMEV_ADDRCHANGE 1

/* has completion vector */
#define HAVE_OFED_RDMA_CMEV_TIMEWAIT_EXIT 1

/* rdma_set_reuse defined */
/* #undef HAVE_OFED_RDMA_SET_REUSEADDR */

/* has transport iWARP */
#define HAVE_OFED_TRANSPORT_IWARP 1

/* kernel store a oom parameters in signal struct */
#define HAVE_OOMADJ_IN_SIG 1

/* kernel has .pagevec_lru_add_file */
#define HAVE_PAGEVEC_LRU_ADD_FILE 1

/* does kernel have PageChecked and SetPageChecked */
#define HAVE_PAGE_CHECKED 1

/* kernel have PageConstant supported */
#define HAVE_PAGE_CONSTANT 1

/* percpu_counter_init has two arguments */
#define HAVE_PERCPU_2ND_ARG 1

/* percpu_counter found */
#define HAVE_PERCPU_COUNTER 1

/* is kernel have PG_fs_misc */
/* #undef HAVE_PG_FS_MISC */

/* readlink returns ssize_t */
#define HAVE_POSIX_1003_READLINK 1

/* kernel has deleted member in procfs entry struct */
/* #undef HAVE_PROCFS_DELETED */

/* kernel has pde_users member in procfs entry struct */
#define HAVE_PROCFS_USERS 1

/* have quota64 */
#define HAVE_QUOTA64 1

/* kernel has include/linux/quotaio_v2.h */
/* #undef HAVE_QUOTAIO_H */

/* quota_off needs 3 paramters */
#define HAVE_QUOTA_OFF_3ARGS 1

/* quota_on needs 5 paramters */
#define HAVE_QUOTA_ON_5ARGS 1

/* Enable quota support */
#define HAVE_QUOTA_SUPPORT 1

/* have RCU defined */
#define HAVE_RCU 1

/* super_operations has a read_inode */
/* #undef HAVE_READ_INODE_IN_SBOPS */

/* kernel has register_shrinker */
#define HAVE_REGISTER_SHRINKER 1

/* request_queue has a limits field */
#define HAVE_REQUEST_QUEUE_LIMITS 1

/* kernel has tree_lock as rw_lock */
/* #undef HAVE_RW_TREE_LOCK */

/* Kernel has a sb_any_quota_active */
#define HAVE_SB_ANY_QUOTA_ACTIVE 1

/* super_block has s_bdi field */
#define HAVE_SB_BDI 1

/* Kernel has a sb_has_quota_active */
#define HAVE_SB_HAS_QUOTA_ACTIVE 1

/* kernel has old get_sb_time_gran */
/* #undef HAVE_SB_TIME_GRAN */

/* struct scatterlist has page member */
#define HAVE_SCATTERLIST_SETPAGE 1

/* sched_show_task is exported */
/* #undef HAVE_SCHED_SHOW_TASK */

/* SLES10 SP2 use extra parameter in vfs */
/* #undef HAVE_SECURITY_PLUG */

/* semaphore counter is atomic */
/* #undef HAVE_SEM_COUNT_ATOMIC */

/* after 2.6.18 seq_file has lock intead of sem */
#define HAVE_SEQ_LOCK 1

/* support server */
#define HAVE_SERVER_SUPPORT 1

/* Define this if the Kerberos GSS library supports
   gss_krb5_set_allowable_enctypes */
/* #undef HAVE_SET_ALLOWABLE_ENCTYPES */

/* show_task is exported */
/* #undef HAVE_SHOW_TASK */

/* shrinker want self pointer in handler */
#define HAVE_SHRINKER_WANT_SHRINK_PTR 1

/* Define to 1 if you have the `socket' function. */
#define HAVE_SOCKET 1

/* sock_map_fd have second argument */
#define HAVE_SOCK_MAP_FD_2ARG 1

/* spinlock_t is defined */
/* #undef HAVE_SPINLOCK_T */

/* enable split support */
/* #undef HAVE_SPLIT_SUPPORT */

/* first parameter of vfs_statfs is dentry */
#define HAVE_STATFS_DENTRY_PARAM 1

/* struct statfs has a namelen field */
#define HAVE_STATFS_NAMELEN 1

/* Define to 1 if you have the <stdint.h> header file. */
#define HAVE_STDINT_H 1

/* Define to 1 if you have the <stdlib.h> header file. */
#define HAVE_STDLIB_H 1

/* Define to 1 if you have the <strings.h> header file. */
#define HAVE_STRINGS_H 1

/* Define to 1 if you have the <string.h> header file. */
#define HAVE_STRING_H 1

/* Define to 1 if you have the `strnlen' function. */
#define HAVE_STRNLEN 1

/* kernel has struct blkcipher_desc */
#define HAVE_STRUCT_BLKCIPHER_DESC 1

/* struct cred found */
#define HAVE_STRUCT_CRED 1

/* kernel has struct hash_desc */
#define HAVE_STRUCT_HASH_DESC 1

/* in 2.6.12 synchronize_rcu preferred over synchronize_kernel */
/* #undef HAVE_SYNCHRONIZE_RCU */

/* sysctl has CTL_UNNUMBERED */
#define HAVE_SYSCTL_UNNUMBERED 1

/* Define to 1 if you have the <sys/ioctl.h> header file. */
#define HAVE_SYS_IOCTL_H 1

/* Define to 1 if you have <sys/quota.h>. */
#define HAVE_SYS_QUOTA_H 1

/* Define to 1 if you have the <sys/stat.h> header file. */
#define HAVE_SYS_STAT_H 1

/* Define to 1 if you have the <sys/types.h> header file. */
#define HAVE_SYS_TYPES_H 1

/* Define to 1 if you have the <sys/user.h> header file. */
#define HAVE_SYS_USER_H 1

/* Define to 1 if you have the <sys/vfs.h> header file. */
#define HAVE_SYS_VFS_H 1

/* super block has s_time_gran member */
#define HAVE_S_TIME_GRAN 1

/* tasklist_lock exported */
/* #undef HAVE_TASKLIST_LOCK */

/* task_struct has rcu field */
#define HAVE_TASK_RCU 1

/* print_trace_address has reliable argument */
#define HAVE_TRACE_ADDRESS_RELIABLE 1

/* kernel export truncate_complete_page */
/* #undef HAVE_TRUNCATE_COMPLETE_PAGE */

/* kernel export truncate_inode_pages_range */
#define HAVE_TRUNCATE_RANGE 1

/* kernel uses trylock_page for page lock */
#define HAVE_TRYLOCK_PAGE 1

/* umode_t is defined */
#define HAVE_UMODE_T 1

/* Define umount_begin need second argument */
/* #undef HAVE_UMOUNTBEGIN_VFSMOUNT */

/* Define to 1 if you have the <unistd.h> header file. */
#define HAVE_UNISTD_H 1

/* unregister_blkdev return int */
/* #undef HAVE_UNREGISTER_BLKDEV_RETURN_INT */

/* unshare_fs_struct found */
#define HAVE_UNSHARE_FS_STRUCT 1

/* __u64 is long long type */
#define HAVE_USER__U64_LONG_LONG 1

/* vfs_dq_off is defined */
#define HAVE_VFS_DQ_OFF 1

/* vfs_kern_mount exist in kernel */
#define HAVE_VFS_KERN_MOUNT 1

/* if vfs_readdir need 64bit inode number */
#define HAVE_VFS_READDIR_U64_INO 1

/* vfs_symlink need 5 parameteres */
/* #undef HAVE_VFS_SYMLINK_5ARGS */

/* kernel has .fault in vm_operation_struct */
#define HAVE_VM_OP_FAULT 1

/* Kernel has xattr_acl */
/* #undef HAVE_XATTR_ACL */

/* Define to 1 if you have the <xtio.h> header file. */
/* #undef HAVE_XTIO_H */

/* Define to 1 if you have the <zlib.h> header file. */
#define HAVE_ZLIB_H 1

/* __add_wait_queue_exclusive exists */
/* #undef HAVE___ADD_WAIT_QUEUE_EXCLUSIVE */

/* __d_rehash is exported by the kernel */
/* #undef HAVE___D_REHASH */

/* __s16 is defined */
#define HAVE___S16 1

/* __s32 is defined */
#define HAVE___S32 1

/* __s64 is defined */
#define HAVE___S64 1

/* __s8 is defined */
#define HAVE___S8 1

/* __u16 is defined */
#define HAVE___U16 1

/* __u32 is defined */
#define HAVE___U32 1

/* __u64 is defined */
#define HAVE___U64 1

/* __u8 is defined */
#define HAVE___U8 1

/* call sysio init functions */
#define INIT_SYSIO 1

/* enable invariant checking */
/* #undef INVARIANT_CHECK */

/* quota_read found */
#define KERNEL_SUPPORTS_QUOTA_READ 1

/* Define this as the Kerberos version number */
/* #undef KRB5_VERSION */

/* ext4_discard_preacllocations defined */
#define LDISKFS_DISCARD_PREALLOCATIONS 1

/* EXT4_SINGLEDATA_TRANS_BLOCKS takes sb as argument */
#define LDISKFS_SINGLEDATA_TRANS_BLOCKS_HAS_SB 1

/* enable libcfs LASSERT, LASSERTF */
#define LIBCFS_DEBUG 1

/* Liblustre Support ACL-enabled MDS */
#define LIBLUSTRE_POSIX_ACL 1

/* use /dev/urandom for random data */
#define LIBLUSTRE_USE_URANDOM 1

/* have tux_info */
/* #undef LL_TASK_CL_ENV */

/* use dumplog on panic */
/* #undef LNET_DUMP_ON_PANIC */

/* Max LNET payload */
#define LNET_MAX_PAYLOAD LNET_MTU

/* enable page state tracking code */
/* #undef LUSTRE_PAGESTATE_TRACKING */

/* Multi-threaded user-level lustre port */
/* #undef LUSTRE_ULEVEL_MT */

/* maximum number of mdt threads */
/* #undef MDT_MAX_THREADS */

/* Report minimum OST free space */
/* #undef MIN_DF */

/* name of ldiskfs mkfs program */
#define MKE2FS "mke2fs"

/* IOCTL Buffer Size */
#define OBD_MAX_IOCTL_BUFFER 8192

/* Name of package */
#define PACKAGE "lustre"

/* Define to the address where bug reports for this package should be sent. */
#define PACKAGE_BUGREPORT "http://bugs.whamcloud.com/"

/* Define to the full name of this package. */
#define PACKAGE_NAME "Lustre"

/* Define to the full name and version of this package. */
#define PACKAGE_STRING "Lustre LUSTRE_VERSION"

/* Define to the one symbol short name of this package. */
#define PACKAGE_TARNAME "lustre"

/* Define to the version of this package. */
#define PACKAGE_VERSION "LUSTRE_VERSION"

/* Enable POSIX OSD */
/* #undef POSIX_OSD */

/* enable randomly alloc failure */
#define RANDOM_FAIL_ALLOC 1

/* The size of `unsigned long long', as computed by sizeof. */
#define SIZEOF_UNSIGNED_LONG_LONG 8

/* use tunable backoff TCP */
/* #undef SOCKNAL_BACKOFF */

/* tunable backoff TCP in ms */
/* #undef SOCKNAL_BACKOFF_MS */

/* 'struct stacktrace_ops' has 'walk_stack' field */
#define STACKTRACE_OPS_HAVE_WALK_STACK 1

/* Define to 1 if you have the ANSI C header files. */
#define STDC_HEADERS 1

/* name of ldiskfs tune program */
#define TUNE2FS "tune2fs"

/* Enable user-level OSS */
/* #undef UOSS_SUPPORT */

/* Define this if the private function, gss_krb5_cache_name, must be used to
   tell the Kerberos library which credentials cache to use. Otherwise, this
   is done by setting the KRB5CCNAME environment variable */
/* #undef USE_GSS_KRB5_CCACHE_NAME */

/* Write when Checking Health */
/* #undef USE_HEALTH_CHECK_WRITE */

/* enable lu_ref reference tracking code */
/* #undef USE_LU_REF */

/* Version number of package */
#define VERSION "2.1.0"

/* ext4_ext_walk_space takes i_data_sem */
#define WALK_SPACE_HAS_DATA_SEM 1

/* size of xattr acl */
#define XATTR_ACL_SIZE 260

#endif /* __MERO_LUSTRE_CONFIG_H__ */
