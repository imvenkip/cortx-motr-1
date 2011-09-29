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
#include "fid/fid.h"
#include "db/db.h"

/**
   @page DLD-conf.schema DLD for configuration schema

   - @ref DLD-fspec-ds
   - @ref DLD-fspec-sub
   - @ref DLD-fspec-cli
   - @ref DLD-fspec-usecases
   - @ref DLDDFS "Detailed Functional Specification" <!-- Note link -->

   @section DLD-fspec-ds Data Structures

   Simple lists can also suffice:
   - dld_sample_ds1
   - dld_bad_example

   The section could also describe what use it makes of data structures
   described elsewhere.

   Note that data structures are defined in the @ref DLDDFS "Detailed
   Functional Specification" so <b>do not duplicate the definitions</b>!  Do
   not describe internal data structures here either - they can be described in
   the @ref DLD-lspec "Logical Specification" if necessary.

   @section DLD-fspec-sub Subroutines

   @subsection DLD-fspec-sub-cons Constructors and Destructors

   @subsection DLD-fspec-sub-acc Accessors and Invariants

   @subsection DLD-fspec-sub-opi Operational Interfaces
   - dld_sample_sub1()

   @section DLD-fspec-cli Command Usage

   @section DLD-fspec-usecases Recipes
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
	C2_CFG_NODE_ONLINE  = 1 << 0,     /*< Online or Offline */
	C2_CFG_NODE_GOOD    = 1 << 1,     /*< Good or Failed    */
	C2_CFG_NODE_REAL    = 1 << 2,     /*< Real or Virtual node */
	C2_CFG_NODE_ENDIAN  = 1 << 3,     /*< Little-endian or Big-endian CPU */
};

enum {
	C2_CFG_NAME_LEN  = 128,
	C2_CFG_NAME_LEN2 = C2_CFG_NAME_LEN * 2
};

/**
   Node configuration.
   Keyed by human readable node name.
*/
struct c2_cfg_node {
	char               cn_name[C2_CFG_NAME_LEN]; /*< node name, key    */
	struct c2_cfg_uuid cn_uuid;                  /*< node uuid         */
	uint32_t	   cn_memory_size;           /*< memory size in MB */
	uint32_t	   cn_nr_processors;         /*< # of processors   */
	uint64_t           cn_last_state;            /*< last known state  */
	uint64_t	   cn_pool_id;               /*< pool id           */
};

/**
   Colibri network intercase card.
   keyed by nic name.
*/
struct c2_cfg_nic {
	char     cn_name[C2_CFG_NAME_LEN];      /*< HW address: MAC or others */
	uint32_t cn_type;                       /*< network interface type    */
	uint32_t cn_mtu;                        /*< MTU                       */
	uint64_t cn_speed;                      /*< bandwidth in bytes        */
	char     cn_node_name[C2_CFG_NAME_LEN]; /*< host node name            */
	uint64_t cn_last_state;                 /*< state                     */
};

enum c2_cfg_device_interface_type {
	C2_CFG_DEVICE_INTERFACE_ATA,
	C2_CFG_DEVICE_INTERFACE_SATA,
	C2_CFG_DEVICE_INTERFACE_SCSI,
	C2_CFG_DEVICE_INTERFACE_SATA2,
	C2_CFG_DEVICE_INTERFACE_SCSI2,
	C2_CFG_DEVICE_INTERFACE_SAS,
	C2_CFG_DEVICE_INTERFACE_SAS2
};


enum c2_cfg_device_media_type {
	C2_CFG_DEVICE_MEDIA_DISK,       /*< spin disk       */
	C2_CFG_DEVICE_MEDIA_SSD,	/*< or flash memory */
	C2_CFG_DEVICE_MEDIA_TAPE,       /*< tape            */
	C2_CFG_DEVICE_MEDIA_ROM         /*< read-only memory, like CD */
};

/**
   Colibri device.
   Keyed by uuid. @see c2_cfg_device_interface_type and c2_cfg_device_media_type
   for type and media field.
*/
struct c2_cfg_device {
	struct c2_cfg_uuid                cd_uuid;       /*< device uuid, key */
	enum c2_cfg_device_interface_type cd_type;	 /*< interface type   */
	enum c2_cfg_device_media_type     cd_media;      /*< media type:      */
	uint64_t                          cd_size;       /*< size in bytes    */
	uint64_t                          cd_last_state; /*< last known state */
};

enum c2_cfg_partition_type {
	C2_CFG_PARTITION_TYPE_EXT2,            /*< ext2 fs partition   */
	C2_CFG_PARTITION_TYPE_EXT3,            /*< ext3 fs partition   */
	C2_CFG_PARTITION_TYPE_EXT4,            /*< ext4 fs partition   */
	C2_CFG_PARTITION_TYPE_XFS,             /*< xfs fs partition    */
	C2_CFG_PARTITION_TYPE_JFS,             /*< jfs fs partition    */
	C2_CFG_PARTITION_TYPE_REISERFS,        /*< reiser fs partition */
	C2_CFG_PARTITION_TYPE_BTRFS            /*< btrfs fs partition  */
};

struct c2_cfg_partition {
	struct c2_cfg_uuid cp_uuid;            /*< partition uuid        */
	uint64_t           cp_start;           /*< start offset in bytes */
	uint64_t           cp_size;            /*< size in bytes         */
	struct c2_cfg_uuid cp_devide_uuid;     /*< host device uuid      */
	uint32_t           cp_index;           /*< partition index       */
	uint32_t           cp_type;            /*< partition type        */
	char               cp_filename[C2_CFG_NAME_LEN]; /*< filename in OS  */
};

enum {
	C2_CFG_PARAM_LEN = 128
};

/**
   Colibri storage.
*/
struct c2_cfg_pool {
	uint64_t cp_id;                           /*< pool id,   key        */
	char     cp_name[C2_CFG_NAME_LEN];        /*< pool name             */
	uint64_t cp_last_state;                   /*< pool state bits       */
	uint64_t cp_param_list[C2_CFG_PARAM_LEN]; /*< params, with id       */
};

struct c2_cfg_filesystem {
	char          cf_name[C2_CFG_NAME_LEN];  /* file system name        */
	struct c2_fid cf_rootfid;                /* file system root fid    */
};

enum c2_cfg_service_type {
	C2_CFG_SERVICE_METADATA,                 /*< metadata service       */
	C2_CFG_SERVICE_IO,                       /*< io/data service        */
	C2_CFG_SERVICE_MGMT,                     /*< management service     */
	C2_CFG_SERVICE_DLM,                      /*< metadata service       */
};

struct c2_cfg_service {
	struct c2_cfg_uuid       cs_uuid;        /*< service uuid          */
	enum c2_cfg_service_type cs_type;        /*< service type          */
	char      cs_node_name[C2_CFG_NAME_LEN]; /*< host node name        */
	char      cs_fs_name[C2_CFG_NAME_LEN];   /*< file system name      */
/*      end_points[]; */                         /*< end points            */
};


struct c2_cfg_profile {
	char cp_name[C2_CFG_NAME_LEN];      /*< profile name          */
	char cp_fs_name[C2_CFG_NAME_LEN];   /*< its file system name  */
};


struct c2_cfg_param {
	uint64_t cp_id;                       /*< param id                */
	char     cp_param[C2_CFG_NAME_LEN2];  /*< param itself: key=value */
};

struct c2_cfg_router {

};

struct c2_cfg_container {

};

struct c2_cfg_snapshot {

};

struct c2_cfg_user {

};

struct c2_cfg_security {

};

#define C2_CFG_DEVICE_DB     "devices"
#define C2_CFG_FS_DB         "fs"
#define C2_CFG_NIC_DB        "nics"
#define C2_CFG_NODE_DB       "nodes"
#define C2_CFG_PARTITION_DB  "partitions"
#define C2_CFG_PARAM_DB      "params"
#define C2_CFG_POOL_DB       "pool"
#define C2_CFG_PROFILE_DB    "profile"
#define C2_CFG_SERVICE_DB    "services"

/**
   Colibri configuration operation environment.
   This is an in-memory data structure.
*/
struct c2_cfg_env {
	char                c2_cfg_db_path[C2_CFG_NAME_LEN]; /*< cfg db path  */
	struct c2_dbenv	    c2_cfg_dbenv;                    /*< cfg db env   */
	struct c2_db_table *c2_cfg_devices;                  /*< devices table*/
	struct c2_db_table *c2_cfg_fs;                       /*< fs table     */
	struct c2_db_table *c2_cfg_nics;                     /*< nics table   */
	struct c2_db_table *c2_cfg_partitions;               /*< partitions   */
	struct c2_db_table *c2_cfg_params;                   /*< params table */
	struct c2_db_table *c2_cfg_pools;                    /*< pools table  */
	struct c2_db_table *c2_cfg_profiles;                 /*< profile table*/
	struct c2_db_table *c2_cfg_services;                 /*< service table*/
	struct c2_db_tx     c2_cfg_tx;                       /*< cfg tx       */
	struct c2_mutex     c2_cfg_lock;                     /*< cfg mutex    */
};

extern struct c2_cfg_env cfg_env;

int  c2_cfg_env_init(struct c2_cfg_env *env);
void c2_cfg_env_fini(struct c2_cfg_env *env);

/**
   Create a device cfg.

   @param device the device to create
   @retval 0 means success; -ve means failure.
   @pre the device doesn't exist in db
*/
int c2_cfg_device_create(struct c2_cfg_env *env, struct c2_cfg_device *device);

/**
   update a device cfg.

   @param device the device to update
   @retval 0 means success; -ve means failure.
   @pre the device exists in db
*/
int c2_cfg_device_update(struct c2_cfg_env *env, struct c2_cfg_device *device);

/**
   delete a device cfg.

   @param device the device to delete
   @retval 0 means success; -ve means failure.
   @pre the device exists in db
*/
int c2_cfg_device_delete(struct c2_cfg_env *env, struct c2_cfg_device *device);



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
