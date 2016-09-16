/* -*- C -*- */
/*
 * COPYRIGHT 2016 XYRATEX TECHNOLOGY LIMITED
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
 * Original author: Sining Wu       <sining.wu@seagate.com>
 * Revision:        Pratik Shinde   <pratik.shinde@seagate.com>
 * Original creation date: 24-Aug-2015
 */

#pragma once

#ifndef __MERO_CLOVIS_CLOVIS_IDX_H__
#define __MERO_CLOVIS_CLOVIS_IDX_H__

#include "clovis/clovis.h" /* m0_clovis_entity_opcode */

/**
 * Experimental implementation of Clovis Index API by wrapping the mds
 * operations with a slim layer. This will be replaced by K-V store operations
 * when it is in place.
 *
 * A clovis index is a key-value store.
 *
 * An index stores records, each record consisting of a key and a value. Keys
 * and values within the same index can be of variable size. Keys are ordered by
 * the lexicographic ordering of their bit-level representation. Records are
 * ordered by the key ordering. Keys are unique within an index.
 *
 * There are 4 types of index operations:
 *
 *     - GET: given a set of keys, return the matching records from the index;
 *
 *     - PUT: given a set of records, place them in the index, overwriting
 *       existing records if necessary, inserting new records otherwise;
 *
 *     - DEL: given a set of keys, delete the matching records from the index;
 *
 *     - NEXT: given a set of keys, return the records with the next (in the
 *       ascending key order) keys from the index. (origin definition)

 *     - NEXT: given a start key and the number of K-V pair wanted, return the
 *       records with the next (in the ascending key order) keys from the index.
 *
 * The clovis implementation guarantees that concurrent calls to the same index
 * are linearizable.
 *
 * Clovis doesn't assume any relationship between index and container. A
 * container can have any number of indices and have to manage them itself.
 * In some cases,  multiple containers can even share index.
 *
 */

/** Imported. */
struct m0_clovis_op_idx;

/** Types of index services supported by Clovis. */
enum m0_clovis_idx_service_type {
	M0_CLOVIS_IDX_MOCK,
	M0_CLOVIS_IDX_MERO,
	M0_CLOVIS_IDX_CASS,
	M0_CLOVIS_IDX_MAX_SERVICE_ID
};

/**
 * Query operations for an index service. The operations in this data
 * structure can be devided into 2 groups:
 * (a) Operations over indices: iqo_namei_create/delete/lookup/list.
 * (b) Queries on a specific index: get/put/del/next, see the comments above for
 *     details.
 *
 * Returned value of query operations:
 *     = 0: the query is executed synchronously and returns successfully.
 *     < 0: the query fails.
 *     = 1: the driver successes in launching the query asynchronously.
 *
 * clovis_idx_op_ast_complete()/fail() must be called correspondingly when an
 * index operation is completed successfully or fails. This gives Clovis
 * a chance to take back control and move operation's state forward.
 */
struct m0_clovis_idx_query_ops {
	/* Index operations. */
	int  (*iqo_namei_create)(struct m0_clovis_op_idx *oi);
	int  (*iqo_namei_delete)(struct m0_clovis_op_idx *oi);
	int  (*iqo_namei_lookup)(struct m0_clovis_op_idx *oi);
	int  (*iqo_namei_list)(struct m0_clovis_op_idx *oi);

	/* Query operations. */
	int  (*iqo_get)(struct m0_clovis_op_idx *oi);
	int  (*iqo_put)(struct m0_clovis_op_idx *oi);
	int  (*iqo_del)(struct m0_clovis_op_idx *oi);
	int  (*iqo_next)(struct m0_clovis_op_idx *oi);
};

/** Initialisation and finalisation functions for an index service. */
struct m0_clovis_idx_service_ops {
	int (*iso_init) (void *svc);
	int (*iso_fini) (void *svc);
};

/**
 * Clovis separates the definitions of index service ant its instances(ctx)
 * to allow a Clovis instance to have its own kind of index service.
 */
struct m0_clovis_idx_service {
	struct m0_clovis_idx_service_ops *is_svc_ops;
	struct m0_clovis_idx_query_ops   *is_query_ops;
};

struct m0_clovis_idx_service_ctx {
	struct m0_clovis_idx_service *isc_service;

	/**
	 * isc_config: service specific configurations.
	 * isc_conn  : connection to the index service
	 */
	void                         *isc_svc_conf;
	void                         *isc_svc_inst;
};

/* Configurations for Cassandra cluster. */
struct m0_idx_cass_config {
	char *cc_cluster_ep;   /* Contact point for a Cassandra cluster. */
	char *cc_keyspace;
	int   cc_max_column_family_num;
};

M0_INTERNAL void clovis_idx_op_ast_complete(struct m0_sm_group *grp,
				       struct m0_sm_ast *ast);
M0_INTERNAL void clovis_idx_op_ast_fail(struct m0_sm_group *grp,
					struct m0_sm_ast *ast);


M0_INTERNAL int m0_clovis_idx_op_namei(struct m0_clovis_entity *entity,
				       struct m0_clovis_op **op,
				       enum m0_clovis_entity_opcode opcode);

M0_INTERNAL void m0_clovis_idx_service_config(struct m0_clovis *m0c,
					     int svc_id, void *svc_conf);
M0_INTERNAL void m0_clovis_idx_service_register(int svc_id,
				struct m0_clovis_idx_service_ops *sops,
				struct m0_clovis_idx_query_ops   *qops);
M0_INTERNAL void m0_clovis_idx_services_register(void);

M0_INTERNAL void m0_clovis_idx_mock_register(void);
M0_INTERNAL void m0_clovis_idx_cass_register(void);
M0_INTERNAL void m0_clovis_idx_kvs_register(void);

#endif /* __MERO_CLOVIS_CLOVIS_IDX_H__ */

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
