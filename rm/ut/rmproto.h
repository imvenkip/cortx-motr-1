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
 * Original author: Dipak Dudhabhate <dipak_dudhabhate@xyratex.com>
 * Original creation date: 06/29/2011
 */
#ifndef __COLIBRI_RM_RMPROTO_H__
#define __COLIBRI_RM_RMPROTO_H__

#include "lib/chan.h"
#include "lib/list.h"
#include "lib/memory.h"
#include "rm/rm.h"
#include "lib/thread.h"
#include "lib/ut.h"
#include "lib/queue.h"

#define NO_OF_OWNERS	5

/**
 * Type of request sent on rpc queue.
 */
enum c2_rm_request_type {
	/* Loan/revoke reply */
	PRO_LOAN_REPLY,
	/* Incoming request(go_out)*/
	PRO_OUT_REQUEST
};

/**
 * This struct is send on rpc queue 
 */
struct c2_rm_req_reply {
	enum c2_rm_request_type type;
	uint64_t sig_id;
        uint64_t reply_id;
        struct c2_rm_incoming in;
        struct c2_queue_link rq_link;
};

/** 
 * Things required for simulation of rpc layer(RM only)
 */
struct c2_queue rpc_queue;
struct c2_mutex rpc_lock;
struct c2_thread rpc_handle;
int rpc_signal;

/** 
 * Information of owners. Data maintained to locate owners
 * to send IN irequest or loan/revoke reply.
 */
struct c2_rm_proto_info {
        uint64_t owner_id;
        uint64_t req_owner_id;
        struct c2_thread rm_handle;
        struct c2_rm_owner *owner;
        struct c2_rm_resource *res;
	struct c2_queue owner_queue;
} rm_info[NO_OF_OWNERS];

void c2_rm_rpc_init(void);
void c2_rm_rpc_fini(void);
void rpc_process(int id);

/* __COLIBRI_RM_RMPROTO_H__ */
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

