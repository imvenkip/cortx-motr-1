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
 * Original author: Dipak Dudhabhate <dipak_dudhabhate@xyratex.com>
 *                  Anup Barve <anup_barve@xyratex.com>
 * Original creation date: 02/22/2012
 */

#include "lib/bob.h"    /* c2_bob */
#include "lib/misc.h"   /* C2_IN */
#include "lib/errno.h"  /* ENOMEM */
#include "lib/memory.h" /* C2_ALLOC_PTR */

#include "cm/cp.h"
#include "cm/cm.h"

/**
 *   @page DLD-cp DLD Copy Packet
 *
 *   - @ref DLD-cp-ovw
 *   - @ref DLD-cp-def
 *   - @ref DLD-cp-req
 *   - @ref DLD-cp-depends
 *   - @ref DLD-cp-highlights
 *   - @subpage DLD-cp-fspec "Functional Specification" <!-- Note @subpage -->
 *   - @ref DLD-cp-lspec
 *      - @ref DLD-cp-lspec-comps
 *      - @ref DLD-cp-lspec-sc1
 *      - @ref DLD-cp-lspec-state
 *      - @ref DLD-cp-lspec-thread
 *      - @ref DLD-cp-lspec-numa
 *   - @ref DLD-cp-conformance
 *   - @ref DLD-cp-ut
 *   - @ref DLD-cp-st
 *   - @ref DLD-cp-O
 *   - @ref DLD-cp-ref
 *   - @ref DLD-cp-impl-plan
 *
 *
 *   <hr>
 *   @section DLD-cp-ovw Overview
 *
 *   <hr>
 *   @section DLD-cp-def Definitions
 *
 *   <hr>
 *   @section DLD-cp-req Requirements
 *
 *   <hr>
 *   @section DLD-cp-depends Dependencies
 *
 *   <hr>
 *   @section DLD-cp-highlights Design Highlights
 *
 *   <hr>
 *   @section DLD-cp-lspec Logical Specification
 *
 *   - @ref DLD-cp-lspec-comps
 *   - @ref DLD-cp-lspec-sc1
 *      - @ref DLD-cp-lspec-ds1
 *      - @ref DLD-cp-lspec-sub1
 *      - @ref DLDCPDFSInternal  <!-- Note link -->
 *   - @ref DLD-c-lspec-state
 *   - @ref DLD-cp-lspec-thread
 *   - @ref DLD-cp-lspec-numa
 *
 *   @subsection DLD-cp-lspec-comps Component Overview
 *
 *   @subsection DLD-cp-lspec-sc1 Subcomponent design
 *
 *   @subsubsection DLD-cp-lspec-ds1 Subcomponent Data Structures
 *
 *   @subsubsection DLD-cp-lspec-sub1 Subcomponent Subroutines
 *
 *   @subsection DLD-cp-lspec-state State Specification
 *
 *   @subsection DLD-cp-lspec-thread Threading and Concurrency Model
 *
 *   @subsection DLD-cp-lspec-numa NUMA optimizations
 *
 *   <hr>
 *   @section DLD-cp-conformance Conformance
 *
 *   <hr>
 *   @section DLD-cp-ut Unit Tests
 *
 *   <hr>
 *   @section DLD-cp-st System Tests
 *
 *   <hr>
 *   @section DLD-cp-O Analysis
 *
 *   <hr>
 *   @section DLD-cp-ref References
 *
 *   <hr>
 *   @section DLD-cp-impl-plan Implementation Plan
 *
 */

/**
 * @defgroup DLDCPDFSInternal Colibri Sample Module Internals
 *
 * @see @ref DLD-cp and @ref DLD-cp-lspec
 *
 * @{
 */



static const struct c2_fom_type_ops cp_fom_type_ops = {
        .fto_create = NULL
};

static const struct c2_fom_type cp_fom_type = {
        .ft_ops = &cp_fom_type_ops
};

static void cp_fom_fini(struct c2_fom *fom)
{
        struct c2_cm_cp *cp;
        struct c2_cm    *cm;

        cp = container_of(fom, struct c2_cm_cp, c_fom);
        cm = cp->c_cm;
        C2_ASSERT(cm != NULL);
        c2_cm_group_lock(cm);
        c2_cm_cp_fini(cp);
        c2_cm_group_unlock(cm);
}

static size_t cp_fom_locality(const struct c2_fom *fom)
{
        struct c2_cm_cp *cp;

        cp = container_of(fom, struct c2_cm_cp, c_fom);
        C2_PRE(c2_cm_cp_invariant(cp));

        return 0;
}

static int cp_fom_state(struct c2_fom *fom)
{
        struct c2_cm_cp *cp;

        cp = container_of(fom, struct c2_cm_cp, c_fom);
        C2_ASSERT(c2_cm_cp_invariant(cp));

	switch (fom->fo_phase) {
	case CCP_READ:
		return cp->c_ops->co_read(cp);
	case CCP_WRITE:
		return cp->c_ops->co_write(cp);
	case CCP_XFORM:
		return cp->c_ops->co_xform(cp);
	case CCP_SEND:
		return cp->c_ops->co_send(cp);
	case CCP_RECV:
		return cp->c_ops->co_recv(cp);
	case CCP_FINI:
		cp->c_ops->co_fini(cp);
		break;
	default:
		return cp->c_ops->co_state(cp);
	}
}

/** Copy packet FOM operations */
static const struct c2_fom_ops cp_fom_ops = {
        .fo_fini          = cp_fom_fini,
        .fo_state         = cp_fom_state,
        .fo_home_locality = cp_fom_locality
};


/** @} */ /* end internal */

/**
   External documentation can be continued if need be - usually it should
   be fully documented in the header only.
   @addtogroup DLDFS
   @{
 */

bool c2_cm_cp_invariant(struct c2_cm_cp *cp)
{
	return true;
}

void c2_cm_cp_init(struct c2_cm *cm, struct c2_cm_cp *packet)
{
	C2_POST(c2_cm_cp_invariant(packet));
}

void c2_cm_cp_fini(struct c2_cm_cp *packet)
{
	C2_PRE(c2_cm_cp_invariant(packet));
}

void c2_cm_cp_enqueue(struct c2_cm *cm, struct c2_cm_cp *cp)
{
}

/** @} */ /* end-of-DLDFS */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 79
 *  scroll-step: 1
 *  End:
 */
