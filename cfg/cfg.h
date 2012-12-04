/* -*- C -*- */
/*
 * COPYRIGHT 2011 XYRATEX TECHNOLOGY LIMITED
 *
 * THIS DRAWING/DOCUMENT, ITS SPECIFICATIONS, AND THE DATA CONTAINED
 * HEREIN, ARE THE EXCLUSIVE PROPERTY OF XYRATEX TECHNOLOGY
 * LIMITED, ISSUED IN STRICT CONFIDENCE AND SHALL NOT, WITHOUT
 * THE PRIOR WRITTEN PERMISSION OF XYRATEX TECHNOLOGY LIMITED,
 * BE REPRODUCED, COPIED, OR DISCLOSED TO A THIRD PARTY, OR
 * USED FOR ANY PURPOSE WHATSOEVER, OR STORED IN A RETRIEVAL SYSTEM
 * EXCEPT AS ALLOWED BY THE TERMS OF XYRATEX LICENSES AND AGREEMENTS.
 *
 * YOU SHOULD HAVE RECEIVED A COPY OF XYRATEX'S LICENSE ALONG WITH
 * THIS RELEASE. IF NOT PLEASE CONTACT A XYRATEX REPRESENTATIVE
 * http://www.xyratex.com/contact
 *
 * Original author: Huang Hua <Hua_Huang@xyratex.com>
 * Original creation date: 09/27/2011
 */

#pragma once

#ifndef __MERO_CFG_CFG_H__
#define __MERO_CFG_CFG_H__

/* import */
#include "lib/types.h"
#include "fid/fid.h" /** import m0_fid */
#include "db/db.h"

/**
   @page DLD_conf_schema DLD for configuration schema

   This page contains the internal on-disk data structures for Mero
   configuration information.

   - @ref conf_schema "DLD for Configuration Schema"
 */

/**
   @defgroup conf_schema Configuration Schema
   @brief DLD of configuration schema

   This file defines the interfaces and data structures to store and
   access the Mero configuration information in database. Mero
   configuration information is used to describe how a Mero file system
   is organized by storage, nodes, devices, containers, services, etc.

   These data structures are used for on-disk purpose.

   <hr>
   @section DLD-ovw Overview

   This DLD contains the data structures and routines to organize the
   configuration information of Mero, to access these information.
   These configuration information is stored in database, and is populated
   to clients and servers who are using these information to take the whole
   Mero system up. These information is cached on clients and servers.
   These information is treated as resources, which are protected by locks.
   So configuration may be invalidated and re-acquired (that is update)
   by users when resources are revoked.

   The configuration schema is to provide a way to store, load, and update
   these informations. How to maintain the relations in-between these data
   strucctures is done by its upper layer.

   @see HLD of configuration schema <a>https://docs.google.com/a/xyratex.com/document/d/1JmsVBV8B4R-FrrYyJC_kX2ibzC1F-yTHEdrm3-FLQYk/edit?hl=en_US</a>
   @{
*/

enum {
	/** uuid string size */
	M0_CFG_UUID_SIZE = 40
};

/**
   config uuid
 */
struct m0_cfg_uuid {
	/** uuid */
	char cu_uuid[M0_CFG_UUID_SIZE];
};

/**
   state bits for node, device, nic, etc. These bits can be OR'd and tested.
*/
enum m0_cfg_state_bit {
	/** set if Online, and clear if Offline */
	M0_CFG_STATE_ONLINE  = 1 << 0,

	/** set if Good, and clear if Failed    */
	M0_CFG_STATE_GOOD    = 1 << 1
};

/**
   Property flag bits for node, device, nic, etc. These bits can be OR'd and tested.
*/
enum m0_cfg_flag_bit {
	/** set if real machine, and clear if virtual machine */
	M0_CFG_FLAG_REAL          = 1 << 0,

	/** set if Little-endian CPU, and clear if Big-endian CPU */
	M0_CFG_FLAG_LITTLE_ENDIAN = 1 << 1,

	/** set if a disk/device is removable */
	M0_CFG_FLAG_REMOVABLE     = 1 << 2,
};


enum {
	/** maximum name length */
	M0_CFG_NAME_LEN  = 128,

	/** double of the maximum name length */
	M0_CFG_NAME_LEN2 = M0_CFG_NAME_LEN * 2
};

/**
   Mero node configuration key.
   Keyed by human readable node name.

*/
struct m0_cfg_node__key {
	/** node name */
	char               cn_name[M0_CFG_NAME_LEN];
};

/**
   Mero node configuration value.
*/
struct m0_cfg_node__val {
	/** node uuid         */
	struct m0_cfg_uuid cn_uuid;

	/** memory size in MB */
	uint32_t	   cn_memory_size;

	/** # of processors   */
	uint32_t	   cn_nr_processors;

	/** last known state. @see m0_cfg_state_bit  */
	uint64_t           cn_last_state;

	/** node property flag. @see m0_cfg_flag_bit  */
	uint64_t           cn_flags;

	/** pool id, foreign-key. @see m0_cfg_pool__key    */
	uint64_t	   cn_pool_id;
};


/**
   Network interface types.
*/
enum m0_cfg_nic_type {
	/** Ethernet, 10Mb */
	M0_CFG_NIC_ETHER10 = 1,

	/** Ethernet, 100Mb */
	M0_CFG_NIC_ETHER100,

	/** Ethernet, 1000Mb */
	M0_CFG_NIC_ETHER1000,

	/** Ethernet, 10gb */
	M0_CFG_NIC_ETHER10GB,

	/** Infini/Band */
	M0_CFG_NIC_INFINIBAND
};

/**
   Mero network intercase card key.
   keyed by nic name.
*/
struct m0_cfg_nic__key {
	/** HW address. */
	char     cn_hw_addr[M0_CFG_NAME_LEN];
};

/**
   Mero network intercase card value.
*/
struct m0_cfg_nic__val {
	/** network interface type. @see m0_cfg_nic_type */
	uint32_t cn_interface_type;

	/** MTU  */
	uint32_t cn_mtu;

	/** speed, Mbits/second */
	uint64_t cn_speed;

	/** host node name, foreign-key     */
	char     cn_node_name[M0_CFG_NAME_LEN];

	/** nic's file name in host OS  */
	char     cn_file_name[M0_CFG_NAME_LEN];

	/** state. @see m0_cfg_state_bit  */
	uint64_t cn_last_state;
};


/**
   Mero device interface types.
*/
enum m0_cfg_storage_device_interface_type {
	M0_CFG_DEVICE_INTERFACE_ATA = 1,  /**< ATA     */
	M0_CFG_DEVICE_INTERFACE_SATA,     /**< SATA    */
	M0_CFG_DEVICE_INTERFACE_SCSI,     /**< SCSI    */
	M0_CFG_DEVICE_INTERFACE_SATA2,    /**< SATA II */
	M0_CFG_DEVICE_INTERFACE_SCSI2,    /**< SCSI II */
	M0_CFG_DEVICE_INTERFACE_SAS,      /**< SAS     */
	M0_CFG_DEVICE_INTERFACE_SAS2      /**< SAS II  */
};


/**
   Mero device media types.
*/
enum m0_cfg_storage_device_media_type {
	/** spin disk       */
	M0_CFG_DEVICE_MEDIA_DISK = 1,

	/** SSD or flash memory */
	M0_CFG_DEVICE_MEDIA_SSD,

	/** tape            */
	M0_CFG_DEVICE_MEDIA_TAPE,

	/** read-only memory, like CD */
	M0_CFG_DEVICE_MEDIA_ROM
};

/**
   Mero storage device configuration key.
   Keyed by uuid.
*/
struct m0_cfg_storage_device__key {
	/** device uuid */
	struct m0_cfg_uuid csd_uuid;
};

/**
   Mero storage device configuration value.

   @see m0_cfg_storage_device_interface_type and,
   @see m0_cfg_storage_device_media_type for type and media field.
*/
struct m0_cfg_storage_device__val {
	/** interface type. @see m0_cfg_storage_device_interface_type  */
	uint32_t csd_type;

	/** media type. @see m0_cfg_storage_device_media_type */
	uint32_t csd_media;

	/** size in bytes    */
	uint64_t csd_size;

	/** last known state. @see m0_cfg_state_bit */
	uint64_t csd_last_state;

	/** property flags. @see m0_cfg_flag_bit */
	uint64_t csd_flags;

	/** filename in host OS */
	char     csd_filename[M0_CFG_NAME_LEN];

	/** the hosting node, foreign-key */
	char     csd_nodename[M0_CFG_NAME_LEN];
};

const struct m0_table_ops m0_cfg_storage_device_table_ops;

/**
   Mero partition types.
*/
enum m0_cfg_storage_device_partition_type {
	/** RAW partition. Used as raw device.  */
	M0_CFG_PARTITION_TYPE_RAW = 0,

	/** ext2 fs partition   */
	M0_CFG_PARTITION_TYPE_EXT2,

	/** ext3 fs partition   */
	M0_CFG_PARTITION_TYPE_EXT3,

	/** ext4 fs partition   */
	M0_CFG_PARTITION_TYPE_EXT4,

	/** xfs fs partition    */
	M0_CFG_PARTITION_TYPE_XFS,

	/** jfs fs partition    */
	M0_CFG_PARTITION_TYPE_JFS,

	/** reiser fs partition */
	M0_CFG_PARTITION_TYPE_REISERFS,

	/** btrfs fs partition  */
	M0_CFG_PARTITION_TYPE_BTRFS,
};

/**
   Mero storage devide partitions configuration key.
   Keyed by partition uuid.
*/
struct m0_cfg_storage_device_partition__key {
	/** partition uuid  */
	struct m0_cfg_uuid csdp_uuid;
};

/**
   Mero storage devide partitions configuration value.
*/
struct m0_cfg_storage_device_partition__val {
	/** start offset in bytes */
	uint64_t           csdp_start;

	/** size in bytes         */
	uint64_t           csdp_size;

	/** host device uuid,foreign-key*/
	struct m0_cfg_uuid csdp_devide_uuid;

	/** partition index       */
	uint32_t           csdp_index;

	/** partition type. @see m0_cfg_storage_device_partition_type */
	uint32_t           csdp_type;

	/** filename in OS  */
	char               csdp_filename[M0_CFG_NAME_LEN];
};

enum {
	/** maximum number of params */
	M0_CFG_PARAM_LEN = 128
};

/**
   Mero pool configuration key.
   Keyed by pool id.
*/
struct m0_cfg_pool__key {
	/** pool id */
	uint64_t cp_id;
};

/**
   Mero pool configuration value.
*/
struct m0_cfg_pool__val {
	/** pool name */
	char     cp_name[M0_CFG_NAME_LEN];

	/** pool state bits. @see m0_cfg_state_bit */
	uint64_t cp_last_state;

	/** list of parameters for this pool, identified by param id. */
	uint64_t cp_param_list[M0_CFG_PARAM_LEN];
};

/**
   Mero file system configuration key.
   keyed by file system name.
*/
struct m0_cfg_filesystem__key {
	/** file system name   */
	char          cf_name[M0_CFG_NAME_LEN];
};

/**
   Mero file system configuration val.
   keyed by file system name.
*/
struct m0_cfg_filesystem__val {
	/** file system root fid    */
	struct m0_fid cf_rootfid;

	/** list of parameters for this file system, identified by param id.*/
	uint64_t cp_param_list[M0_CFG_PARAM_LEN];
};

/**
   Mero service type.
*/
/*enum m0_cfg_service_type {
	|+* metadata service       +|
	M0_CFG_SERVICE_METADATA = 1,

	|+* io/data service        +|
	M0_CFG_SERVICE_IO,

	|+* management service     +|
	M0_CFG_SERVICE_MGMT,

	|+* DLM service            +|
	M0_CFG_SERVICE_DLM,
};*/

/**
   Mero service configuration key.
   Keyed by service uuid.
*/
struct m0_cfg_service__key {
	/** service uuid  */
	struct m0_cfg_uuid       cs_uuid;
};

enum {
	M0_CFG_SERVICE_MAX_END_POINTS = 16
};

/**
   Mero service configuration val.
*/
struct m0_cfg_service__val {
	/** service type. @see m0_cfg_service_type */
	uint32_t cs_type;

	/** unused. 64-bit alligned. */
	uint32_t cs_unsed;

	/** host node name,   foreign-key  */
	char     cs_host_node_name[M0_CFG_NAME_LEN];

	/** file system name, foreign-key  */
	char     cs_fs_name[M0_CFG_NAME_LEN];

	/**
	   The array end_points[] gives a list of end-points from which
	   the service is reachable.

	   end_points[] is an array of character strings. Each element is a
	   string that can be passed to m0_net_end_point_create() as 'addr'
	   argument, to create a m0_net_end_point object.
	   See doxygen header of m0_net_end_point_create(), to know more
	   about addr argument.
	 */
	char     cs_end_points[M0_CFG_SERVICE_MAX_END_POINTS][M0_CFG_NAME_LEN];
};


/**
   Mero profile configuration key.
   Keyed by profile name.
*/
struct m0_cfg_profile__key {
	/** profile name */
	char cp_name[M0_CFG_NAME_LEN];
};

/**
   Mero profile configuration value.
*/
struct m0_cfg_profile__val {
	/** its file system name  */
	char cp_fs_name[M0_CFG_NAME_LEN];
};

/**
   Mero configure parameters configuration key.
   Keyed by param id. The param is represented by "key=value".
   Sometimes, only keys exist, e.g. "readonly".
*/
struct m0_cfg_param__key {
	/** param id */
	uint64_t cp_id;
};

/**
   Mero configure parameters configuration value.
*/
struct m0_cfg_param__val {
	/** param itself: key=value */
	char     cp_param[M0_CFG_NAME_LEN2];
};

#define M0_CFG_DEVICE_DB_TABLE     "devices"
#define M0_CFG_FS_DB_TABLE         "fs"
#define M0_CFG_NIC_DB_TABLE        "nics"
#define M0_CFG_NODE_DB_TABLE       "nodes"
#define M0_CFG_PARTITION_DB_TABLE  "partitions"
#define M0_CFG_PARAM_DB_TABLE      "params"
#define M0_CFG_POOL_DB_TABLE       "pools"
#define M0_CFG_PROFILE_DB_TABLE    "profiles"
#define M0_CFG_SERVICE_DB_TABLE    "services"

/**
   @} conf_schema end group
*/

#endif /*  __MERO_CFG_CFG_H__ */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 79
 *  scroll-step: 1
 *  End:
 */
