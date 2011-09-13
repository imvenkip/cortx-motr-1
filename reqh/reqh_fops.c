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
 * Original author: Mandar Sawant <Mandar_Sawant@xyratex.com>
 * Original creation date: 06/21/2011
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "fop/fop.h"
#include "fop/fop_iterator.h"
#include "fop/fop_format_def.h"

#ifdef __KERNEL__
#include "reqh_fops_k.h"
#else

#include "reqh_fops_u.h"
#endif

#include "reqh_fops.ff"

/**
   @addtogroup reqh
   @{
 */

static struct c2_fop_type_ops reqh_err_fop_ops = {
	.fto_fom_init = NULL,
	.fto_execute = NULL,
};


C2_FOP_TYPE_DECLARE(c2_reqh_error_rep, "reqh error reply", 25, &reqh_err_fop_ops);

static struct c2_fop_type *reqh_fops[] = {
	&c2_reqh_error_rep_fopt,
};

void c2_reqh_fop_fini(void)
{
	c2_fop_type_fini_nr(reqh_fops, ARRAY_SIZE(reqh_fops));
}
C2_EXPORTED(c2_reqh_fop_fini);

int c2_reqh_fop_init(void)
{
	int result;
	result = c2_fop_type_build_nr(reqh_fops, ARRAY_SIZE(reqh_fops));
	if (result != 0)
		c2_reqh_fop_fini();
	return result;
}
C2_EXPORTED(c2_reqh_fop_init);

/** @} endgroup reqh */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */

