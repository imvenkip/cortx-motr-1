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
 * Original author: Anup Barve <Anup_Barve@xyratex.com>
 * Original creation date: 07/07/2011
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "lib/errno.h"
#include "lib/memory.h"
#include "fop/fom.h"
#include "fop/fop.h"
#include "fop/fop_format_def.h"
#include "ping_fop_u.h"
#include "fop/fop_iterator.h"
#include "ping_fop.h"
#include "ping_fom.h"
#include "ping_fop.ff"
#include "lib/errno.h"


/**
   Return size for a fop of type c2_fop_ping;
   @param - Ping fop for which size has to be calculated
 */
uint64_t c2_fop_ping_getsize(struct c2_fop *ping_fop)
{
	uint64_t			 size = 0;
	uint32_t			 count;
	struct c2_fop_ping		 *fp;

	C2_PRE(ping_fop != NULL);
	fp = c2_fop_data(ping_fop);
	count = fp->fp_arr.f_count;
	size = sizeof(count) + sizeof(fp->fp_arr.f_data) * count;
	printf("\nIn Ping get size : %ld\n", size);
	/** Size of fop layout 
	size = fop->f_type->ft_fmt->ftf_layout->fm_sizeof;
	size += sizeof(struct c2_fop_type);
	size += sizeof(struct c2_fop);*/
	return size;
}

uint64_t c2_fop_ping_reply_get_size(struct c2_fop *fop)
{
	uint64_t 	size;

	C2_PRE(fop != NULL);
	size = fop->f_type->ft_fmt->ftf_layout->fm_sizeof;
	printf("\nIn Ping reply get size : %ld\n", size);
	return size;
}
/**
   Return the opcode of given fop.
   @pre fop.f_item.ri_type->rit_ops->rio_io_get_opcode is called.

   @param - fop for which opcode has to be returned
 */
int c2_fop_get_opcode(struct c2_fop *fop)
{
        int opcode = 0;

        C2_PRE(fop != NULL);
	
        opcode = fop->f_type->ft_code;
        return opcode;
}

/**
  return false by default
 */
bool c2_fop_is_rw(struct c2_fop *fop)
{
        C2_PRE(fop != NULL);
        return false;
}

/* Init for ping reply fom */
int c2_fop_ping_fom_init(struct c2_fop *fop, struct c2_fom **m);

/* Ops vector for ping request. */
struct c2_fop_type_ops c2_fop_ping_ops = {
	.fto_fom_init = c2_fop_ping_fom_init,
	.fto_fop_replied = NULL,
	.fto_getsize = c2_fop_ping_getsize,
	.fto_op_equal = NULL, 
	.fto_get_opcode = c2_fop_get_opcode,
	.fto_get_fid = NULL,
	.fto_is_io = c2_fop_is_rw,
	.fto_get_nfragments = NULL,
	.fto_io_coalesce = NULL,
	.fto_io_segment_coalesce = NULL,
};

/* Init for ping reply fom */
int c2_fop_ping_rep_fom_init(struct c2_fop *fop, struct c2_fom **m)
{
	return 0;
}

/* Ops vector for ping reply. */
struct c2_fop_type_ops c2_fop_ping_rep_ops = {
        .fto_fom_init = c2_fop_ping_rep_fom_init,
        .fto_fop_replied = NULL,
        .fto_getsize = c2_fop_ping_reply_get_size,
        //.fto_getsize = c2_fop_ping_getsize,
        .fto_op_equal = NULL,
        .fto_get_opcode = c2_fop_get_opcode,
        .fto_get_fid = NULL,
        .fto_is_io = c2_fop_is_rw,
        .fto_get_nfragments = NULL,
        .fto_io_coalesce = NULL,
        .fto_io_segment_coalesce = NULL,
};

/**
 * FOP definitions for ping fop and its reply 
 */
C2_FOP_TYPE_DECLARE(c2_fop_ping, "Ping",
		c2_fop_ping_opcode, &c2_fop_ping_ops);
C2_FOP_TYPE_DECLARE(c2_fop_ping_rep, "Ping Reply",
		c2_fop_ping_rep_opcode, &c2_fop_ping_rep_ops);


static struct c2_fop_type_format *fmts[] = {
        &c2_fop_ping_arr_tfmt,
};


static struct c2_fop_type *fops[] = {
        &c2_fop_ping_fopt,
        &c2_fop_ping_rep_fopt,
};

void c2_ping_fop_fini(void)
{
        c2_fop_type_fini_nr(fops, ARRAY_SIZE(fops));
}

int c2_ping_fop_init(void)
{
        int result;
	result = c2_fop_type_format_parse_nr(fmts, ARRAY_SIZE(fmts));
        result = c2_fop_type_build_nr(fops, ARRAY_SIZE(fops));
        return result;
}


/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
