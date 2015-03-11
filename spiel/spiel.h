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

#include "fid/fid.h"          /* m0_fid */
#include "conf/schema.h"      /* m0_conf_service_type */
#include "conf/confc.h"       /* m0_confc */

/**
 * @defgroup sspl Spiel
 *
 * Definition of Seagate Software Platform Library for Mero (SPIEL, SSPL).
 *
 * Overview
 * --------
 *
 * Spiel library is used by a "management application" to control Mero:
 *
 *     - to inform Mero about hardware resources it should use, their roles and
 *       arrangement;
 *
 *     - to specify operational characteristics of Mero, such as fault-tolerance
 *       parameters;
 *
 *     - to issue commands to modify the cluster state: start and stop
 *       operation, format storage, etc.
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
 * Configuration data-base
 * -----------------------
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
 *     - a profile is an access path to the cluster. Profiles are used to serve
 *       different storage services from the same hardware. Currently only a
 *       single profile is supported. Profile is the root of configuration
 *       graph;
 *
 *     - a filesystem represents a top-level name space (filesystem-like or
 *       objectstore-like);
 *
 *     - a pool is a collection of hardware resources. Cluster hardware is
 *       divided into pools for administrative purposes (for example, for
 *       security reasons) and to encode fault-tolerance properties;
 *
 *     - a pool version is a list of elements that belonged to a pool at a
 *       certain moment in time. As system evolves, new hardware is added and
 *       old hardware retired, contents of a pool might change (this change is
 *       reflected by creation of a new pool version), but pool identity remains
 *       unchanged;
 *
 *     - a rack of enclosures (cf. "a knot of toads",
 *       http://www.oxforddictionaries.com/words/what-do-you-call-a-group-of);
 *
 *     - an enclosure;
 *
 *     - a controller;
 *
 *     - a storage device: a rotational or solid-state drive;
 *
 *     - a node is something capable or running processes. Controllers are one
 *       type of node, but a cluster can contain other nodes;
 *
 *     - a process is a user-space process or kernel executing services;
 *
 *     - a service is an executable entity that can accept and execute requests;
 *
 *     - in addition, off each pool version hangs off a tree of "v-objects"
 *       (rack-v, enclosure-v and controller-v) that specify which hardware
 *       elements belong to the pool version. A v-object contains a pointer to
 *       the "real object" (rack, enclosure or controller) and the list of
 *       children. Such indirect arrangement makes it possible to have pool
 *       versions sharing hardware.
 *
 * Schema
 * ------
 *
 * @note from http://es-gerrit.xyus.xyratex.com:8080/#/c/4676/4/conf/obj.h
 *
 * @dot
 * digraph x {
 *      edge [arrowhead=open, fontsize=10];
 *
 *      "root" [height=0.15, width=0.15, label=""];
 *      "root" -> "prof";
 *      "prof" -> "fs" [label="filesystem"];
 *      "fs" -> "node" [label="nodes"];
 *      "fs" -> "rack" [label="racks"];
 *      "fs" -> "pool" [label="pools"];
 *      "node" -> "process" [label="processes"];
 *      "process" -> "service" [label="services"];
 *      "service" -> "device" [label="devices"];
 *      "rack" -> "enclosure" [label="enclosures"];
 *      "enclosure" -> "controller" [label="controllers"];
 *      "controller" -> "device" [label="devices"];
 *      "node" -> "controller" [arrowhead=none, arrowtail=none,
 *                              style=dashed, weight=0.0];
 *      "pool" -> "pool version" [label="versions"];
 *      "pool version" -> "rack-v";
 *      "rack-v" -> "rack" [style=dashed];
 *      "rack-v" -> "enclosure-v";
 *      "enclosure-v" -> "enclosure" [style=dashed];
 *      "enclosure-v" -> "controller-v";
 *      "controller-v" -> "controller" [style=dashed];
 * }
 * @enddot
 *
 * Interface
 * ---------
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
 * Invocation
 * ----------
 *
 * Spiel interface is exported from the standard Mero library, which uses Mero
 * networking for communication. As a result, spiel entry points can be invoked
 * on any node in the cluster.
 *
 * @{
 */

struct m0_rpc_machine;
struct m0_pdclust_attr;
struct m0_reqh;

/**
 * Spiel instance context
 */
struct m0_spiel {
	/** RPC machine for network communication */
	struct m0_rpc_machine    *spl_rmachine;

	/** Confd endpoints to communicate with.
	 * Used both in configuration management and command interface */
	const char              **spl_confd_eps;

	/** Confc instance */
	struct m0_confc           spl_confc;
};

/**
 * Start spiel instance. Should be invoked before calling other spiel functions.
 *
 * Schematic code to start spiel using standard mero setup procedure:
 * @code
 * struct m0_mero  mero;
 * struct m0_spiel spiel;
 * const char    *confd_eps[] = { "0@lo:12345:34:1" , NULL };
 *
 * m0_cs_init(&mero, ...);
 * m0_cs_setup_env(&mero, ...);
 * m0_cs_start(&mero);
 *
 * m0_spiel_start(&spiel,
 *                m0_cs_reqh_get(&mero),
 *                confd_eps,
 *                m0_cs_profile_get(&mero));
 * @endcode
 *
 * @param spiel      Spiel instance
 * @param reqh       Request handler
 * @param confd_eps  Network endpoints of confd services. @n
 *                   Endpoints are deep-copied internally
 * @param profile    Configuration profile. It is used for cluster @n
 *                   configuration retrieval by command interface
 */
int m0_spiel_start(struct m0_spiel *spiel,
		   struct m0_reqh  *reqh,
		   const char     **confd_eps,
		   const char      *profile);

/**
 * Stop spiel instance.
 *
 * @param spiel spiel instance
 */
void m0_spiel_stop(struct m0_spiel *spiel);


/**********************************************************/
/*              Configuration management                  */
/**********************************************************/

/**
 * Spiel transaction
 */
struct m0_spiel_tx {
	struct m0_spiel  *spt_spiel;
};

/**
 * Initialize and open spiel transaction.
 *
 * @param spiel spiel instance
 * @param tx spiel transaction
 * @return NULL on error, pointer to the opened transaction on success
 */
struct m0_spiel_tx *m0_spiel_tx_open(struct m0_spiel    *spiel,
		                     struct m0_spiel_tx *tx);

/**
 * Cancel (rollback) spiel transaction
 *
 * @param tx spiel transaction
 */
void m0_spiel_tx_cancel(struct m0_spiel_tx *tx);

/**
 * Commit filled spiel transaction
 *
 * @param tx spiel transaction
 */
int m0_spiel_tx_done(struct m0_spiel_tx *tx);

/**
 * Add profile to the configuration tree of the transaction
 *
 * @param tx  spiel transaction
 * @param fid fid of the profile
 */
int m0_spiel_profile_add(struct m0_spiel_tx *tx, const struct m0_fid *fid);

/**
 * Add filesystem to the configuration tree of the transaction
 *
 * @param tx         spiel transaction
 * @param fid        fid of the filesystem
 * @param parent     fid of the parent profile
 * @param redundancy metadata redundancy count
 * @param rootfid    root's fid of filesystem
 * @param fs_params  NULL-terminated array of command-line like parameters @n
 *                   Parameters are copied, so caller can safely free them.
 */
int m0_spiel_filesystem_add(struct m0_spiel_tx    *tx,
		            const struct m0_fid   *fid,
		            const struct m0_fid   *parent,
		            unsigned               redundancy,
		            const struct m0_fid   *rootfid,
		            const char           **fs_params);

/**
 * Add node to the configuration tree of the transaction
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
 * Add process to the configuration tree of the transaction
 *
 * @param tx         spiel transaction
 * @param fid        fid of the process
 * @param parent     fid of the parent node
 * @param cores      limit on the number of used cores
 * @param memlimit   memory limit for process
 */
int m0_spiel_process_add(struct m0_spiel_tx  *tx,
			 const struct m0_fid *fid,
			 const struct m0_fid *parent,
			 uint32_t             cores,
			 uint32_t             memlimit);

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
 * Add service to the configuration tree of the transaction
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
 * Add service to the configuration tree of the transaction
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
		        const struct m0_fid                       *parent,
		        enum m0_cfg_storage_device_interface_type  iface,
		        enum m0_cfg_storage_device_media_type      media,
		        uint32_t                                   bsize,
		        uint64_t                                   size,
		        uint64_t                                   last_state,
		        uint64_t                                   flags,
		        const char                                *filename);

/**
 * Add pool to the configuration tree of the transaction
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
 * Add rack to the configuration tree of the transaction
 *
 * @param tx           spiel transaction
 * @param fid          fid of the rack
 * @param parent       fid of the parent filesystem
 */
int m0_spiel_rack_add(struct m0_spiel_tx  *tx,
		      const struct m0_fid *fid,
		      const struct m0_fid *parent);

/**
 * Add enclosure to the configuration tree of the transaction
 *
 * @param tx           spiel transaction
 * @param fid          fid of the pool
 * @param parent       fid of the parent rack
 */
int m0_spiel_enclosure_add(struct m0_spiel_tx  *tx,
			   const struct m0_fid *fid,
			   const struct m0_fid *parent);

/**
 * Add controller to the configuration tree of the transaction
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
 * Add pool version to the configuration tree of the transaction
 *
 * Pool version is represented as a tree of "v-objects".
 * "V-objects" can be added to the pool version using calls
 * like @ref m0_spiel_rack_v_add(), @ref m0_spiel_enclosure_v_add, etc.
 * After all "V-objects" are added, function @ref m0_spiel_pool_version_done()
 * should be called.
 *
 * @param tx           spiel transaction
 * @param fid          fid of the pool version
 * @param parent       fid of the parent pool
 * @param attrs        attributes specific to layout type
 */
int m0_spiel_pool_version_add(struct m0_spiel_tx     *tx,
			      const struct m0_fid    *fid,
			      const struct m0_fid    *parent,
			      struct m0_pdclust_attr *attrs);

/**
 * Add rack "v-object"
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
 * Add enclosure "v-object"
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
 * Add controller "v-object"
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
 * Signal that constructing pool version tree is finished
 *
 * @param tx           spiel transaction
 * @param fid          fid of the pool version
 */
int m0_spiel_pool_version_done(struct m0_spiel_tx  *tx,
			       const struct m0_fid *fid);

/**
 * Delete element that was previously added to transaction
 * configuration tree.
 *
 * @param tx           spiel transaction
 * @param fid          fid of the object to be deleted
 */
int m0_spiel_element_del(struct m0_spiel_tx *tx, const struct m0_fid *fid);




/**********************************************************/
/*                 Command interface                      */
/**********************************************************/

/** Health status of some mero resource
 *  @see m0_spiel_service_health, m0_spiel_process_health */
enum m0_spiel_health {
	/** Good health (service is able to process incoming requests,
	 *  process is running, etc.) */
	M0_HEALTH_GOOD    = 0,
	/** Something wrong with the resource */
	M0_HEALTH_BAD     = 1,
};

/**
 * Initialize mero service
 *
 * @param spl spiel instance
 * @param svc_fid   service fid from configuration DB
 */
int m0_spiel_service_init(struct m0_spiel *spl, const struct m0_fid *svc_fid);

/**
 * Start mero service
 *
 * @param spl spiel instance
 * @param svc_fid service fid from configuration DB
 */
int m0_spiel_service_start(struct m0_spiel *spl, const struct m0_fid *svc_fid);

/**
 * Stop mero service
 *
 * @param spl spiel instance
 * @param svc_fid service fid from configuration DB
 */
int m0_spiel_service_stop(struct m0_spiel *spl, const struct m0_fid *svc_fid);

/**
 * Check health status of the mero service
 *
 * @param spl spiel instance
 * @param svc_fid service fid from configuration DB
 * @return value from @ref ::m0_spiel_health if operation successful @n
 *         negative value if error occurred
 */
int m0_spiel_service_health(struct m0_spiel *spl, const struct m0_fid *svc_fid);

/**
 * Tell mero service to stop accepting incoming requests
 *
 * @param spl spiel instance
 * @param svc_fid service fid from configuration DB
 */
int m0_spiel_service_quiesce(struct m0_spiel     *spl,
		             const struct m0_fid *svc_fid);

/**
 * Attach device to the mero service
 *
 * @param spl spiel instance
 * @param dev_fid device fid from configuration DB
 */
int m0_spiel_device_attach(struct m0_spiel *spl, const struct m0_fid *dev_fid);

/**
 * Detach device from the mero service
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
 * Reconfigure process running on mero node
 * (for example set nicety, memory usage limit, etc.)
 *
 * @param spl spiel instance
 * @param proc_fid process fid from configuration DB
 */
int m0_spiel_process_reconfig(struct m0_spiel     *spl,
		              const struct m0_fid *proc_fid);

/**
 * Check health status of the mero process
 *
 * @param spl spiel instance
 * @return value from @ref ::m0_spiel_health if operation successful @n
 *         negative value if error occurred
 */
int m0_spiel_process_health(struct m0_spiel     *spl,
		            const struct m0_fid *proc_fid);

/**
 * Prepare mero process for stopping
 *
 * @param spl spiel instance
 * @param proc_fid process fid from configuration DB
 */
int m0_spiel_process_quiesce(struct m0_spiel     *spl,
		             const struct m0_fid *proc_fid);

/**
 * List currently running services inside the mero process.
 * Can be used to monitor services and detect service failures.
 * Caller provides place to put fids of running services.
 *
 * @param spl            spiel instance
 * @param proc_fid       process fid from configuration DB
 * @param services       array to store running services fids
 * @param services_count number of elements in services array
 */
int m0_spiel_process_list_services(struct m0_spiel      *spl,
		                   const struct m0_fid  *proc_fid,
		                   struct m0_fid        *services,
		                   int                   services_count);

/**
 * Start pool repair
 *
 * @param spl            spiel instance
 * @param pool_fid       pool fid from configuration DB
 */
int m0_spiel_pool_repair_start(struct m0_spiel     *spl,
		               const struct m0_fid *pool_fid);

/**
 * Quiesce pool repair
 *
 * @param spl            spiel instance
 * @param pool_fid       pool fid from configuration DB
 */
int m0_spiel_pool_repair_quiesce(struct m0_spiel     *spl,
		                 const struct m0_fid *pool_fid);
/**
 * Start pool rebalance
 *
 * @param spl            spiel instance
 * @param pool_fid       pool fid from configuration DB
 */
int m0_spiel_pool_rebalance_start(struct m0_spiel     *spl,
		                  const struct m0_fid *pool_fid);

/**
 * Quiesce pool rebalance
 *
 * @param spl            spiel instance
 * @param pool_fid       pool fid from configuration DB
 */
int m0_spiel_pool_rebalance_quiesce(struct m0_spiel     *spl,
		                    const struct m0_fid *pool_fid);

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
