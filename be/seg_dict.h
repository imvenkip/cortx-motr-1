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
 * Original author: Anatoliy Bilenko <Anatoliy_Bilenko@xyratex.com>
 * Original creation date: 24-Aug-2013
 */


#pragma once

#ifndef __MERO_BE_SEG_DICT_H__
#define __MERO_BE_SEG_DICT_H__


/**
 * @defgroup be
 *
 * @{
 */

struct m0_be_tx;
struct m0_be_seg;
struct m0_sm_group;
struct m0_be_tx_credit;

M0_INTERNAL void m0_be_seg_dict_init(struct m0_be_seg *seg);
M0_INTERNAL int m0_be_seg_dict_lookup(struct m0_be_seg *seg,
				      const char *name,	void **out);

/* tx based dictionary interface */
M0_INTERNAL int m0_be_seg_dict_insert(struct m0_be_seg *seg,
				      struct m0_be_tx  *tx,
				      const char       *name,
				      void             *value);
M0_INTERNAL int m0_be_seg_dict_delete(struct m0_be_seg *seg,
				      struct m0_be_tx  *tx,
				      const char       *name);
M0_INTERNAL void m0_be_seg_dict_create(struct m0_be_seg *seg,
				       struct m0_be_tx  *tx);
M0_INTERNAL void m0_be_seg_dict_destroy(struct m0_be_seg *seg,
					struct m0_be_tx  *tx);

M0_INTERNAL void m0_be_seg_dict_create_credit(const struct m0_be_seg *seg,
					      struct m0_be_tx_credit *accum);
M0_INTERNAL void m0_be_seg_dict_destroy_credit(const struct m0_be_seg *seg,
					       struct m0_be_tx_credit *accum);
M0_INTERNAL void m0_be_seg_dict_insert_credit(const struct m0_be_seg *seg,
					      const char             *name,
					      struct m0_be_tx_credit *accum);
M0_INTERNAL void m0_be_seg_dict_delete_credit(const struct m0_be_seg *seg,
					      const char             *name,
					      struct m0_be_tx_credit *accum);

/* XXX: deprecated sm_group based interface, still here due to outgoing db/db */
M0_INTERNAL int m0_be_seg_dict_create_grp(struct m0_be_seg *seg,
					  struct m0_sm_group *grp);
M0_INTERNAL int m0_be_seg_dict_destroy_grp(struct m0_be_seg *seg,
					   struct m0_sm_group *grp);

/** @} end of be group */

#endif /* __MERO_BE_SEG_DICT_H__ */


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
