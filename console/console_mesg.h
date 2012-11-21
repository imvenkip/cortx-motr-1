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
 * Original creation date: 09/09/2011
 */

#pragma once

#ifndef __COLIBRI_CONSOLE_MESG_H__
#define __COLIBRI_CONSOLE_MESG_H__

#include "fop/fop.h"
#include "rpc/rpc.h"

/**
 *  Prints name and opcode of FOP.
 *  It can be used to print more info if required.
 */
C2_INTERNAL void c2_cons_fop_name_print(const struct c2_fop_type *ftype);

/**
 * @brief Builds and send FOP using rpc_post and waits for reply.
 *
 * @param fop	   FOP to be send.
 * @param session  RPC connection session.
 * @param deadline Time to to wait for RPC reply.
 */
C2_INTERNAL int c2_cons_fop_send(struct c2_fop *fop,
				 struct c2_rpc_session *session,
				 c2_time_t deadline);

/**
 *  @brief Iterate over FOP fields and print names.
 */
C2_INTERNAL int c2_cons_fop_show(struct c2_fop_type *fopt);

/**
 * @brief Helper function to print list of FOPs.
 */
C2_INTERNAL void c2_cons_fop_list_show(void);

/**
 * @brief Find the fop type equals to opcode and returns.
 *
 * @param opcode FOP opcode.
 *
 * @return c2_fop_type ref. or NULL
 */
C2_INTERNAL struct c2_fop_type *c2_cons_fop_type_find(uint32_t opcode);

/* __COLIBRI_CONSOLE_MESG_H__ */
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

