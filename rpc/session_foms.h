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
 * Original author: Rohan Puri <Rohan_Puri@xyratex.com>,
 *                  Amit Jambure <Amit_Jambure@xyratex.com>
 * Original creation date: 04/15/2011
 */

#ifndef _C2_RPC_SESSION_FOM_H
#define _C2_RPC_SESSION_FOM_H

#include "fop/fop.h"
#include "fop/fop_format.h"

#include "rpc/session_fops.h"
#ifdef __KERNEL__
#include "rpc/session_k.h"
#else
#include "rpc/session_u.h"
#endif

#include "fop/fom.h"
/**
   @addtogroup rpc_session

   @{
 */

/*
 * FOM to execute "RPC connection create" request
 */

enum c2_rpc_fom_conn_create_phase {
	FOPH_CONN_CREATE = FOPH_NR + 1
};

struct c2_rpc_fom_conn_create {
	struct c2_fom		fcc_gen;
	struct c2_fop		*fcc_fop;
	struct c2_fop		*fcc_fop_rep;
};
extern struct c2_fom_type c2_rpc_fom_conn_create_type;
extern const struct c2_fom_ops c2_rpc_fom_conn_create_ops;

int c2_rpc_fom_conn_create_state(struct c2_fom *);
void c2_rpc_fom_conn_create_fini(struct c2_fom *);

/*
 * FOM to execute "Session Create" request
 */

enum c2_rpc_fom_session_create_phase {
	FOPH_SESSION_CREATE = FOPH_NR + 1
};

struct c2_rpc_fom_session_create {
	struct c2_fom		fsc_gen;
	struct c2_fop		*fsc_fop;
	struct c2_fop		*fsc_fop_rep;
};

extern struct c2_fom_type c2_rpc_fom_session_create_type;
extern const struct c2_fom_ops c2_rpc_fom_session_create_ops;

int c2_rpc_fom_session_create_state(struct c2_fom *);
void c2_rpc_fom_session_create_fini(struct c2_fom *);

/*
 * FOM to execute session terminate request
 */

enum c2_rpc_fom_session_terminate_phase {
	FOPH_SESSION_DESTROYING = FOPH_NR + 1
};

struct c2_rpc_fom_session_terminate {
	struct c2_fom		fst_gen;
	struct c2_fop		*fst_fop;
	struct c2_fop		*fst_fop_rep;
};

extern struct c2_fom_type c2_rpc_fom_session_terminate_type;
extern const struct c2_fom_ops c2_rpc_fom_session_terminate_ops;

int c2_rpc_fom_session_terminate_state(struct c2_fom *);
void c2_rpc_fom_session_terminate_fini(struct c2_fom *);

/*
 * FOM to execute RPC connection terminate request
 */

enum c2_rpc_fom_conn_terminate_phase {
	FOPH_CONN_TERMINATING = FOPH_NR + 1
};

struct c2_rpc_fom_conn_terminate {
	struct c2_fom		fct_gen;
	struct c2_fop		*fct_fop;
	struct c2_fop		*fct_fop_rep;
};

extern struct c2_fom_type c2_rpc_fom_conn_terminate_type;
extern const struct c2_fom_ops c2_rpc_fom_conn_terminate_ops;

int c2_rpc_fom_conn_terminate_state(struct c2_fom *);
void c2_rpc_fom_conn_terminate_fini(struct c2_fom *);

#endif

/** @} end of rpc_session group */
/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */

