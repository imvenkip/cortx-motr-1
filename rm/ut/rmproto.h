/* -*- C -*- */

#ifndef __COLIBRI_RM_RMPROTO_H__
#define __COLIBRI_RM_RMPROTO_H__

#include "lib/chan.h"
#include "lib/list.h"
#include "lib/memory.h"
#include "rm/rm.h"
#include "lib/thread.h"
#include "lib/ut.h"
#include "lib/queue.h"

#define NO_OF_OWNERS	3

enum c2_rm_request_type {
	PRO_LOAN_REPLY,
	PRO_OUT_REQUEST,
	PRO_REQ_FINISH
};

struct c2_rm_req_reply {
	enum c2_rm_request_type type;
	uint64_t sig_id;
        uint64_t reply_id;
        struct c2_rm_incoming in;
        struct c2_rm_right right;
        struct c2_queue_link rq_link;
};

struct c2_queue rpc_queue;
struct c2_mutex rpc_lock;
struct c2_thread rpc_handle;
int rpc_signal;

struct c2_rm_proto_info {
        char name[255];
        uint64_t owner_id;
        uint64_t req_owner_id;
        struct c2_thread rm_handle;
        struct c2_rm_owner *owner;
        struct c2_rm_resource *res;
	struct c2_queue owner_queue;
        struct c2_mutex oq_lock;
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

