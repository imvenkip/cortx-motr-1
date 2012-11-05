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
 * Original author: Rohan Puri <Rohan_Puri@xyratex.com>
 *                  Amit Jambure <Amit_Jambure@xyratex.com>
 * Original creation date: 08/24/2011
 */

#ifndef __KERNEL__
#include <sys/stat.h>    /* S_ISDIR */
#endif

#define C2_TRACE_SUBSYSTEM C2_TRACE_SUBSYS_RPC
#include "lib/trace.h"
#include "lib/errno.h"
#include "lib/memory.h"
#include "lib/misc.h"
#include "lib/arith.h"
#include "lib/bitstring.h"
#include "lib/finject.h"       /* C2_FI_ENABLED */
#include "cob/cob.h"
#include "fop/fop.h"
#include "reqh/reqh.h"
#include "db/db.h"

#include "rpc/rpc.h"
#include "rpc/rpc_internal.h"

/**
   @addtogroup rpc_session

   @{
 */

int c2_rpc_session_module_init(void)
{
        return c2_rpc_session_fop_init();
}

void c2_rpc_session_module_fini(void)
{
        c2_rpc_session_fop_fini();
}

void c2_rpc_sender_uuid_get(struct c2_rpc_sender_uuid *u)
{
	u->su_uuid = uuid_generate();
}

int c2_rpc_sender_uuid_cmp(const struct c2_rpc_sender_uuid *u1,
			   const struct c2_rpc_sender_uuid *u2)
{
	return C2_3WAY(u1->su_uuid, u2->su_uuid);
}

int c2_rpc__post_locked(struct c2_rpc_item *item);

/**
   Initialises rpc item and posts it to rpc-layer
 */
int c2_rpc__fop_post(struct c2_fop                *fop,
		     struct c2_rpc_session        *session,
		     const struct c2_rpc_item_ops *ops)
{
	struct c2_rpc_item *item;
	int                 rc;

	if (C2_FI_ENABLED("fake_error"))
		return -ETIMEDOUT;

	if (C2_FI_ENABLED("do_nothing"))
		return 0;

	C2_ENTRY("fop: %p, session: %p", fop, session);

	item                = &fop->f_item;
	item->ri_session    = session;
	item->ri_prio       = C2_RPC_ITEM_PRIO_MAX;
	item->ri_deadline   = 0;
	item->ri_ops        = ops;
	item->ri_op_timeout = c2_time_from_now(10, 0);

	rc = c2_rpc__post_locked(item);
	C2_RETURN(rc);
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
	struct c2_cob_fabrec *fabrec;
	struct c2_cob_omgrec  omgrec;
	struct c2_cob        *cob;
	struct c2_fid         pfid;
	struct c2_uint128     stobid;
	int                   rc;

	C2_ENTRY("cob_dom: %p, pcob: %p", dom, pcob);
	C2_PRE(dom != NULL && name != NULL && out != NULL);

	*out = NULL;
	C2_SET0(&nsrec);

	rc = c2_cob_alloc(dom, &cob);
	if (rc)
	        return rc;

	if (pcob == NULL) {
	        pfid.f_container = pfid.f_key = 1;
	} else {
	        pfid = pcob->co_nsrec.cnr_fid;
	}

	rc = c2_cob_nskey_make(&key, &pfid, name, strlen(name));
	if (rc != 0) {
	        c2_cob_put(cob);
		C2_RETURN(rc);
	}

        stobid = stob_id_alloc();
	nsrec.cnr_fid.f_container = stobid.u_hi;
	nsrec.cnr_fid.f_key = stobid.u_lo;
	nsrec.cnr_nlink = 1;

        rc = c2_cob_fabrec_make(&fabrec, NULL, 0);
        if (rc != 0) {
	        c2_cob_put(cob);
		C2_RETURN(rc);
        }

	/*
	 * Temporary assignment for lsn
	 */
	fabrec->cfb_version.vn_lsn = C2_LSN_RESERVED_NR + 2;
	fabrec->cfb_version.vn_vc = 0;

        omgrec.cor_uid = 0;
        omgrec.cor_gid = 0;
        omgrec.cor_mode = S_IFDIR |
                          S_IRUSR | S_IWUSR | S_IXUSR | /* rwx for owner */
                          S_IRGRP | S_IXGRP |           /* r-x for group */
                          S_IROTH | S_IXOTH;            /* r-x for others */

	rc = c2_cob_create(cob, key, &nsrec, fabrec, &omgrec, tx);
	if (rc == 0) {
		*out = cob;
	} else {
	        c2_cob_put(cob);
                c2_free(key);
                c2_free(fabrec);
	}

	C2_RETURN(rc);
}

int c2_rpc_cob_lookup_helper(struct c2_cob_domain *dom,
			     struct c2_cob        *pcob,
			     const char           *name,
			     struct c2_cob       **out,
			     struct c2_db_tx      *tx)
{
	struct c2_cob_nskey *key = NULL;
	struct c2_fid        pfid;
	int                  rc;

	C2_ENTRY("cob_dom: %p, pcob; %p, name: %s", dom, pcob,
		 (char *)name);
	C2_PRE(dom != NULL && name != NULL && out != NULL);

	*out = NULL;
	if (pcob == NULL) {
	        pfid.f_container = pfid.f_key = 1;
	} else {
	        pfid = pcob->co_nsrec.cnr_fid;
	}

	rc = c2_cob_nskey_make(&key, &pfid, name, strlen(name));
	if (rc != 0)
	        C2_RETURN(rc);
	if (key == NULL)
		C2_RETURN(-ENOMEM);
	rc = c2_cob_lookup(dom, key, C2_CA_NSKEY_FREE | C2_CA_FABREC, out, tx);

	C2_POST(ergo(rc == 0, *out != NULL));
	C2_RETURN(rc);
}

int c2_rpc_root_session_cob_get(struct c2_cob_domain *dom,
				struct c2_cob       **out,
				struct c2_db_tx      *tx)
{
	return c2_rpc_cob_lookup_helper(dom, NULL, C2_COB_SESSIONS_NAME,
						out, tx);
}

#ifdef __KERNEL__

int c2_rpc_root_session_cob_create(struct c2_cob_domain *dom,
				   struct c2_db_tx      *tx)
{
	return 0;
}

#else /* !__KERNEL__ */

int c2_rpc_root_session_cob_create(struct c2_cob_domain *dom,
				   struct c2_db_tx      *tx)
{
	int rc;

	if (C2_FI_ENABLED("fake_error"))
		C2_RETURN(-EINVAL);

	rc = c2_cob_domain_mkfs(dom, &C2_COB_SLASH_FID, &C2_COB_SESSIONS_FID, tx);
	if (rc == -EEXIST)
		rc = 0;

	return rc;
}
#endif /* __KERNEL__ */

/**
  XXX temporary routine that submits the fop inside item for execution.
 */
void c2_rpc_item_dispatch(struct c2_rpc_item *item)
{
	struct c2_fop                        *fop;
	struct c2_reqh                       *reqh;
        struct c2_rpc_fop_conn_establish_ctx *ctx;
	struct c2_rpc_machine                *rpcmach;

	C2_ENTRY("item : %p", item);

	 if (c2_rpc_item_is_conn_establish(item)) {

		ctx = container_of(item, struct c2_rpc_fop_conn_establish_ctx,
					cec_fop.f_item);
		C2_ASSERT(ctx != NULL);
		rpcmach = ctx->cec_rpc_machine;
	} else
		rpcmach = item_machine(item);

	C2_ASSERT(rpcmach != NULL);

	reqh = rpcmach->rm_reqh;
	C2_ASSERT(reqh != NULL);

	fop = c2_rpc_item_to_fop(item);
#ifndef __KERNEL__
	c2_reqh_fop_handle(reqh, fop, NULL);
#endif
	C2_LEAVE();
}

/** @} */
