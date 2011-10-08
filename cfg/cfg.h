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
   Colibri node configuration.
   Keyed by human readable node name.
*/
struct c2_cfg_node {
	char               cn_name[C2_CFG_NAME_LEN]; /*< node name, key    */
	struct c2_cfg_uuid cn_uuid;                  /*< node uuid         */
	uint32_t	   cn_memory_size;           /*< memory size in MB */
	uint32_t	   cn_nr_processors;         /*< # of processors   */
	uint64_t           cn_last_state;            /*< last known state  */
	uint64_t	   cn_pool_id;               /*< pool id, f-key    */
};

/**
   Colibri network intercase card.
   keyed by nic name.
*/
struct c2_cfg_nic {
	char     cn_name[C2_CFG_NAME_LEN]; /*< HW address: MAC or others. key */
	uint32_t cn_type;                       /*< network interface type    */
	uint32_t cn_mtu;                        /*< MTU                       */
	uint64_t cn_speed;                      /*< bandwidth in bytes        */
	char     cn_node_name[C2_CFG_NAME_LEN]; /*< host node name, f-key     */
	uint64_t cn_last_state;                 /*< state                     */
};


/**
   Colibri device interface types.
*/
enum c2_cfg_device_interface_type {
	C2_CFG_DEVICE_INTERFACE_ATA,
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
	char       cd_filename[C2_CFG_NAME_LEN];      /*< filename in host OS */
	char       cd_nodename[C2_CFG_NAME_LEN];  /*< the hosting node, f-key */
};

/**
   Colibri partition types.
*/
enum c2_cfg_partition_type {
	C2_CFG_PARTITION_TYPE_EXT2,            /*< ext2 fs partition   */
	C2_CFG_PARTITION_TYPE_EXT3,            /*< ext3 fs partition   */
	C2_CFG_PARTITION_TYPE_EXT4,            /*< ext4 fs partition   */
	C2_CFG_PARTITION_TYPE_XFS,             /*< xfs fs partition    */
	C2_CFG_PARTITION_TYPE_JFS,             /*< jfs fs partition    */
	C2_CFG_PARTITION_TYPE_REISERFS,        /*< reiser fs partition */
	C2_CFG_PARTITION_TYPE_BTRFS            /*< btrfs fs partition  */
};

/**
   Colibri partitions.
   Keyed by partition uuid.
*/
struct c2_cfg_partition {
	struct c2_cfg_uuid cp_uuid;            /*< partition uuid, key   */
	uint64_t           cp_start;           /*< start offset in bytes */
	uint64_t           cp_size;            /*< size in bytes         */
	struct c2_cfg_uuid cp_devide_uuid;     /*< host device uuid,f-key*/
	uint32_t           cp_index;           /*< partition index       */
	uint32_t           cp_type;            /*< partition type        */
	char               cp_filename[C2_CFG_NAME_LEN]; /*< filename in OS  */
};

enum {
	C2_CFG_PARAM_LEN = 128
};

/**
   Colibri storage.
   Keyed by pool id.
*/
struct c2_cfg_pool {
	uint64_t cp_id;                           /*< pool id,   key        */
	char     cp_name[C2_CFG_NAME_LEN];        /*< pool name             */
	uint64_t cp_last_state;                   /*< pool state bits       */
	uint64_t cp_param_list[C2_CFG_PARAM_LEN]; /*< param ids, f-keys     */
};

/**
   Colibri file system.
   keyed by file system name.
*/
struct c2_cfg_filesystem {
	char          cf_name[C2_CFG_NAME_LEN];  /* file system name, key   */
	struct c2_fid cf_rootfid;                /* file system root fid    */
};

/**
   Colibri service type.
*/
enum c2_cfg_service_type {
	C2_CFG_SERVICE_METADATA,                 /*< metadata service       */
	C2_CFG_SERVICE_IO,                       /*< io/data service        */
	C2_CFG_SERVICE_MGMT,                     /*< management service     */
	C2_CFG_SERVICE_DLM,                      /*< metadata service       */
};

/**
   Colibri service.
   Keyed by service uuid.
*/
struct c2_cfg_service {
	struct c2_cfg_uuid       cs_uuid;        /*< service uuid, key        */
	enum c2_cfg_service_type cs_type;        /*< service type             */
	char      cs_node_name[C2_CFG_NAME_LEN]; /*< host node name,   f-key  */
	char      cs_fs_name  [C2_CFG_NAME_LEN]; /*< file system name, f-key  */
/*      end_points[]; */                         /*< end points               */
};


/**
   Colibri profile.
   Keyed by profile name.
*/
struct c2_cfg_profile {
	char cp_name[C2_CFG_NAME_LEN];           /*< profile name, key     */
	char cp_fs_name[C2_CFG_NAME_LEN];        /*< its file system name  */
};

/**
   Colibri configure parameters.
   Keyed by param id. The param is represented by "key=value". Sometimes, only
   keys exist, e.g. "readonly".
*/
struct c2_cfg_param {
	uint64_t cp_id;                          /*< param id, key           */
	char     cp_param[C2_CFG_NAME_LEN2];     /*< param itself: key=value */
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

/**
   Init the colibri configuration operation environment.

   This routines must be called before any configuration operations.

   @param env pointer to the operation environment.
   @retval 0 means success. -ve means failure.
   @see c2_cfg_env_fini
*/
int  c2_cfg_env_init(struct c2_cfg_env *env);

/**
   Finish the colibri configuration operation environment.

   This routines must be called last to cleanup the operation environment.

   @param env pointer to the operation environment.
   @param commit true to commit the db transaction, false to abort.
   @see c2_cfg_env_init
*/
void c2_cfg_env_fini(struct c2_cfg_env *env, bool commit);

/**
   Insert a device cfg.

   @param device the device to create
   @retval 0 means success; -ve means failure.
   @pre the device doesn't exist in db
*/
int c2_cfg_device_insert(struct c2_cfg_env *env, struct c2_cfg_device *device);

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
   list all device cfg. This is similar to directory listing by readdir().

   Upper layer calls this function to get all devices information.

   @param last the last device in previous call. If this is the first call,
          pass NULL to this.
   @param results an array of devices to store the results.
   @param len the length of the above array.
   @retval number of devices in the results; -ve means failure. If the return
           is zero, end is reached and no more call is needed.
*/
int c2_cfg_device_list(struct c2_cfg_env *env, const struct c2_cfg_device *last,
		       struct c2_cfg_device *results, const int len);


/**
   Insert a filesystem cfg.

   @param fs the filesystem info to insert
   @retval 0 means success; -ve means failure.
   @pre the filesystem doesn't exist in db
*/
int c2_cfg_fs_insert(struct c2_cfg_env *env, struct c2_cfg_filesystem *fs);

/**
   update a filesystem cfg.

   @param fs the filesystem to update
   @retval 0 means success; -ve means failure.
   @pre the filesystem exists in db
*/
int c2_cfg_fs_update(struct c2_cfg_env *env, struct c2_cfg_filesystem *fs);

/**
   delete a filesystem cfg.

   @param fs the filesystem to delete
   @retval 0 means success; -ve means failure.
   @pre the filesystem exists in db
*/
int c2_cfg_fs_delete(struct c2_cfg_env *env, struct c2_cfg_filesystem *fs);

/**
   list all file systems.

   Upper layer calls this function to get all file system information.

   @param last the last fs in previous call. If this is the first call,
          pass NULL to this.
   @param results an array of file systems to store the results.
   @param len the length of the above array.
   @retval number of fs in the results; -ve means failure. If the return
           is zero, end is reached and no more call is needed.
*/
int c2_cfg_fs_list(struct c2_cfg_env *env, const struct c2_cfg_filesystem *last,
		   struct c2_cfg_filesystem *results, const int len);


/**
   Insert a nic cfg.

   @param nic the nic info to insert
   @retval 0 means success; -ve means failure.
   @pre the nic doesn't exist in db
*/
int c2_cfg_nic_insert(struct c2_cfg_env *env, struct c2_cfg_nic *nic);

/**
   update a nic cfg.

   @param nic the nic to update
   @retval 0 means success; -ve means failure.
   @pre the nic exists in db
*/
int c2_cfg_nic_update(struct c2_cfg_env *env, struct c2_cfg_nic *nic);

/**
   delete a nic cfg.

   @param nic the nic to delete
   @retval 0 means success; -ve means failure.
   @pre the nic exists in db
*/
int c2_cfg_nic_delete(struct c2_cfg_env *env, struct c2_cfg_nic *nic);

/**
   list all nics.

   Upper layer calls this function to get all nic information.

   @param last the last nic in previous call. If this is the first call,
          pass NULL to this.
   @param results an array of nics to store the results.
   @param len the length of the above array.
   @retval number of nics in the results; -ve means failure. If the return
           is zero, end is reached and no more call is needed.
*/
int c2_cfg_nic_list(struct c2_cfg_env *env, const struct c2_cfg_nic *last,
		    struct c2_cfg_nic *results, const int len);

/**
   Insert a node cfg.

   @param node the node info to insert
   @retval 0 means success; -ve means failure.
   @pre the node doesn't exist in db
*/
int c2_cfg_node_insert(struct c2_cfg_env *env, struct c2_cfg_node *node);

/**
   update a node cfg.

   @param node the node to update
   @retval 0 means success; -ve means failure.
   @pre the node exists in db
*/
int c2_cfg_node_update(struct c2_cfg_env *env, struct c2_cfg_node *node);

/**
   delete a node cfg.

   @param node the node to delete
   @retval 0 means success; -ve means failure.
   @pre the node exists in db
*/
int c2_cfg_node_delete(struct c2_cfg_env *env, struct c2_cfg_node *node);

/**
   list all nodes.

   Upper layer calls this function to get all node information.

   @param last the last node in previous call. If this is the first call,
          pass NULL to this.
   @param results an array of nodes to store the results.
   @param len the length of the above array.
   @retval number of nics in the results; -ve means failure. If the return
           is zero, end is reached and no more call is needed.
*/
int c2_cfg_node_list(struct c2_cfg_env *env, const struct c2_cfg_node *last,
		     struct c2_cfg_node *results, const int len);

/**
   Insert a partition cfg.

   @param p the partition info to insert
   @retval 0 means success; -ve means failure.
   @pre the partition doesn't exist in db
*/
int c2_cfg_partition_insert(struct c2_cfg_env *env, struct c2_cfg_partition *p);

/**
   update a partition cfg.

   @param p the partition to update
   @retval 0 means success; -ve means failure.
   @pre the partition exists in db
*/
int c2_cfg_partition_update(struct c2_cfg_env *env, struct c2_cfg_partition *p);

/**
   delete a partition cfg.

   @param p the partition to delete
   @retval 0 means success; -ve means failure.
   @pre the partition exists in db
*/
int c2_cfg_partition_delete(struct c2_cfg_env *env, struct c2_cfg_partition *p);

/**
   list all partitions.

   Upper layer calls this function to get all partition information.

   @param last the last partition in previous call. If this is the first call,
          pass NULL to this.
   @param results an array of partitions to store the results.
   @param len the length of the above array.
   @retval number of nics in the results; -ve means failure. If the return
           is zero, end is reached and no more call is needed.
*/
int c2_cfg_partition_list(struct c2_cfg_env *env,
			  const struct c2_cfg_partition *last,
		          struct c2_cfg_partition *results, const int len);


/**
   Insert a parameter cfg.

   @param p the param info to insert
   @retval 0 means success; -ve means failure.
   @pre the param doesn't exist in db
*/
int c2_cfg_param_insert(struct c2_cfg_env *env, struct c2_cfg_param *p);

/**
   update a param cfg.

   @param p the param to update
   @retval 0 means success; -ve means failure.
   @pre the param exists in db
*/
int c2_cfg_param_update(struct c2_cfg_env *env, struct c2_cfg_param *p);

/**
   delete a param cfg.

   @param p the param to delete
   @retval 0 means success; -ve means failure.
   @pre the param exists in db
*/
int c2_cfg_param_delete(struct c2_cfg_env *env, struct c2_cfg_param *p);

/**
   list all params.

   Upper layer calls this function to get all params information.

   @param last the last param in previous call. If this is the first call,
          pass NULL to this.
   @param results an array of params to store the results.
   @param len the length of the above array.
   @retval number of nics in the results; -ve means failure. If the return
           is zero, end is reached and no more call is needed.
*/
int c2_cfg_param_list(struct c2_cfg_env *env, const struct c2_cfg_param *last,
		      struct c2_cfg_param *results, const int len);


/**
   Insert a pool cfg.

   @param p the pool info to insert
   @retval 0 means success; -ve means failure.
   @pre the pool doesn't exist in db
*/
int c2_cfg_pool_insert(struct c2_cfg_env *env, struct c2_cfg_pool *p);

/**
   update a pool cfg.

   @param p the pool to update
   @retval 0 means success; -ve means failure.
   @pre the pool exists in db
*/
int c2_cfg_pool_update(struct c2_cfg_env *env, struct c2_cfg_pool *p);

/**
   delete a pool cfg.

   @param p the pool to delete
   @retval 0 means success; -ve means failure.
   @pre the pool exists in db
*/
int c2_cfg_pool_delete(struct c2_cfg_env *env, struct c2_cfg_pool *p);

/**
   list all pools.

   Upper layer calls this function to get all pool information.

   @param last the last pool in previous call. If this is the first call,
          pass NULL to this.
   @param results an array of pools to store the results.
   @param len the length of the above array.
   @retval number of nics in the results; -ve means failure. If the return
           is zero, end is reached and no more call is needed.
*/
int c2_cfg_pool_list(struct c2_cfg_env *env, const struct c2_cfg_pool *last,
		     struct c2_cfg_pool *results, const int len);



/**
   Insert a profile cfg.

   @param p the profile info to insert
   @retval 0 means success; -ve means failure.
   @pre the profile doesn't exist in db
*/
int c2_cfg_profile_insert(struct c2_cfg_env *env, struct c2_cfg_profile *p);

/**
   update a profile cfg.

   @param p the profile to update
   @retval 0 means success; -ve means failure.
   @pre the profile exists in db
*/
int c2_cfg_profile_update(struct c2_cfg_env *env, struct c2_cfg_profile *p);

/**
   delete a profile cfg.

   @param p the profile to delete
   @retval 0 means success; -ve means failure.
   @pre the profile exists in db
*/
int c2_cfg_profile_delete(struct c2_cfg_env *env, struct c2_cfg_profile *p);

/**
   list all profiles.

   Upper layer calls this function to get all profile information.

   @param last the last profile in previous call. If this is the first call,
          pass NULL to this.
   @param results an array of profiles to store the results.
   @param len the length of the above array.
   @retval number of nics in the results; -ve means failure. If the return
           is zero, end is reached and no more call is needed.
*/
int c2_cfg_profile_list(struct c2_cfg_env *env,
			const struct c2_cfg_profile *last,
			struct c2_cfg_profile *results, const int len);


/**
   Insert a service cfg.

   @param s the service info to insert
   @retval 0 means success; -ve means failure.
   @pre the service doesn't exist in db
*/
int c2_cfg_service_insert(struct c2_cfg_env *env, struct c2_cfg_service *s);

/**
   update a service cfg.

   @param s the service to update
   @retval 0 means success; -ve means failure.
   @pre the service exists in db
*/
int c2_cfg_service_update(struct c2_cfg_env *env, struct c2_cfg_service *s);

/**
   delete a service cfg.

   @param s the service to delete
   @retval 0 means success; -ve means failure.
   @pre the service exists in db
*/
int c2_cfg_service_delete(struct c2_cfg_env *env, struct c2_cfg_service *s);

/**
   list all services.

   Upper layer calls this function to get all service information.

   @param last the last service in previous call. If this is the first call,
          pass NULL to this.
   @param results an array of services to store the results.
   @param len the length of the above array.
   @retval number of nics in the results; -ve means failure. If the return
           is zero, end is reached and no more call is needed.
*/
int c2_cfg_service_list(struct c2_cfg_env *env,
			const struct c2_cfg_service *last,
			struct c2_cfg_service *results, const int len);

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
