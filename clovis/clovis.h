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
struct m0_clovis_bag;
struct m0_clovis_bag_conf;
struct m0_clovis_id;
struct m0_clovis_sgsl;
struct m0_clovis_op;

struct m0_clovis_id {
	struct m0_uint128 cid_128;
};

enum m0_clovis_domain_type {
	M0_CLOVIS_DOMAIN_TYPE_EXCL = 0,
	M0_CLOVIS_DOMAIN_TYPE_NR
};

struct m0_clovis_domain {
	struct m0_sm               cdo_sm;
	enum m0_clovis_domain_type cdo_type;
	struct m0_clovis_id        cdo_id;
};

struct m0_clovis_dtx {
	struct m0_dtx cdt_dx;
};

struct m0_clovis_obj {
	struct m0_sm        cob_sm;
	struct m0_clovis_id cob_id;
};

struct m0_clovis_bag {
	struct m0_sm              cco_sm;
	struct m0_clovis_id       cco_id;
	struct m0_clovis_bag_conf cco_conf;
};

struct m0_clovis_bag_conf {
};

struct m0_clovis_sgsl {
	struct m0_indexvec csg_ext;
	struct m0_bufvec   csg_buf;
};

struct m0_clovis_obj_op {
	struct m0_sm                 cio_sm;
	struct m0_clovis_obj        *cio_obj;
	struct m0_clovis_dtx        *cio_dx;
	const struct m0_clovis_sgsl  cio_data;
};

struct m0_clovis_bag_op {
	struct m0_sm          ckv_sm;
	struct m0_clovis_bag *ckv_bag;
	struct m0_clovis_dtx *ckv_dx;
	struct m0_clovis_rec  ckv_rec;
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
void m0_clovis_dtx_wait (struct m0_clovis_dtx *dx);
void m0_clovis_dtx_force(struct m0_clovis_dtx *dx);

void m0_clovis_obj_init  (struct m0_clovis_obj *obj, const struct m0_clovis_id *id);
void m0_clovis_obj_fini  (struct m0_clovis_obj *obj);
void m0_clovis_obj_create(struct m0_clovis_obj *obj, struct m0_clovis_dtx *dx);
void m0_clovis_obj_open  (struct m0_clovis_obj *obj);
void m0_clovis_obj_close (struct m0_clovis_obj *obj);
void m0_clovis_obj_delete(struct m0_clovis_obj *obj, struct m0_clovis_dtx *dx);

void m0_clovis_obj_read  (struct m0_clovis_obj_op *op);
void m0_clovis_obj_write (struct m0_clovis_obj_op *op);
void m0_clovis_obj_free  (struct m0_clovis_obj_op *op);
void m0_clovis_obj_alloc (struct m0_clovis_obj_op *op);

void m0_clovis_bag_init  (struct m0_clovis_bag *bag, const struct m0_clovis_id *id);
void m0_clovis_bag_fini  (struct m0_clovis_bag *bag);
void m0_clovis_bag_create(struct m0_clovis_bag *bag, struct m0_clovis_dtx *dx);
void m0_clovis_bag_open  (struct m0_clovis_bag *bag);
void m0_clovis_bag_close (struct m0_clovis_bag *bag);
void m0_clovis_bag_delete(struct m0_clovis_bag *bag, struct m0_clovis_dtx *dx);

void m0_clovis_bag_lookup(struct m0_clovis_bag_op *op);
void m0_clovis_bag_insert(struct m0_clovis_bag_op *op);
void m0_clovis_bag_delete(struct m0_clovis_bag_op *op);
void m0_clovis_bag_update(struct m0_clovis_bag_op *op);

void m0_clovis_cur_open  (struct m0_clovis_bag_op *op);
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
