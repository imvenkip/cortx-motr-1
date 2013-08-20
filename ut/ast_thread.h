/* -*- C -*- */
/*
 * COPYRIGHT 2013 XYRATEX TECHNOLOGY LIMITED
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
 * Original author: Valery V. Vorotyntsev <valery_vorotyntsev@xyratex.com>
 * Original creation date: 13-Aug-2013
 */

#pragma once

#ifndef __MERO_UT_AST_THREAD_H__
#define __MERO_UT_AST_THREAD_H__

/**
 * @defgroup XXX
 *
 * XXX FIXME: Using "ast threads" is a very wrong thing to do.
 * We should use m0_fom_simple instead.
 *
 * @{
 */

/* import */
struct m0_sm_group;

extern struct m0_sm_group *XXX_ast_thread_sm_group;	/* XXX_DB_BE */

M0_INTERNAL int m0_ut_ast_thread_start(struct m0_sm_group *grp);

M0_INTERNAL void m0_ut_ast_thread_stop(void);

/** @} end of XXX group */
#endif /* __MERO_UT_AST_THREAD_H__ */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
/*
 * vim: tabstop=8 shiftwidth=8 noexpandtab textwidth=80 nowrap
 */
