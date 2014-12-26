/* -*- C -*- */
/*
 * COPYRIGHT 2012 XYRATEX TECHNOLOGY LIMITED
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
 * Original author: Anand Vidwansa <Anand_Vidwansa@xyratex.com>
 * Original creation date: 03/22/2011
 */

#pragma once

#ifndef __MERO_IOSERVICE_COB_FOMS_H__
#define __MERO_IOSERVICE_COB_FOMS_H__

/**
 * Phases of cob create/delete state machine.
 */
enum m0_fom_cob_operations_phases {
	M0_FOPH_COB_OPS_PREPARE = M0_FOPH_NR + 1,
	/**
	 * Internally creates/deletes a stob, a cob and adds/removes a record to
	 * or from auxiliary database.
	 * Or this is a getattr/setattr.
	 */
	M0_FOPH_COB_OPS_EXECUTE
};

/**
 * Fom context object for "cob create" and "cob delete" fops.
 * Same structure is used for both type of fops.
 */
struct m0_fom_cob_op {
	/** Stob identifier. */
	struct m0_fid		 fco_stob_fid;
	/** Generic fom object. */
	struct m0_fom		 fco_fom;
	/** Fid of global file. */
	struct m0_fid		 fco_gfid;
	/** Fid of component object. */
	struct m0_fid		 fco_cfid;
	/** Unique cob index in pool. */
	uint32_t                 fco_cob_idx;
	struct m0_stob          *fco_stob;
	bool                     fco_is_done;
	/** FOL rec fragment for create and delete operations. */
	struct m0_fol_frag       fco_fol_frag;
};


M0_INTERNAL int m0_cob_fom_create(struct m0_fop *fop, struct m0_fom **out,
				  struct m0_reqh *reqh);

/**
 * Create the cob for the cob domain.
 */
M0_INTERNAL int m0_cc_cob_setup(struct m0_fom_cob_op     *cc,
				struct m0_cob_domain     *cdom,
				const struct m0_cob_attr *attr,
				struct m0_be_tx	         *ctx);

#endif    /* __MERO_IOSERVICE_COB_FOMS_H__ */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
