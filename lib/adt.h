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
 * Original author: Nikita Danilov <nikita_danilov@xyratex.com>
 * Original creation date: 04/01/2010
 */

#pragma once

#ifndef __MERO_LIB_ADT_H__
#define __MERO_LIB_ADT_H__

#include "lib/types.h"
#include "lib/cdefs.h"
#include "lib/buf.h" /* XXX: remove through code */
/**
   @defgroup adt Basic abstract data types
   @{
*/

struct m0_stack;

struct m0_stack_link;

M0_INTERNAL void m0_stack_init(struct m0_stack *stack);
M0_INTERNAL void m0_stack_fini(struct m0_stack *stack);
M0_INTERNAL bool m0_stack_is_empty(const struct m0_stack *stack);

M0_INTERNAL void m0_stack_link_init(struct m0_stack_link *stack);
M0_INTERNAL void m0_stack_link_fini(struct m0_stack_link *stack);
M0_INTERNAL bool m0_stack_link_is_in(const struct m0_stack_link *stack);

/** @} end of adt group */


/* __MERO_LIB_ADT_H__ */
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
