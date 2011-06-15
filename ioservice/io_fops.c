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
 * Original author: Anand Vidwansa <Anand_Vidwansa@xyratex.com>
 * Original author: Anup Barve <Anup_Barve@xyratex.com>
 * Original creation date: 03/21/2011
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "io_fops.h"
#include "lib/errno.h"

int c2_io_fop_cob_rwv_fom_init(struct c2_fop *fop, struct c2_fom **m);

/**
 * readv FOP operation vector.
 */
struct c2_fop_type_ops c2_io_cob_readv_ops = {
	.fto_fom_init = c2_io_fop_cob_rwv_fom_init,
	.fto_get_io_fop = c2_io_fop_get_read_fop,
};

/**
 * writev FOP operation vector.
 */
struct c2_fop_type_ops c2_io_cob_writev_ops = {
	.fto_fom_init = c2_io_fop_cob_rwv_fom_init,
	.fto_get_io_fop = c2_io_fop_get_write_fop,
};

/**
 * Init function to initialize readv and writev reply FOMs.
 * Since there is no client side FOMs as of now, this is empty.
 */
static int c2_io_fop_cob_rwv_rep_fom_init(struct c2_fop *fop, struct c2_fom **m)
{
	return 0;
}

/**
 * readv and writev reply FOP operation vector.
 */
struct c2_fop_type_ops c2_io_rwv_rep_ops = {
	.fto_fom_init = c2_io_fop_cob_rwv_rep_fom_init,
};

/* Init function for file create request. */
static int c2_fop_file_create_request(struct c2_fop *fop, struct c2_fom **m)
{
	return 0;
}

/* Ops vector for file create request. */
struct c2_fop_type_ops c2_fop_file_create_ops = {
	.fto_fom_init = c2_fop_file_create_request,
};

/**
 * FOP definitions for readv and writev operations.
 */
C2_FOP_TYPE_DECLARE(c2_fop_cob_readv, "Read request", 
		    c2_io_service_readv_opcode, &c2_io_cob_readv_ops);
C2_FOP_TYPE_DECLARE(c2_fop_cob_writev, "Write request", 
		    c2_io_service_writev_opcode, &c2_io_cob_writev_ops);
C2_FOP_TYPE_DECLARE(c2_fop_file_create, "File Create",
		    19, &c2_fop_file_create_ops);
/**
 * FOP definitions of readv and writev reply FOPs.
 */
C2_FOP_TYPE_DECLARE(c2_fop_cob_writev_rep, "Write reply", 
		    c2_io_service_writev_rep_opcode, &c2_io_rwv_rep_ops);
C2_FOP_TYPE_DECLARE(c2_fop_cob_readv_rep, "Read reply", 
		    c2_io_service_readv_rep_opcode, &c2_io_rwv_rep_ops);

#ifdef __KERNEL__

/** Placeholder API for c2t1fs build. */
int c2_io_fop_cob_rwv_fom_init(struct c2_fop *fop, struct c2_fom **m)
{
	return 0;
}

int c2_io_fop_get_read_fop(struct c2_fop *curr_fop, struct c2_fop **res_fop,
		void *ioseg)
{
	return 0;
}

int c2_io_fop_get_write_fop(struct c2_fop *curr_fop, struct c2_fop **res_fop,
		void *iovec)
{
	return 0;
}

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
