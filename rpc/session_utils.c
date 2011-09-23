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
 * Original author: Rohan Puri <Rohan_Puri@xyratex.com>
 *                  Amit Jambure <Amit_Jambure@xyratex.com>
 * Original creation date: 08/24/2011
 */
#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "lib/errno.h"
#include "lib/memory.h"
#include "lib/misc.h"
#include "lib/arith.h"
#include "rpc/session.h"
#include "lib/bitstring.h"
#include "cob/cob.h"
#include "fop/fop.h"
#include "fop/fop_format_def.h"
#include "reqh/reqh.h"

#ifdef __KERNEL__
#include "rpc/session_k.h"
#define printf printk
#else
#include "rpc/session_u.h"
#endif

#include "rpc/session_fops.h"
#include "rpc/session_internal.h"
#include "db/db.h"

/**
   @addtogroup rpc_session

   @{
 */

int c2_rpc_session_module_init(void)
{
        return c2_rpc_session_fop_init();
}
C2_EXPORTED(c2_rpc_session_module_init);

void c2_rpc_session_module_fini(void)
{
        c2_rpc_session_fop_fini();
}
C2_EXPORTED(c2_rpc_session_module_fini);

void c2_rpc_sender_uuid_generate(struct c2_rpc_sender_uuid *u)
{
	/* XXX temporary */
	uint64_t  rnd;

	rnd = c2_time_nanoseconds(c2_time_now()) * 1000;
	u->su_uuid = c2_rnd(~0ULL >> 16, &rnd);
}

int c2_rpc_sender_uuid_cmp(const struct c2_rpc_sender_uuid *u1,
			   const struct c2_rpc_sender_uuid *u2)
{
	return C2_3WAY(u1->su_uuid, u2->su_uuid);
}

/**
   Initialises rpc item and posts it to rpc-layer
 */
int c2_rpc__fop_post(struct c2_fop                *fop,
		     struct c2_rpc_session        *session,
		     const struct c2_rpc_item_ops *ops)
{
	struct c2_rpc_item *item;

	item = &fop->f_item;
	item->ri_session = session;
	item->ri_prio = C2_RPC_ITEM_PRIO_MAX;
	item->ri_deadline = 0;
	item->ri_ops = ops;

	return c2_rpc_post(item);
}

static struct c2_uint128 stob_id_alloc(void)
{
        static struct c2_atomic64 cnt;
	struct c2_uint128         id;
        uint64_t                  millisec;

	/*
	 * TEMPORARY implementation to allocate unique stob id
	 */
	millisec = c2_time_nanoseconds(c2_time_now()) * 1000000;
	c2_atomic64_inc(&cnt);

	id.u_hi = (0xFFFFULL << 48); /* MSB 16 bit set */
	id.u_lo = (millisec << 20) | (c2_atomic64_get(&cnt) & 0xFFFFF);
        return id;
}

int c2_rpc_cob_create_helper(struct c2_cob_domain *dom,
			     struct c2_cob        *pcob,
			     const char           *name,
			     struct c2_cob       **out,
			     struct c2_db_tx      *tx)
{
	struct c2_cob_nskey  *key;
	struct c2_cob_nsrec   nsrec;
	struct c2_cob_fabrec  fabrec;
	struct c2_cob        *cob;
	uint64_t              pfid_hi;
	uint64_t              pfid_lo;
	int                   rc;

	C2_PRE(dom != NULL && name != NULL && out != NULL);

	*out = NULL;

	if (pcob == NULL) {
		pfid_hi = pfid_lo = 1;
	} else {
		pfid_hi = COB_GET_PFID_HI(pcob);
		pfid_lo = COB_GET_PFID_LO(pcob);
	}

	c2_cob_nskey_make(&key, pfid_hi, pfid_lo, name);
	if (key == NULL)
		return -ENOMEM;

	nsrec.cnr_stobid.si_bits = stob_id_alloc();
	nsrec.cnr_nlink = 1;

	/*
	 * Temporary assignment for lsn
	 */
	fabrec.cfb_version.vn_lsn = C2_LSN_RESERVED_NR + 2;
	fabrec.cfb_version.vn_vc = 0;

	rc = c2_cob_create(dom, key, &nsrec, &fabrec, CA_NSKEY_FREE | CA_FABREC,
				&cob, tx);
	if (rc == 0)
		*out = cob;

	return rc;
}

int c2_rpc_cob_lookup_helper(struct c2_cob_domain *dom,
			     struct c2_cob        *pcob,
			     const char           *name,
			     struct c2_cob       **out,
			     struct c2_db_tx      *tx)
{
	struct c2_cob_nskey *key = NULL;
	uint64_t             pfid_hi;
	uint64_t             pfid_lo;
	int                  rc;

	C2_PRE(dom != NULL && name != NULL && out != NULL);

	*out = NULL;
	if (pcob == NULL) {
		pfid_hi = pfid_lo = 1;
	} else {
		pfid_hi = COB_GET_PFID_HI(pcob);
		pfid_lo = COB_GET_PFID_LO(pcob);
	}

	c2_cob_nskey_make(&key, pfid_hi, pfid_lo, name);
	if (key == NULL)
		return -ENOMEM;
	rc = c2_cob_lookup(dom, key, CA_NSKEY_FREE | CA_FABREC, out, tx);

	C2_POST(ergo(rc == 0, *out != NULL));
	return rc;
}

int c2_rpc_root_session_cob_get(struct c2_cob_domain *dom,
				struct c2_cob       **out,
				struct c2_db_tx      *tx)
{
	return c2_rpc_cob_lookup_helper(dom, NULL, root_session_cob_name,
						out, tx);
}

int c2_rpc_root_session_cob_create(struct c2_cob_domain *dom,
				   struct c2_cob       **out,
				   struct c2_db_tx      *tx)
{
	int rc;

	rc = c2_rpc_cob_create_helper(dom, NULL, root_session_cob_name,
						out, tx);
	if (rc == -EEXIST)
		rc = 0;

	return rc;
}

/**
  XXX temporary routine that submits the fop inside item for execution.
 */
void c2_rpc_item_dispatch(struct c2_rpc_item *item)
{
	struct c2_fop *fop;

	printf("Executing %p\n", item);

	fop = c2_rpc_item_to_fop(item);	
        c2_reqh_fop_handle(&c2_rh, fop); 

	/*c2_mutex_lock(&c2_exec_queue_mutex);
	c2_queue_link_init(&item->ri_dummy_qlinkage);
	c2_queue_put(&c2_exec_queue, &item->ri_dummy_qlinkage);
	c2_chan_broadcast(&c2_exec_chan);
	c2_mutex_unlock(&c2_exec_queue_mutex);*/
}

/**
   for dubugging purpose.
 */
/*
#ifndef __KERNEL__
int c2_exec_queue_print(void)
{
	struct c2_queue_link *ql;
	struct c2_rpc_item   *item;
	char                  str_stages[][20] = {
					"INVALID",
					"PAST_COMMITTED",
					"PAST_VOLATILE",
					"IN_PROGRESS",
					"FUTURE"
				};
#define EOQ ((struct c2_queue_link *)8)

	C2_ASSERT(c2_queue_invariant(&c2_exec_queue));

	for (ql = c2_exec_queue.q_head; ql != EOQ; ql = ql->ql_next) {
		item = container_of(ql, struct c2_rpc_item, ri_dummy_qlinkage);
		printf("Q item %p state %s\n", item,
			str_stages[item->ri_stage]);
	}
	return 0;
}
#endif
*/
