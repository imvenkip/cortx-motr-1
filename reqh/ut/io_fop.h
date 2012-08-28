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
 * Original author: Dmitriy Chumak <dmitriy_chumak@xyratex.com>
 * Original creation date: 10/29/2011
 */

#pragma once

#ifndef __COLIBRI_STOB_IO_FOP_H__
#define __COLIBRI_STOB_IO_FOP_H__

#include "fop/fop.h"

extern struct c2_fop_type c2_stob_io_create_fopt;
extern struct c2_fop_type c2_stob_io_read_fopt;
extern struct c2_fop_type c2_stob_io_write_fopt;

extern struct c2_fop_type c2_stob_io_create_rep_fopt;
extern struct c2_fop_type c2_stob_io_read_rep_fopt;
extern struct c2_fop_type c2_stob_io_write_rep_fopt;

extern struct c2_rpc_item_type c2_stob_create_rpc_item_type;
extern struct c2_rpc_item_type c2_stob_read_rpc_item_type;
extern struct c2_rpc_item_type c2_stob_write_rpc_item_type;

extern struct c2_rpc_item_type c2_stob_create_rep_rpc_item_type;
extern struct c2_rpc_item_type c2_stob_read_rep_rpc_item_type;
extern struct c2_rpc_item_type c2_stob_write_rep_rpc_item_type;

int c2_stob_io_fop_init(void);
void c2_stob_io_fop_fini(void);

#endif /* !__COLIBRI_STOB_IO_FOP_H__ */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
