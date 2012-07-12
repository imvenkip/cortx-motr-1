/* -*- C -*- */
/*
 * COPYRIGHT 2011 XYRATEX TECHNOLOGY LIMITED
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
 * Original creation date: 08/06/2012
 */

#ifndef __COLIBRI_RDWR_FOM_H__
#define __COLIBRI_RDWR_FOM_H__

#include "rdwr_fop.h"
#include "fop/fom_long_lock.h"
/**
 * Object encompassing FOM for rdwr
 * operation and necessary context data
 */
struct c2_fom_rdwr {
	/** Generic c2_fom object. */
        struct c2_fom                    fp_gen;
	/** FOP associated with this FOM. */
        struct c2_fop			*fp_fop;
	struct c2_long_lock_link	 fp_link;
};

/**
 * <b> State Transition function for "rdwr" operation
 *     that executes on data server. </b>
 *  - Send reply FOP to client.
 */
int c2_fom_rdwr_state(struct c2_fom *fom);
size_t c2_fom_rdwr_home_locality(const struct c2_fom *fom);
void c2_fop_rdwr_fom_fini(struct c2_fom *fom);

/* __COLIBRI_RDWR_FOM_H__ */
#endif

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
