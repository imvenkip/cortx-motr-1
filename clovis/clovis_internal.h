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
 * Original creation date: 14-Oct-2013
 */

#pragma once

#ifndef __MERO_CLOVIS_CLOVIS_INTERNAL_H__
#define __MERO_CLOVIS_CLOVIS_INTERNAL_H__

void m0_clovis_op_init(struct m0_clovis_op *op,
		       const struct m0_sm_conf *conf,
		       struct m0_clovis_entity *entity);
void m0_clovis_op_fini(struct m0_clovis_op *op);

struct m0_clovis_io_op {
	struct m0_clovis_op ioo_op;
	struct m0_indexvec  ioo_ext;
	struct m0_bufvec    ioo_data;
	struct m0_bufvec    ioo_attr;
	uint64_t            ioo_attr_mask;
};

bool m0_clovis_io_op_invariant(const struct m0_clovis_io_op *iop);

struct m0_clovis_md_op {
	struct m0_clovis_op mdo_op;
	struct m0_bufvec    mdo_key;
	struct m0_bufvec    mdo_val;
	struct m0_bufvec    mdo_chk;
};

bool m0_clovis_io_op_invariant(const struct m0_clovis_io_op *mop);

void m0_clovis_entity_init(struct m0_clovis_obj    *obj,
			   struct m0_clovis_scope  *parent,
			   const struct m0_uint128 *id);

/** @} end of clovis group */

#endif /* __MERO_CLOVIS_CLOVIS_INTERNAL_H__ */

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
