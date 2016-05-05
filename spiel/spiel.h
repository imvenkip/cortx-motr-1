/* -*- C -*- */
/*
 * COPYRIGHT 2014 XYRATEX TECHNOLOGY LIMITED
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
 * Original author: Nikita Danilov <nikita.danilov@seagate.com>
 * Original creation date: 09-Dec-2014
 */

#pragma once

#ifndef __MERO_SPIEL_SPIEL_H__
#define __MERO_SPIEL_SPIEL_H__

#include "fid/fid.h"           /* m0_fid */
#include "conf/schema.h"       /* m0_conf_service_type */
#include "conf/confc.h"        /* m0_confc */
#include "conf/rconfc.h"       /* m0_rconfc */
#include "reqh/reqh_service.h" /* enum m0_service_health */
#include "sns/cm/cm.h"         /* m0_sns_cm_status */

/**
 * @page spiel-dld Spiel API DLD
 *
 *  - @ref spiel-dld-ovw
 *  - @ref spiel-dld-def
 *  - @ref spiel-dld-conf
 *    - @ref spiel-dld-conf-schema
 *    - @ref spiel-dld-conf-iface
 *    - @ref spiel-dld-conf-invoc
 *  - @ref spiel-api-fspec
 *  - @ref spiel-api-fspec-intr
 *
 * Definition of Seagate Software Platform Library for Mero (SPIEL, SSPL).
 *
 * <hr>
 * @section spiel-dld-ovw Overview
 *
 * Spiel library is used by a "management application" to control Mero:
 *
 *  - to inform Mero about hardware resources it should use, their roles and
 *    arrangement;
 *
 *  - to specify operational characteristics of Mero, such as fault-tolerance
 *    parameters;
 *
 *  - to issue commands to modify the cluster state: start and stop
 *    operation, format storage, etc.
 *
 * Mero stores information about cluster elements (hardware and software), their
 * arrangement, functions and operational characteristics in an internal
 * (replicated) data-base, called configuration data-base.
 *
 * Cluster state is changed by sending operation requests (fops) to Mero
 * services running on cluster nodes. This assumes that every node already runs
 * a mininal Mero process, which is started on node bootup and can be then
 * remotely commanded to start more services, as necessary.
 *
 * <hr>
 * @section spiel-dld-def Definitions
 *
 * - @b Configuration: Data-base describing Mero cluster in details required and
 *   sufficient for cluster components operation. See @ref spiel-dld-conf.
 *
 * - @b Version: A @b Configuration data-base snapshot reflecting the changes
 *   introduced by "managements application". A @b Version is intended for being
 *   uploaded to confd servers.
 *
 * - @b Transaction: A standard mechanism of spreading @b Version among confd
 *   servers and putting it into effect. @b Transaction guarantees @b Version
 *   being consistently distributed among confd servers and reached a quorum
 *   enough for non-conflicting configuration reading. A @b Transaction needs to
 *   be open explicitly. Later it may be either closed or committed. A @b
 *   Version appliance occurs in case of successful committing only.
 *
 * <hr>
 * @section spiel-dld-conf Configuration data-base
 *
 * Mero configuration data-base contains all the meta-data that is manipulated
 * by a system administrator, as opposed to meta-data manipulated in the course
 * of executing application requests.
 *
 * Data-base is a graph. Graph nodes represent cluster elements and
 * arcs---relations between elements. Graph matches the "schema", which defines
 * types of elements and possible relations between them.
 *
 * The following configuration elements are currently supported (see conf/obj.h
 * for details):
 *
 *  - a profile is an access path to the cluster. Profiles are used to serve
 *    different storage services from the same hardware. Currently only a single
 *    profile is supported. Profile is the root of configuration graph;
 *
 *  - a filesystem represents a top-level name space (filesystem-like or
 *    objectstore-like);
 *
 *  - a pool is a collection of hardware resources. Cluster hardware is divided
 *    into pools for administrative purposes (for example, for security reasons)
 *    and to encode fault-tolerance properties;
 *
 *  - a pool version is a list of elements that belonged to a pool at a certain
 *    moment in time. As system evolves, new hardware is added and old hardware
 *    retired, contents of a pool might change (this change is reflected by
 *    creation of a new pool version), but pool identity remains unchanged;
 *
 *  - a rack of enclosures (cf. "a knot of toads",
 *    http://www.oxforddictionaries.com/words/what-do-you-call-a-group-of);
 *
 *  - an enclosure;
 *
 *  - a controller;
 *
 *  - a storage device: a rotational or solid-state drive;
 *
 *  - a node is something capable or running processes. Controllers are one type
 *    of node, but a cluster can contain other nodes;
 *
 *  - a process is a user-space process or kernel executing services;
 *
 *  - a service is an executable entity that can accept and execute requests;
 *
 *  - in addition, off each pool version hangs off a tree of "v-objects"
 *    (rack-v, enclosure-v and controller-v) that specify which hardware
 *    elements belong to the pool version. A v-object contains a pointer to the
 *    "real object" (rack, enclosure or controller) and the list of
 *    children. Such indirect arrangement makes it possible to have pool
 *    versions sharing hardware.
 *
 * @subsection spiel-dld-conf-schema Schema
 *
 * @note from conf/obj.h
 *
 * @dot
 * digraph x {
 *   edge [arrowhead=open, fontsize=11];
 *
 *   root -> profile [label=profiles];
 *   profile -> filesystem;
 *
 *   filesystem -> "node" [label=nodes];
 *   filesystem -> rack [label=racks];
 *   filesystem -> pool [label=pools];
 *   "node" -> process [label=processes];
 *   process -> service [label=services];
 *   service -> sdev [label=sdevs];
 *   rack -> enclosure [label=encls];
 *   enclosure -> controller [label=ctrls];
 *   controller -> disk [label=disks];
 *   "node" -> controller [dir=back, weight=0, style=dashed];
 *
 *   pool -> "pool version" [label=pvers];
 *   "pool version" -> "rack-v" [label=rackvs];
 *   "rack-v" -> "enclosure-v" [label=children];
 *   "enclosure-v" -> "controller-v" [label=children];
 *   "controller-v" -> "disk-v" [label=children];
 *   rack -> "rack-v" [dir=back, weight=0, style=dashed];
 *   enclosure -> "enclosure-v" [dir=back, weight=0, style=dashed];
 *   controller -> "controller-v" [dir=back, style=dashed, weight=0];
 *   disk -> "disk-v" [dir=back, style=dashed, weight=0];
 *   sdev -> disk [dir=back, style=dashed, weight=0];
 * }
 * @enddot
 *
 * @subsection spiel-dld-conf-iface Interface
 *
 * Each configuration element has a unique identifier, which is assigned by the
 * management application. An identifier is 128 bits (m0_fid), with 8 most
 * significant bits representing object type.
 *
 * Spiel interface is divided into two parts: configuration management and
 * command interface.
 *
 * Configuration management interface is designed in transactional manner.
 * Command interface defines individual, separate calls.
 *
 * @subsection spiel-dld-conf-invoc Invocation
 *
 * Spiel interface is exported from the standard Mero library, which uses Mero
 * networking for communication. As a result, spiel entry points can be invoked
 * on any node in the cluster.
 *
 * @defgroup spiel-api-fspec Spiel API public interface
 * @{
 */

struct m0_rpc_machine;
struct m0_pdclust_attr;
struct m0_reqh;

struct m0_spiel_sns_status {
	/* SNS service fid */
	struct m0_fid         sss_fid;
	/* State of current repair/rebalance, see @ref m0_sns_cm_status*/
	enum m0_sns_cm_status sss_state;
	/* Progress of current repair/rebalance in percent */
	unsigned int          sss_progress;
};

/**
 * Spiel instance context
 */
struct m0_spiel {
	/**
	 * Core spiel data most of the spiel internals operate on.
	 */
	struct m0_spiel_core {
		/** RPC machine for network communication */
		struct m0_rpc_machine     *spc_rmachine;
		/** Configuration profile for spiel command interface */
		struct m0_fid              spc_profile;
		/**
		 * Current working confc instance.
		 *
		 * Normally it points at
		 * m0_spiel::spl_rconfc::rc_confc. However, the need is
		 * stipulated by possible situation when m0_spiel::spl_rconfc
		 * remain uninitialised while client side already has a confc
		 * instance already filled with the conf data.
		 */
		struct m0_confc           *spc_confc;
	}                          spl_core;
	/** Rconfc instance */
	struct m0_rconfc           spl_rconfc;
	/** Write lock context */
	struct m0_spiel_wlock_ctx *spl_wlock_ctx;
};

/**
 * Initialises spiel instance.
 * Should be invoked before using other spiel functions.
 * If initialisation fails, then spiel instance must not be used.
 *
 * @param spiel   spiel instance
 * @param reqh    request handler
 *
 * @pre  reqh != NULL
 */
int m0_spiel_init(struct m0_spiel *spiel, struct m0_reqh *reqh);

/**
 * Finalises spiel instance.
 */
void m0_spiel_fini(struct m0_spiel *spiel);

/**********************************************************/
/*              Configuration management                  */
/**********************************************************/

/**
 * The following example shows how to use configuration management commands.
 * Note * that if one of *_add commands returns result code other than 0,
 * -EEXIST, -EINVAL further calling of *_add commands or tx_commit doesn't make
 * sense.
 *
 * @code
 *     struct m0_spiel_tx *tx;
 *     struct m0_spiel    *spiel;
 *     int                 rc;
 *
 *     m0_spiel_tx_open(spiel, tx);
 *     rc = m0_spiel_profile_add(tx, prof_id) ?:
 *          m0_spiel_filesystem_add(tx, fs_id, prof_id, 10, root_id, params) ?:
 *          ...Adding other objects to construct full configuration database. ?:
 *          m0_spiel_tx_commit(tx);
 *     m0_spiel_tx_close(tx);
 * @endcode
 */
/**
 * Spiel transaction
 */
struct m0_spiel_tx {
	/** Spiel instance context */
	struct m0_spiel      *spt_spiel;
	/** Cache m0_obj objects for Spiel transaction */
	struct m0_conf_cache  spt_cache;
	/** Cache's mutex */
	struct m0_mutex       spt_lock;
	/**
	  * String representation of Spiel transaction
	  * Create only for all Endpoints
	  * Free afrer end last receive on Spiel load FOP
	  */
	char                 *spt_buffer;
};

/**
 * Initialises and opens spiel transaction.
 *
 * In case transaction is created for the sole purpose of dumping conf data,
 * to string or file, `spiel' parameter may be NULL:
 *
 * @code
 * spiel_tx__conf_str_create() {
 *	struct m0_spiel_tx  tx;
 *	const int           ver_forced = 10;
 *	char               *local_conf;
 *	int                 rc;
 *
 *	rc = m0_spiel_tx_open(NULL, &tx);
 *
 *	. . . add configuration items to tx . . .
 *
 *	rc = m0_spiel_tx_to_str(&tx, ver_forced, &local_conf);
 *
 *	. . . make use of local_conf . . .
 *
 *	m0_spiel_tx_str_free(local_conf);
 *	m0_spiel_tx_close(&tx);
 * }
 * @endcode
 *
 * In case (spiel == NULL), the transaction must not be m0_spiel_tx_commit()ted.
 *
 * @pre tx != NULL
 */
void m0_spiel_tx_open(struct m0_spiel    *spiel,
		      struct m0_spiel_tx *tx);

/**
 * Closes spiel transaction.
 *
 * Once function is called spiel transaction can't be used anymore.
 *
 * @param tx spiel transaction
 */
void m0_spiel_tx_close(struct m0_spiel_tx *tx);

/**
 * Commits filled-in spiel transaction. The call performs normal committing when
 * reaching quorum is mandatory for uploading new configuration to confd servers
 * and putting it in effect. Endpoints of confd and rm services will be resolved
 * internally by rconfc which gets them from HA service.
 *
 * Once function succeeded, the spiel transaction must not be committed
 * anymore. When failed, forced committing with m0_spiel_tx_commit_forced()
 * still remains as an option.
 *
 * @param tx        spiel transaction
 *
 * @note In case normal transaction committing is required, but resultant quorum
 * number reached is to be controlled as well, the action has to be done using
 * m0_spiel_tx_commit_forced(), specifying non-forced committing as follows:
 @code
    uint32_t rquorum = 0;
    int rc = m0_spiel_tx_commit_forced(tx, false, M0_CONF_VER_UNKNOWN,
                                       &rquorum);
 @endcode
 */
int m0_spiel_tx_commit(struct m0_spiel_tx  *tx);

/**
 * Commits filled-in spiel transaction forcing as many loads and flips as
 * possible, no matter if quorum reached or not. The call allows version number
 * be overridden compared to the version number obtained at m0_spiel_start(). In
 * this case @b ver_forced must be of the value other than M0_CONF_VER_UNKNOWN,
 * otherwise the version number value remains what it initially was.
 *
 * The spiel transaction may be forcibly committed as many times as required
 * completing previously failed uploads to confd servers.
 *
 * @param tx         spiel transaction
 * @param forced     committing with forcing any possible LOAD/FLIP enabled
 * @param ver_forced version number the initial value to be overridden with
 * @param rquorum    resultant quorum value reached, NULL value allowed
 *
 * @note Parameters @b forced and @b ver_forced may be used independent of each
 * other, i.e. forced committing with unchanged version number is possible as
 * well as non-forced committing with version number overridden.
 */
int m0_spiel_tx_commit_forced(struct m0_spiel_tx  *tx,
			      bool                 forced,
			      uint64_t             ver_forced,
			      uint32_t            *rquorum);

/**
 * Adds profile to the configuration tree of the transaction
 *
 * @param tx  spiel transaction
 * @param fid fid of the profile
 */
int m0_spiel_profile_add(struct m0_spiel_tx *tx, const struct m0_fid *fid);

/**
 * Adds filesystem to the configuration tree of the transaction
 *
 * @param tx         spiel transaction
 * @param fid        fid of the filesystem
 * @param parent     fid of the parent profile
 * @param redundancy metadata redundancy count
 * @param rootfid    root's fid of filesystem
 * @param mdpool     meta-data pool
 * @param fs_params  NULL-terminated array of command-line like parameters @n
 *                   Parameters are copied, so caller can safely free them.
 */
int m0_spiel_filesystem_add(struct m0_spiel_tx    *tx,
			    const struct m0_fid   *fid,
			    const struct m0_fid   *parent,
			    unsigned               redundancy,
			    const struct m0_fid   *rootfid,
			    const struct m0_fid   *mdpool,
			    const char           **fs_params);

/**
 * Adds node to the configuration tree of the transaction
 *
 * @param tx         spiel transaction
 * @param fid        fid of the node
 * @param parent     fid of the parent filesystem
 * @param memsize    amount of available memory on the node
 * @param nr_cpu     number of CPUs on the node
 * @param last_state last known state (bitmask of @ref ::m0_cfg_state_bit)
 * @param flags      different flags (bitmask of @ref ::m0_cfg_flag_bit)
 * @param pool_fid   fid of the pool associated with this node
 */
int m0_spiel_node_add(struct m0_spiel_tx  *tx,
		      const struct m0_fid *fid,
		      const struct m0_fid *parent,
		      uint32_t             memsize,
		      uint32_t             nr_cpu,
		      uint64_t             last_state,
		      uint64_t             flags,
		      struct m0_fid       *pool_fid);

/**
 * Adds process to the configuration tree of the transaction
 *
 * @param tx         spiel transaction
 * @param fid        fid of the process
 * @param parent     fid of the parent node
 * @param cores      limit on the number of used cores
 * @param memlimit   memory limit for process
 * @param endpoint   process endpoint
 */
int m0_spiel_process_add(struct m0_spiel_tx  *tx,
			 const struct m0_fid *fid,
			 const struct m0_fid *parent,
			 struct m0_bitmap    *cores,
			 uint64_t             memlimit_as,
			 uint64_t             memlimit_rss,
			 uint64_t             memlimit_stack,
			 uint64_t             memlimit_memlock,
			 const char          *endpoint);

/** Spiel service information */
struct m0_spiel_service_info {
	/** Service type */
	enum m0_conf_service_type svi_type;
	/**
	 * Service end point.
	 * NULL terminated array of C strings.
	 */
	const char              **svi_endpoints;
	/** Different service-specific parameters */
	union {
		uint32_t      repair_limits;
		struct m0_fid addb_stobid;
		const char   *confdb_path;
	} svi_u;
};

/**
 * Adds service to the configuration tree of the transaction
 *
 * @param tx           spiel transaction
 * @param fid          fid of the service
 * @param parent       fid of the parent process
 * @param service_info service info
 */
int m0_spiel_service_add(struct m0_spiel_tx                 *tx,
			 const struct m0_fid                *fid,
			 const struct m0_fid                *parent,
			 const struct m0_spiel_service_info *service_info);

/**
 * Adds service to the configuration tree of the transaction
 *
 * @param tx           spiel transaction
 * @param fid          fid of the device
 * @param parent       fid of the parent service
 * @param iface        device interface type
 * @param media        device media type
 * @param bsize        block size in bytes
 * @param size         size in bytes
 * @param last_state   last known state (bitmask of @ref ::m0_cfg_state_bit)
 * @param flags        different flags (bitmask of @ref ::m0_cfg_flag_bit)
 * @param filename     device filename.
 */
int m0_spiel_device_add(struct m0_spiel_tx                        *tx,
		        const struct m0_fid                       *fid,
		        const struct m0_fid                       *svc_parent,
		        const struct m0_fid                       *disk_parent,
		        uint32_t                                   dev_idx,
		        enum m0_cfg_storage_device_interface_type  iface,
		        enum m0_cfg_storage_device_media_type      media,
		        uint32_t                                   bsize,
		        uint64_t                                   size,
		        uint64_t                                   last_state,
		        uint64_t                                   flags,
		        const char                                *filename);

/**
 * Adds pool to the configuration tree of the transaction
 *
 * @param tx           spiel transaction
 * @param fid          fid of the pool
 * @param parent       fid of the parent filesystem
 * @param order        pool order
 */
int m0_spiel_pool_add(struct m0_spiel_tx  *tx,
		      const struct m0_fid *fid,
		      const struct m0_fid *parent,
		      uint32_t             order);

/**
 * Adds rack to the configuration tree of the transaction
 *
 * @param tx           spiel transaction
 * @param fid          fid of the rack
 * @param parent       fid of the parent filesystem
 */
int m0_spiel_rack_add(struct m0_spiel_tx  *tx,
		      const struct m0_fid *fid,
		      const struct m0_fid *parent);

/**
 * Adds enclosure to the configuration tree of the transaction
 *
 * @param tx           spiel transaction
 * @param fid          fid of the pool
 * @param parent       fid of the parent rack
 */
int m0_spiel_enclosure_add(struct m0_spiel_tx  *tx,
			   const struct m0_fid *fid,
			   const struct m0_fid *parent);

/**
 * Adds controller to the configuration tree of the transaction
 *
 * @param tx           spiel transaction
 * @param fid          fid of the controller
 * @param parent       fid of the parent enclosure
 * @param node         the node this controller is associated with
 */
int m0_spiel_controller_add(struct m0_spiel_tx  *tx,
			    const struct m0_fid *fid,
			    const struct m0_fid *parent,
			    const struct m0_fid *node);

/**
 * Adds disk to the configuration tree of the transcation
 *
 * @param tx          spiel transaction
 * @param fid         fid of the disk
 * @param parent      fid of the parent controller
 */
int m0_spiel_disk_add(struct m0_spiel_tx  *tx,
		      const struct m0_fid *fid,
		      const struct m0_fid *parent);

/**
 * Adds pool version to the configuration tree of the transaction
 *
 * Pool version is represented as a tree of "v-objects".
 * "V-objects" can be added to the pool version using calls
 * like @ref m0_spiel_rack_v_add(), @ref m0_spiel_enclosure_v_add, etc.
 * After all "V-objects" are added, function @ref m0_spiel_pool_version_done()
 * should be called.
 *
 * Parameter nr_failures is number of allowed HW failures in each failure
 * domain. Currently there are 5 failure domains: pools, racks,
 * enclosures, controllers, disks. So nr_failures_cnt should be 5.
 * nr_failures[0] - number of allowed pool failures. Should be zero.
 * nr_failures[1] - number of allowed rack failures.
 * nr_failures[2] - number of allowed enclosure failures.
 * nr_failures[3] - number of allowed controller failures.
 * nr_failures[4] - number of allowed disk failures.
 *
 * @param tx              spiel transaction
 * @param fid             fid of the pool version
 * @param parent          fid of the parent pool
 * @param nr_failures     allowed failures for each failure domain
 * @param nr_failures_cnt number of elements in nr_failures array
 * @param attrs           attributes specific to layout type
 */
int m0_spiel_pool_version_add(struct m0_spiel_tx     *tx,
			      const struct m0_fid    *fid,
			      const struct m0_fid    *parent,
			      uint32_t               *nr_failures,
			      uint32_t                nr_failures_cnt,
			      struct m0_pdclust_attr *attrs);

/**
 * Adds rack "v-object"
 *
 * @param tx           spiel transaction
 * @param fid          fid of the rack "v-object"
 * @param parent       fid of the parent pool version
 * @param real         fid of the rack this object points to
 */
int m0_spiel_rack_v_add(struct m0_spiel_tx *tx,
			const struct m0_fid *fid,
			const struct m0_fid *parent,
			const struct m0_fid *real);

/**
 * Adds enclosure "v-object"
 *
 * @param tx           spiel transaction
 * @param fid          fid of the enclosure "v-object"
 * @param parent       fid of the parent rack "v-object"
 * @param real         fid of the enclosure this object points to
 */
int m0_spiel_enclosure_v_add(struct m0_spiel_tx  *tx,
			     const struct m0_fid *fid,
			     const struct m0_fid *parent,
			     const struct m0_fid *real);

/**
 * Adds controller "v-object"
 *
 * @param tx           spiel transaction
 * @param fid          fid of the controller "v-object"
 * @param parent       fid of the parent enclosure "v-object"
 * @param real         fid of the enclosure this object points to
 */
int m0_spiel_controller_v_add(struct m0_spiel_tx  *tx,
			      const struct m0_fid *fid,
			      const struct m0_fid *parent,
			      const struct m0_fid *real);

/**
 * Adds disk "v-object"
 *
 * @param tx           spiel transaction
 * @param fid          fid of the disk "v-object"
 * @param parent       fid of the parent controller "v-object"
 * @param real         fid of the disk this object points to
 */
int m0_spiel_disk_v_add(struct m0_spiel_tx  *tx,
			const struct m0_fid *fid,
			const struct m0_fid *parent,
			const struct m0_fid *real);
/**
 * Signals that constructing pool version tree is finished
 *
 * @param tx           spiel transaction
 * @param fid          fid of the pool version
 */
int m0_spiel_pool_version_done(struct m0_spiel_tx  *tx,
			       const struct m0_fid *fid);

/**
 * Deletes element that was previously added to transaction
 * configuration tree.
 *
 * @param tx           spiel transaction
 * @param fid          fid of the object to be deleted
 */
int m0_spiel_element_del(struct m0_spiel_tx *tx, const struct m0_fid *fid);

/**
 * Checks configuration tree contained in transaction. It is valid if each
 * configuration object has state M0_CS_READY and has real parent (if any is
 * required). Valid transaction is ready for dump or commit.
 *
 * @see m0_spiel_tx_dump
 * @see m0_spiel_tx_commit
 *
 * @param tx          spiel transaction
 * @return -EBUSY if an object is not in M0_CS_READY state.
 * @return -ENOENT if an object hasn't real parent.
 */
int m0_spiel_tx_validate(struct m0_spiel_tx *tx);

/**
 * Saves spiel transaction dump to string. Caller is responsible for freeing the
 * string with m0_spiel_tx_str_free().
 * @note Sets transaction's root version number to @b ver_forced.
 * @pre ver_forced != M0_CONF_VER_UNKNOWN
 */
int m0_spiel_tx_to_str(struct m0_spiel_tx *tx,
		       uint64_t            ver_forced,
		       char              **str);
/**
 * Frees string created with m0_spiel_tx_to_str().
 */
void m0_spiel_tx_str_free(char *str);

/**
 * Saves spiel transaction dump to file.
 * @note Sets transaction's root version number to @b ver_forced.
 * @pre ver_forced != M0_CONF_VER_UNKNOWN
 */
int m0_spiel_tx_dump(struct m0_spiel_tx *tx, uint64_t ver_forced,
		     const char *filename);
/**
 * Saves spiel transaction dump to file with error and stub object too.
 * @note Sets transaction's root version number to @b ver_forced.
 * @pre ver_forced != M0_CONF_VER_UNKNOWN
 */
int m0_spiel_tx_dump_debug(struct m0_spiel_tx *tx, uint64_t ver_forced,
			   const char *filename);

/**********************************************************/
/*                 Command interface                      */
/**********************************************************/
/**
 * Starts spiel instance.
 *
 * Schematic code to start spiel using standard mero setup procedure:
 * @code
 * struct m0_mero  mero;
 * struct m0_spiel spiel;
 *
 * m0_cs_init(&mero, ...);
 * m0_cs_setup_env(&mero, ...);
 * m0_cs_start(&mero);
 *
 * m0_spiel_init(&spiel, m0_cs_reqh_get(&mero));
 * m0_spiel_rconfc_start(&spiel);
 * @endcode
 *
 * @param spiel              Spiel instance
 * @param m0_rconfc_exp_cb_t Rconfc expiration callback
 */
int m0_spiel_rconfc_start(struct m0_spiel    *spiel,
			  m0_rconfc_exp_cb_t  exp_cb);

/**
 * Stops spiel instance.
 *
 * @param spiel spiel instance
 */
void m0_spiel_rconfc_stop(struct m0_spiel *spiel);

/**
 * Set spiel command profile fid from string. Profile string pointer may be
 * NULL, and this results in setting the fid to M0_FID0.
 */
int m0_spiel_cmd_profile_set(struct m0_spiel *spiel, const char *profile_str);

/**
 * Initialises mero service
 *
 * @param spl spiel instance
 * @param svc_fid   service fid from configuration DB
 */
int m0_spiel_service_init(struct m0_spiel *spl, const struct m0_fid *svc_fid);

/**
 * Starts mero service
 *
 * @param spl spiel instance
 * @param svc_fid service fid from configuration DB
 */
int m0_spiel_service_start(struct m0_spiel *spl, const struct m0_fid *svc_fid);

/**
 * Stops mero service
 *
 * @param spl spiel instance
 * @param svc_fid service fid from configuration DB
 */
int m0_spiel_service_stop(struct m0_spiel *spl, const struct m0_fid *svc_fid);

/**
 * Checks health status of the mero service
 *
 * @param spl spiel instance
 * @param svc_fid service fid from configuration DB
 * @return value from @ref ::m0_service_health if operation successful @n
 *         negative value if error occurred
 */
int m0_spiel_service_health(struct m0_spiel *spl, const struct m0_fid *svc_fid);

/**
 * Returns status of the mero service
 *
 * @param spl spiel instance
 * @param svc_fid service fid from configuration DB
 * @return 0 and state of the service if operation was successful
 *         negative value if error occurred
*/
int m0_spiel_service_status(struct m0_spiel *spl, const struct m0_fid *svc_fid,
                           int *status);

/**
 * Instructs mero service to stop accepting incoming requests
 *
 * @param spl spiel instance
 * @param svc_fid service fid from configuration DB
 */
int m0_spiel_service_quiesce(struct m0_spiel     *spl,
		             const struct m0_fid *svc_fid);

/**
 * Attaches device to the mero service
 *
 * @param spl spiel instance
 * @param dev_fid device fid from configuration DB
 */
int m0_spiel_device_attach(struct m0_spiel *spl, const struct m0_fid *dev_fid);

/**
 * Attaches device to the mero service and reports device object HA state found
 * during the action on remote side.
 */
int m0_spiel_device_attach_state(struct m0_spiel     *spl,
				 const struct m0_fid *dev_fid,
				 uint32_t            *ha_state);

/**
 * Detaches device from the mero service
 *
 * @param spl spiel instance
 * @param dev_fid device fid from configuration DB
 */
int m0_spiel_device_detach(struct m0_spiel *spl, const struct m0_fid *dev_fid);

/**
 * Format specified device
 *
 * @param spl spiel instance
 * @param dev_fid device fid from configuration DB
 */
int m0_spiel_device_format(struct m0_spiel *spl, const struct m0_fid *dev_fid);

/**
 * Stop process on mero node
 *
 * @param spl spiel instance
 * @param proc_fid process fid from configuration DB
 */
int m0_spiel_process_stop(struct m0_spiel *spl, const struct m0_fid *proc_fid);

/**
 * Re-configures process running on mero node
 * (for example set nicety, memory usage limit, etc.)
 *
 * @param spl spiel instance
 * @param proc_fid process fid from configuration DB
 */
int m0_spiel_process_reconfig(struct m0_spiel     *spl,
			      const struct m0_fid *proc_fid);

/**
 * Checks health status of the mero process
 *
 * @param spl spiel instance
 * @return value from @ref ::m0_health if operation successful @n
 *         negative value if error occurred
 */
int m0_spiel_process_health(struct m0_spiel     *spl,
			    const struct m0_fid *proc_fid);

/**
 * Prepares mero process for stopping
 *
 * @param spl spiel instance
 * @param proc_fid process fid from configuration DB
 */
int m0_spiel_process_quiesce(struct m0_spiel     *spl,
			     const struct m0_fid *proc_fid);

struct m0_spiel_running_svc {
	/* Service FID */
	struct m0_fid  spls_fid;
	/* Service type name */
	char          *spls_name;
};

/**
 * Lists currently running services inside the mero process.
 * Can be used to monitor services and detect service failures.
 *
 * @return number of filled elements in services array on success,
 *         error code otherwise.
 *
 * @param spl            spiel instance
 * @param proc_fid       process fid from configuration DB
 * @param services       array to store running services fid and name,
 *                       see @ref m0_spiel_running_svc
 */
int m0_spiel_process_list_services(struct m0_spiel              *spl,
				   const struct m0_fid          *proc_fid,
				   struct m0_spiel_running_svc **services);

/**
 * Starts pool repair.
 *
 * The command is synchronous. it waits replies from all SNS services that each
 * one receives fop and starts repair. 0 is returned if each service replies
 * with success result code. Spiel client is able to check status of the current
 * repair process by calling of m0_spiel_pool_repair_status command.
 *
 * @param spl            spiel instance
 * @param pool_fid       pool fid from configuration DB
 * @return 0 if all services reply with success result code, otherwise an error
 * code from the first failed service (it replies with error) or confc (an error
 * occurred during read of the configuration database)
 *
 * @see m0_spiel_pool_repair_status
 */
int m0_spiel_pool_repair_start(struct m0_spiel     *spl,
			       const struct m0_fid *pool_fid);

/**
 * Continues pool repair.
 *
 * The command is synchronous. it waits replies from all SNS services that each
 * one receives fop and resumes repair which was paused by
 * m0_spiel_pool_repair_quiesce(). 0 is returned if each service replies
 * with success result code. Spiel client is able to check status of the current
 * repair process by calling of m0_spiel_pool_repair_status command.
 *
 * @param spl            spiel instance
 * @param pool_fid       pool fid from configuration DB
 * @return number of the services if all services reply with success result
 * code, otherwise an error code from the first failed service (it replies with
 * error) or confc (an error occurred during read of the configuration database)
 *
 * @see m0_spiel_pool_repair_status
 * @see m0_spiel_pool_repair_quiesce
 */
int m0_spiel_pool_repair_continue(struct m0_spiel     *spl,
				  const struct m0_fid *pool_fid);

/**
 * Quiesces pool repair.
 *
 * The command is synchronous. it waits replies from all SNS services that each
 * one receives fop and pauses repair. 0 is returned if each service replies
 * with success result code (repair process is in PAUSED state). Spiel client is
 * able to check status of the current repair process by calling of
 * m0_spiel_pool_repair_status command.
 *
 * @param spl            spiel instance
 * @param pool_fid       pool fid from configuration DB
 * @return 0 if all services reply with success result code, otherwise an error
 * code from the first failed service (it replies with error) or confc (an error
 * occurred during read of the configuration database)
 *
 * @see m0_spiel_pool_repair_status
 */
int m0_spiel_pool_repair_quiesce(struct m0_spiel     *spl,
				 const struct m0_fid *pool_fid);

/**
 * Aborts pool repair.
 *
 * The command is synchronous. it waits replies from all SNS services that each
 * one receives fop and aborts repair. 0 is returned if each service replies
 * with success result code (repair process is successfully aborted). Spiel
 * client is able to check status of the current repair process by calling of
 * m0_spiel_pool_repair_status command.
 *
 * @param spl            spiel instance
 * @param pool_fid       pool fid from configuration DB
 * @return 0 if all services reply with success result code, otherwise an error
 * code from the first failed service (it replies with error) or confc (an error
 * occurred during read of the configuration database)
 *
 * @see m0_spiel_pool_repair_status
 */
int m0_spiel_pool_repair_abort(struct m0_spiel     *spl,
			       const struct m0_fid *pool_fid);

/**
 * Gets status pool repair.
 *
 * The command is synchronous. it waits replies from all SNS services.
 *
 * @param spl            spiel instance
 * @param pool_fid       pool fid from configuration DB
 * @param statuses       pointer where statuses of services will be stored
 * @return number of services if all services reply with success result code,
 * otherwise an error code from the first failed service (it replies with error)
 * or confc (an error occurred during read of the configuration database)
 */
int m0_spiel_pool_repair_status(struct m0_spiel             *spl,
				const struct m0_fid         *pool_fid,
				struct m0_spiel_sns_status **statuses);


/**
 * Starts pool rebalance.
 *
 * The command is synchronous. it waits replies from all SNS services that each
 * one receives fop and starts rebalance. 0 is returned if each service replies
 * with success result code. Spiel client is able to check status of the current
 * rebalance process by calling of m0_spiel_pool_rebalance_status command.
 *
 * @param spl            spiel instance
 * @param pool_fid       pool fid from configuration DB
 * @return 0 if all services reply with success result code, otherwise an error
 * code from the first failed service (it replies with error) or confc (an error
 * occurred during read of the configuration database)
 *
 * @see m0_spiel_pool_rebalance_status
 */
int m0_spiel_pool_rebalance_start(struct m0_spiel     *spl,
				  const struct m0_fid *pool_fid);

/**
 * Continues pool rebalance.
 *
 * The command is synchronous. it waits replies from all SNS services that each
 * one receives fop and resumes rebalance which was paused. 0 is returned if
 * each service replies with success result code. Spiel client is able to check
 * status of the current rebalance process by calling of
 * m0_spiel_pool_rebalance_status command.
 *
 * @param spl            spiel instance
 * @param pool_fid       pool fid from configuration DB
 * @return 0 if all services reply with success result code, otherwise an error
 * code from the first failed service (it replies with error) or confc (an error
 * occurred during read of the configuration database)
 *
 * @see m0_spiel_pool_rebalance_status
 * @see m0_spiel_pool_rebalance_quiesce
 */
int m0_spiel_pool_rebalance_continue(struct m0_spiel     *spl,
				     const struct m0_fid *pool_fid);

/**
 * Quiesces pool rebalance.
 *
 * The command is synchronous. it waits replies from all SNS services that each
 * one receives fop and pauses rebalance. 0 is returned if each service replies
 * with success result code (rebalance process is in PAUSED state). Spiel client
 * is able to check status of the current rebalance process by calling of
 * m0_spiel_pool_rebalance_status command.
 *
 * @param spl            spiel instance
 * @param pool_fid       pool fid from configuration DB
 * @return 0 if all services reply with success result code, otherwise an error
 * code from the first failed service (it replies with error) or confc (an error
 * occurred during read of the configuration database)
 *
 * @see m0_spiel_pool_rebalance_status
 */
int m0_spiel_pool_rebalance_quiesce(struct m0_spiel     *spl,
				    const struct m0_fid *pool_fid);

/**
 * Gets status of pool rebalance.
 *
 * The command is synchronous. it waits replies from all SNS services.
 *
 * @param spl            spiel instance
 * @param pool_fid       pool fid from configuration DB
 * @param statuses       pointer where statuses of services will be stored
 * @return number of the servies if all services reply with success result code,
 * otherwise an error code from the first failed service (it replies with error)
 * or confc (an error occurred during read of the configuration database)
 */
int m0_spiel_pool_rebalance_status(struct m0_spiel             *spl,
				   const struct m0_fid         *pool_fid,
				   struct m0_spiel_sns_status **statuses);

/**
 * Aborts pool sns rebalance operation.
 *
 * Aborts ongoing pool sns rebalance operation.
 * Waits until all the sns services notify completion.
 * @note Blocks until operation is copleted.
 *
 * @param spl            spiel instance
 * @param pool_fid       pool identifier
 * @return 0 on successful abort
 *
 */
int m0_spiel_pool_rebalance_abort(struct m0_spiel     *spl,
				  const struct m0_fid *pool_fid);

/**
 * Mero filesystem stats. The stats are collected from all processes of the
 * nodes the filesystem builds on. Space counters include respective counts from
 * all BE segments mdservice operates on, including seg0, as well as spaces from
 * all storage devices ioservice operates on. Total space includes all total
 * spaces no matter what state the storage device is in. Unlike to total, free
 * space includes counts only from storage devices currently known as on-line
 * devices.
 *
 * The stats are collected only from the processes which are known to be online
 * to the moment of m0_spiel_filesystem_stats_fetch() call. The implication is
 * that the sum of IOS and MDS instances present in configuration under
 * filesystem object is reported in m0_fs_stats::fs_svc_total, while the number
 * of polled and replied services is reported in m0_fs_stats::fs_svc_replied. So
 * the fact of having some processes in the cluster being offline, or failed to
 * respond correctly, can be detected by the difference between 'service total'
 * and 'service replied' counters.
 */
struct m0_fs_stats {
	m0_bcount_t fs_free;        /**< fs free bytes               */
	m0_bcount_t fs_total;       /**< fs total bytes              */
	uint32_t    fs_svc_total;   /**< fs total IOS and MDS count  */
	uint32_t    fs_svc_replied; /**< fs services replied to call */
};

/**
 * Fetches stats for filesystem object identified by provided fid. Spiel API
 * internally polls all process instances registered in configuration database
 * under the specified filesystem object.
 *
 * @param[in]  spl       spiel instance, must have profile fid set up to the
 *                       moment of the call
 * @param[in]  fs_fid    filesystem fid
 * @param[out] stats     instance of m0_fs_stats to be filled with resultant
 *                       values. The instance counter values are written only
 *                       in case of success, and must be ignored otherwise.
 *
 * @note Filesystem object is looked up only under configuration profile the
 * spiel object is set up with to the moment of the call. In case filesystem
 * object cannot be found there, no additional search is done, even in case some
 * other profiles exist in the Mero configuration.
 */
int m0_spiel_filesystem_stats_fetch(struct m0_spiel     *spl,
				    const struct m0_fid *fs_fid,
				    struct m0_fs_stats  *stats);

/**
 * A less demanding version of m0_spiel_filesystem_stats_fetch() requiring a
 * properly initialised @ref m0_spiel_core as a "lightweight spiel" instance.
 *
 * @pre spc->spc_rmachine != NULL
 * @pre spc->spc_confc != NULL
 * @pre m0_conf_fid_type(&spc->spc_profile) == &M0_CONF_PROFILE_TYPE
 */
M0_INTERNAL int m0_spiel__fs_stats_fetch(struct m0_spiel_core *spc,
					 const struct m0_fid  *fs_fid,
					 struct m0_fs_stats   *stats);

/**
 * Dumps configuration cache to a string in XC format.
 *
 * Configuration cache is expected to be fully loaded at this point.
 * (Call m0_spiel_confstr() after m0_spiel_rconfc_start(), and you will
 * be fine.)
 *
 * @note If the call succeeds, the user is responsible for freeing
 *       allocated memory with free(*out) (not m0_free()).
 */
int m0_spiel_confstr(struct m0_spiel *spl, char **out);

/** @} end of spiel group */
#endif /* __MERO_SPIEL_SPIEL_H__ */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
/*
 * vim: tabstop=8 shiftwidth=8 noexpandtab textwidth=80 nowrap
 */
