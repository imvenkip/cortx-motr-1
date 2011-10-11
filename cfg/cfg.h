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

#ifndef __COLIBRI_CFG_CFG_H__
#define __COLIBRI_CFG_CFG_H__

/* import */
#include "lib/types.h"
#include "fid/fid.h" /** import c2_fid */

/**
   @page DLD-conf.schema DLD for configuration schema

 */

/**
   @defgroup conf.schema Configuration Schema
   @brief DLD of configuration schema

   This file defines the interfaces and data structures to store and
   access the Colibri configuration information in database. Colibri
   configuration information is used to describe how a Colibri file system
   is organized by storage, nodes, devices, containers, services, etc.

   These data structures are used for on-disk and in-memory purpose.

   @see HLD of configuration schema <a>https://docs.google.com/a/xyratex.com/document/d/1JmsVBV8B4R-FrrYyJC_kX2ibzC1F-yTHEdrm3-FLQYk/edit?hl=en_US</a>
   @{
*/

enum {
	C2_CFG_UUID_SIZE = 40
};

/**
   config uuid
 */
struct c2_cfg_uuid {
	char cu_uuid[C2_CFG_UUID_SIZE];
};

/**
   state bits for node, device, nic, etc. These bits can be OR'd and tested.
*/
enum c2_cfg_state_bit {
	/** set if Online, and clear if Offline */
	C2_CFG_STATE_ONLINE  = 1 << 0,

	/*< set if Good, and clear if Failed    */
	C2_CFG_STATE_GOOD    = 1 << 1,
};

/**
   property bits for node, device, nic, etc. These bits can be OR'd and tested.
*/
enum c2_cfg_property_bit {
	/** set if real machine, and clear if virtual machine */
	C2_CFG_PROPERTY_REAL   = 1 << 0,

	/** set if Little-endian CPU, and clear if Big-endian CPU */
	C2_CFG_PROPERTY_ENDIAN = 1 << 1,

	/** set if a disk/device is removable */
	C2_CFG_PROPERTY_REMOVABLE = 1 << 2,
};


enum {
	C2_CFG_NAME_LEN  = 128,
	C2_CFG_NAME_LEN2 = C2_CFG_NAME_LEN * 2
};

/**
   Colibri node configuration key.
   Keyed by human readable node name.

*/
struct c2_cfg_node__key {
	/** node name */
	char               cn_name[C2_CFG_NAME_LEN];
};

/**
   Colibri node configuration value.
   f-key stands for foreign key.
*/
struct c2_cfg_node__val {
	/** node uuid         */
	struct c2_cfg_uuid cn_uuid;

	/** memory size in MB */
	uint32_t	   cn_memory_size;

	/** # of processors   */
	uint32_t	   cn_nr_processors;

	/** last known state. @see c2_cfg_state_bit  */
	uint64_t           cn_last_state;

	/** node property. @see c2_cfg_property_bit  */
	uint64_t           cn_property;

	/** pool id, f-key. @see c2_cfg_pool    */
	uint64_t	   cn_pool_id;
};


/**
   Network interface types.
*/
enum c2_cfg_network_interface_type {
	/** Ethernet, 10Mb */
	C2_CFG_NIC_INTERFACE_ETHER10 = 1,

	/** Ethernet, 100Mb */
	C2_CFG_NIC_INTERFACE_ETHER100,

	/** Ethernet, 1000Mb */
	C2_CFG_NIC_INTERFACE_ETHER1000,

	/** Ethernet, 10gb */
	C2_CFG_NIC_INTERFACE_ETHER10GB,

	/** Infini/Band, */
	C2_CFG_NIC_INTERFACE_INFINIBAND,
};

/**
   Colibri network intercase card key.
   keyed by nic name.
*/
struct c2_cfg_nic__key {
	/** HW address. */
	char     cn_hw_addr[C2_CFG_NAME_LEN];
};

/**
   Colibri network intercase card value.
*/
struct c2_cfg_nic__val {
	/** network interface type. @see c2_cfg_network_interface_type */
	uint32_t cn_interface_type;

	/** MTU  */
	uint32_t cn_mtu;

	/** bandwidth in bytes */
	uint64_t cn_speed;

	/** host node name, f-key     */
	char     cn_node_name[C2_CFG_NAME_LEN];

	/** state. @see c2_cfg_state_bit  */
	uint64_t cn_last_state;
};


/**
   Colibri device interface types.
*/
enum c2_cfg_storage_device_interface_type {
	C2_CFG_DEVICE_INTERFACE_ATA = 1,
	C2_CFG_DEVICE_INTERFACE_SATA,
	C2_CFG_DEVICE_INTERFACE_SCSI,
	C2_CFG_DEVICE_INTERFACE_SATA2,
	C2_CFG_DEVICE_INTERFACE_SCSI2,
	C2_CFG_DEVICE_INTERFACE_SAS,
	C2_CFG_DEVICE_INTERFACE_SAS2
};


/**
   Colibri device media types.
*/
enum c2_cfg_storage_device_media_type {
	/** spin disk       */
	C2_CFG_DEVICE_MEDIA_DISK = 1,

	/** SSD or flash memory */
	C2_CFG_DEVICE_MEDIA_SSD,

	/** tape            */
	C2_CFG_DEVICE_MEDIA_TAPE,

	/*< read-only memory, like CD */
	C2_CFG_DEVICE_MEDIA_ROM
};

/**
   Colibri storage device configuration key.
   Keyed by uuid.
*/
struct c2_cfg_storage_device__key {
	/** device uuid */
	struct c2_cfg_uuid csd_uuid;
};

/**
   Colibri storage device configuration value.

   @see c2_cfg_storage device_interface_type and,
   @see c2_cfg_storage_device_media_type for type and media field.
*/
struct c2_cfg_storage_device__val {
	/** interface type. @see c2_cfg_storage_device_interface_type  */
	uint32_t csd_type;

	/** media type. @see c2_cfg_storage_device_media_type */
	uint32_t csd_media;

	/** size in bytes    */
	uint64_t csd_size;

	/** last known state. @see c2_cfg_state_bit */
	uint64_t csd_last_state;

	/** property. @see c2_cfg_property_bit */
	uint64_t csd_property;

	/** filename in host OS */
	char     csd_filename[C2_CFG_NAME_LEN];

	/** the hosting node, f-key */
	char     csd_nodename[C2_CFG_NAME_LEN];
};

/**
   Colibri partition types.
*/
enum c2_cfg_storage_device_partition_type {
	/** ext2 fs partition   */
	C2_CFG_PARTITION_TYPE_EXT2 = 1,

	/** ext3 fs partition   */
	C2_CFG_PARTITION_TYPE_EXT3,

	/** ext4 fs partition   */
	C2_CFG_PARTITION_TYPE_EXT4,

	/** xfs fs partition    */
	C2_CFG_PARTITION_TYPE_XFS,

	/** jfs fs partition    */
	C2_CFG_PARTITION_TYPE_JFS,

	/** reiser fs partition */
	C2_CFG_PARTITION_TYPE_REISERFS,

	/** btrfs fs partition  */
	C2_CFG_PARTITION_TYPE_BTRFS,

	/** RAW partition  */
	C2_CFG_PARTITION_TYPE_RAW
};

/**
   Colibri storage devide partitions configuration key.
   Keyed by partition uuid.
*/
struct c2_cfg_storage_device_partition__key {
	/** partition uuid  */
	struct c2_cfg_uuid csdp_uuid;
};

/**
   Colibri storage devide partitions configuration value.
*/
struct c2_cfg_storage_device_partition__val {
	/** start offset in bytes */
	uint64_t           csdp_start;

	/** size in bytes         */
	uint64_t           csdp_size;

	/** host device uuid,f-key*/
	struct c2_cfg_uuid csdp_devide_uuid;

	/** partition index       */
	uint32_t           csdp_index;

	/** partition type. @see c2_cfg_storage_device_partition_type */
	uint32_t           csdp_type;

	/** filename in OS  */
	char               csdp_filename[C2_CFG_NAME_LEN];
};

enum {
	C2_CFG_PARAM_LEN = 128
};

/**
   Colibri pool configuration key.
   Keyed by pool id.
*/
struct c2_cfg_pool__key {
	/** pool id */
	uint64_t cp_id;
};

/**
   Colibri pool configuration value.
*/
struct c2_cfg_pool__val {
	/** pool name */
	char     cp_name[C2_CFG_NAME_LEN];

	/** pool state bits. @see c2_cfg_state_bit */
	uint64_t cp_last_state;

	/** list of param ids, f-keys */
	uint64_t cp_param_list[C2_CFG_PARAM_LEN];
};

/**
   Colibri file system configuration key.
   keyed by file system name.
*/
struct c2_cfg_filesystem__key {
	/** file system name   */
	char          cf_name[C2_CFG_NAME_LEN];
};

/**
   Colibri file system configuration val.
   keyed by file system name.
*/
struct c2_cfg_filesystem__val {
	/** file system root fid    */
	struct c2_fid cf_rootfid;
};

/**
   Colibri service type.
*/
enum c2_cfg_service_type {
	/** metadata service       */
	C2_CFG_SERVICE_METADATA = 1,

	/** io/data service        */
	C2_CFG_SERVICE_IO,

	/** management service     */
	C2_CFG_SERVICE_MGMT,

	/*< DLM service            */
	C2_CFG_SERVICE_DLM,
};

/**
   Colibri service configuration key.
   Keyed by service uuid.
*/
struct c2_cfg_service__key {
	/** service uuid  */
	struct c2_cfg_uuid       cs_uuid;
};

/**
   Colibri service configuration val.
*/
struct c2_cfg_service__val {
	/** service type. @see c2_cfg_service_type */
	uint32_t cs_type;

	/** unused. 64-bit alligned. */
	uint32_t cs_unsed;

	/** host node name,   f-key  */
	char     cs_host_node_name[C2_CFG_NAME_LEN];

	/** file system name, f-key  */
	char     cs_fs_name  [C2_CFG_NAME_LEN];

	/*< TODO: end points is not clear from HLD */
	/* end_points[]; */
};


/**
   Colibri profile configuration key.
   Keyed by profile name.
*/
struct c2_cfg_profile__key {
	/** profile name */
	char cp_name[C2_CFG_NAME_LEN];
};

/**
   Colibri profile configuration value.
*/
struct c2_cfg_profile__value {
	/** its file system name  */
	char cp_fs_name[C2_CFG_NAME_LEN];
};

/**
   Colibri configure parameters configuration key.
   Keyed by param id. The param is represented by "key=value".
   Sometimes, only keys exist, e.g. "readonly".
*/
struct c2_cfg_param__key {
	/** param id */
	uint64_t cp_id;
};

/**
   Colibri configure parameters configuration value.
*/
struct c2_cfg_param__val {
	/** param itself: key=value */
	char     cp_param[C2_CFG_NAME_LEN2];
};

#define C2_CFG_DEVICE_DB_TABLE     "devices"
#define C2_CFG_FS_DB_TABLE         "fs"
#define C2_CFG_NIC_DB_TABLE        "nics"
#define C2_CFG_NODE_DB_TABLE       "nodes"
#define C2_CFG_PARTITION_DB_TABLE  "partitions"
#define C2_CFG_PARAM_DB_TABLE      "params"
#define C2_CFG_POOL_DB_TABLE       "pool"
#define C2_CFG_PROFILE_DB_TABLE    "profile"
#define C2_CFG_SERVICE_DB_TABLE    "services"

/**
   @} conf.schema end group
*/

#endif /*  __COLIBRI_CFG_CFG_H__ */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 79
 *  scroll-step: 1
 *  End:
 */
