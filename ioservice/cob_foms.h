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

#ifndef __COLIBRI_IOSERVICE_COB_FOMS_H__
#define __COLIBRI_IOSERVICE_COB_FOMS_H__

/**
 * Common part of cob create and cob delete foms.
 */
struct c2_fom_cob_common {
	/** Generic fom object. */
	struct c2_fom		 cc_fom;
	/** Fid of global file. */
	struct c2_fid		 cc_gfid;
	/** Fid of component object. */
	struct c2_fid		 cc_cfid;
};

/**
 * Fom context object for "cob create" request. Cob create fop is sent to
 * data servers on creation of a global file.
 */
struct c2_fom_cob_create {
	/** Stob Identifier. */
	struct c2_stob_id        fcc_stob_id;
	/** Common part of cob operation foms. */
	struct c2_fom_cob_common fcc_cc;
};

/**
 * Phases of c2_fom_cob_create state machine.
 */
enum c2_fom_cob_create_phases {
	/**
	 * Internally creates a stob, a cob and adds a record to
	 * auxiliary database.
	 */
	FOPH_CC_COB_CREATE = FOPH_NR + 1,
};

/**
 * Fom context object for "cob delete" request. Cob delete fop is sent
 * to data servers on deletion of a global file.
 */
struct c2_fom_cob_delete {
	/** Stob identifier. */
	struct c2_stob_id	 fcd_stobid;
	/** Common part of cob operation foms. */
	struct c2_fom_cob_common fcd_cc;
};

/**
 * Phases of c2_fom_cob_delete state machine.
 */
enum c2_fom_cob_delete_phases {
	/**
	 * Internally deletes the cob, stob and removes the corresponding
	 * record from auxiliary database.
	 */
	FOPH_CD_COB_DEL = FOPH_NR + 1,
};

#endif    /* __COLIBRI_IOSERVICE_COB_FOMS_H__ */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
