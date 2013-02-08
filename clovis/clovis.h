/* -*- C -*- */
/*
 * COPYRIGHT 2013 XYRATEX TECHNOLOGY LIMITED
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
 * Original author: Nikita Danilov <nikita_danilov@xyratex.com>
 * Original creation date: 07-Feb-2013
 */


#pragma once

#ifndef __MERO_CLOVIS_CLOVIS_H__
#define __MERO_CLOVIS_CLOVIS_H__


/**
 * @defgroup clovis
 *
 * Clovis overview.
 *
 * Clovis is *the* interface exported by Mero for use by Mero
 * applications. Examples of Mero applications are:
 *
 *     - Mero file system client (m0t1fs);
 *
 *     - Lustre osd-mero module;
 *
 *     - Mero-based block device.
 *
 * Note that FDMI plugins use a separate interface.
 *
 * Clovis provides the following abstractions:
 *
 *     - object (m0_clovis_obj) is an array of fixed-size blocks;
 *
 *     - container (m0_clovis_bag) is a key-value store;
 *
 *     - domain is a collection of objects and containers with a specified
 *       access discipline and certain guaranteed fault-tolerance
 *       characteristics. There are different types of domains, specified by the
 *       enum m0_clovis_domain_type. Initially clovis supports only domains of
 *       M0_CLOVIS_DOMAIN_TYPE_EXCL. Such domains have, at any given moment, at
 *       most one application accessing the domain. This application is called
 *       "domain owner".
 *
 *     - operation (m0_clovis_obj_op, m0_clovis_bag_op) is a process of querying
 *       or updating object or container;
 *
 *     - transaction (m0_clovis_dtx) is a collection of operations atomic in the
 *       face of failures. All operations from a transaction belong to the same
 *       domain.
 *
 * Objects, containers, domains (and internally transactions) have unique
 * identifiers (m0_clovis_id) from disjoint name-spaces (that is, an object, a
 * container and a domain might have the same identifier). Identifier management
 * is up to the application, except for the single reserved identifier for
 * "domain0", see below and for transaction identifiers, which are assigned by
 * the clovis implementation.
 *
 * All clovis entry points are non-blocking: a structure representing object,
 * container, domain, transaction or operation contains an embedded state
 * machine (m0_sm). A call to a clovis function would, if necessary, change the
 * state machine state, initiate some asynchronous activity and immediately
 * return without waiting for the activity to complete. The caller is expected
 * to wait for the state machine state changes using m0_sm interface. Errors are
 * returned through m0_sm::sm_rc.
 *
 * @see https://docs.google.com/a/xyratex.com/document/d/sHUAUkByacMNkDBRAd8-AbA
 *
 * @{
 */

/* import */
#include "lib/types.h"
#include "lib/vec.h"
#include "sm/sm.h"
#include "dtm/dtm.h"

/* export */
struct m0_clovis_domain;
struct m0_clovis_dtx;
struct m0_clovis_obj;
struct m0_clovis_obj_attr;
struct m0_clovis_bag;
struct m0_clovis_bag_attr;
struct m0_clovis_id;
struct m0_clovis_sgsl;
struct m0_clovis_op;

struct m0_clovis_id {
	struct m0_uint128 cid_128;
};

struct m0_clovis_common {
	struct m0_sm        com_sm;
	struct m0_clovis_is com_id;
};

enum m0_clovis_state {
	M0_CS_INIT = 1,
	M0_CS_OPENING,
	M0_CS_CREATING,
	M0_CS_DELETING,
	M0_CS_CLOSING,
	M0_CS_ACTIVE,
	M0_CS_FAILED
};

enum m0_clovis_domain_type {
	M0_CLOVIS_DOMAIN_TYPE_EXCL = 0,
	M0_CLOVIS_DOMAIN_TYPE_NR
};

struct m0_clovis_domain {
	struct m0_clovis_common    cdo_com;
	enum m0_clovis_domain_type cdo_type;
};

struct m0_clovis_dtx {
	struct m0_dtx cdt_dx;
};

struct m0_clovis_obj {
	struct m0_clovis_common   cob_com;
	struct m0_clovis_obj_attr cob_attr;
};

struct m0_clovis_obj_attr {
};

struct m0_clovis_bag {
	struct m0_clovis_common   cba_com;
	struct m0_clovis_bag_attr cba_attr;
};

struct m0_clovis_bag_attr {
};

struct m0_clovis_sgsl {
	struct m0_indexvec csg_ext;
	struct m0_bufvec   csg_buf;
	struct m0_bufvec   csg_chk;
};

enum m0_clovis_op_state {
	M0_CO_INIT = 1,
	M0_CO_ONGOING,
	M0_CO_FAILED,
	M0_CO_COMPLETE
};

struct m0_clovis_op {
	unsigned              co_type;
	struct m0_sm          co_sm;
	struct m0_clovis_obj *co_obj;
	struct m0_clovis_dtx *co_dx;
};

enum m0_clovis_obj_op_type {
	M0_COOT_READ,
	M0_COOT_WRITE,
	M0_COOT_FREE,
	M0_COOT_ALLOC
};

struct m0_clovis_obj_op {
	struct m0_clovis_op         cio_op;
	const struct m0_clovis_sgsl cio_data;
};

enum m0_clovis_bag_op_type {
	M0_CBOT_LOOKUP,
	M0_CBOT_INSERT,
	M0_CBOT_DELETE,
	M0_CBOT_UPDATE,
	M0_CBOT_CURSOR
};

struct m0_clovis_bag_op {
	struct m0_clovis_op  cbo_op;
	struct m0_clovis_rec cbo_rec;
};

M0_EXTERN const struct m0_clovis_id M0_CLOVIS_DOMAIN0_ID;

void m0_clovis_domain_init  (struct m0_clovis_domain *dom,
			     enum m0_clovis_domain_type type,
			     const struct m0_clovis_id *id);
void m0_clovis_domain_fini  (struct m0_clovis_domain *dom);
void m0_clovis_domain_create(struct m0_clovis_domain *dom, struct m0_clovis_dtx *dx);
void m0_clovis_domain_open  (struct m0_clovis_domain *dom);
void m0_clovis_domain_close (struct m0_clovis_domain *dom);
void m0_clovis_domain_delete(struct m0_clovis_domain *dom, struct m0_clovis_dtx *dx);

void m0_clovis_dtx_open (struct m0_clovis_dtx *dx, struct m0_clovis_domain *dom);
void m0_clovis_dtx_close(struct m0_clovis_dtx *dx);
void m0_clovis_dtx_add  (struct m0_clovis_dtx *dx, struct m0_clovis_op *op);
void m0_clovis_dtx_force(struct m0_clovis_dtx *dx);

void m0_clovis_obj_init  (struct m0_clovis_obj *obj, const struct m0_clovis_id *id);
void m0_clovis_obj_fini  (struct m0_clovis_obj *obj);
void m0_clovis_obj_create(struct m0_clovis_obj *obj, struct m0_clovis_dtx *dx);
void m0_clovis_obj_open  (struct m0_clovis_obj *obj);
void m0_clovis_obj_close (struct m0_clovis_obj *obj);
void m0_clovis_obj_delete(struct m0_clovis_obj *obj, struct m0_clovis_dtx *dx);

void m0_clovis_obj_op    (struct m0_clovis_obj_op *op);

void m0_clovis_bag_init  (struct m0_clovis_bag *bag, const struct m0_clovis_id *id);
void m0_clovis_bag_fini  (struct m0_clovis_bag *bag);
void m0_clovis_bag_create(struct m0_clovis_bag *bag, struct m0_clovis_dtx *dx);
void m0_clovis_bag_open  (struct m0_clovis_bag *bag);
void m0_clovis_bag_close (struct m0_clovis_bag *bag);
void m0_clovis_bag_delete(struct m0_clovis_bag *bag, struct m0_clovis_dtx *dx);

void m0_clovis_bag_op    (struct m0_clovis_bag_op *op);
void m0_clovis_cur_next  (struct m0_clovis_bag_op *op);
void m0_clovis_cur_close (struct m0_clovis_bag_op *op);

/** @} end of clovis group */

#endif /* __MERO_CLOVIS_CLOVIS_H__ */


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
