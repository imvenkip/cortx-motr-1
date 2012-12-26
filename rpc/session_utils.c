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

#define M0_TRACE_SUBSYSTEM M0_TRACE_SUBSYS_RPC
#include "lib/trace.h"
#include "lib/errno.h"
#include "lib/memory.h"
#include "lib/misc.h"
#include "lib/arith.h"
#include "lib/bitstring.h"
#include "lib/finject.h"       /* M0_FI_ENABLED */
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

M0_INTERNAL int m0_rpc_session_module_init(void)
{
        return m0_rpc_session_fop_init();
}

M0_INTERNAL void m0_rpc_session_module_fini(void)
{
        m0_rpc_session_fop_fini();
}

M0_INTERNAL void m0_rpc_sender_uuid_get(struct m0_rpc_sender_uuid *u)
{
	u->su_uuid = uuid_generate();
}

M0_INTERNAL int m0_rpc_sender_uuid_cmp(const struct m0_rpc_sender_uuid *u1,
				       const struct m0_rpc_sender_uuid *u2)
{
	return M0_3WAY(u1->su_uuid, u2->su_uuid);
}

M0_INTERNAL int m0_rpc__post_locked(struct m0_rpc_item *item);

/**
   Initialises rpc item and posts it to rpc-layer
 */
M0_INTERNAL int m0_rpc__fop_post(struct m0_fop *fop,
				 struct m0_rpc_session *session,
				 const struct m0_rpc_item_ops *ops)
{
	struct m0_rpc_item *item;
	int                 rc;

	if (M0_FI_ENABLED("fake_error"))
		return -ETIMEDOUT;

	if (M0_FI_ENABLED("do_nothing"))
		return 0;

	M0_ENTRY("fop: %p, session: %p", fop, session);

	item                = &fop->f_item;
	item->ri_session    = session;
	item->ri_prio       = M0_RPC_ITEM_PRIO_MAX;
	item->ri_deadline   = 0;
	item->ri_ops        = ops;
	item->ri_op_timeout = m0_time_from_now(10, 0);

	rc = m0_rpc__post_locked(item);
	M0_RETURN(rc);
}

static struct m0_uint128 stob_id_alloc(void)
{
        static struct m0_atomic64 cnt;
	struct m0_uint128         id;
        uint64_t                  millisec;

	/*
	 * TEMPORARY implementation to allocate unique stob id
	 */
	millisec = m0_time_nanoseconds(m0_time_now()) * 1000000;
	m0_atomic64_inc(&cnt);

	id.u_hi = (0xFFFFULL << 48); /* MSB 16 bit set */
	id.u_lo = (millisec << 20) | (m0_atomic64_get(&cnt) & 0xFFFFF);
        return id;
}

M0_INTERNAL int m0_rpc_cob_create_helper(struct m0_cob_domain *dom,
					 const struct m0_cob *pcob,
					 const char *name,
					 struct m0_cob **out,
					 struct m0_db_tx *tx)
{
	struct m0_cob_nskey  *key;
	struct m0_cob_nsrec   nsrec;
	struct m0_cob_fabrec *fabrec;
	struct m0_cob_omgrec  omgrec;
	struct m0_cob        *cob;
	struct m0_fid         pfid;
	struct m0_uint128     stobid;
	int                   rc;

	M0_ENTRY("cob_dom: %p, pcob: %p", dom, pcob);
	M0_PRE(dom != NULL && name != NULL && out != NULL);

	*out = NULL;
	M0_SET0(&nsrec);

	rc = m0_cob_alloc(dom, &cob);
	if (rc)
	        return rc;

	if (pcob == NULL) {
	        m0_fid_set(&pfid, 1, 1);
	} else {
	        pfid = pcob->co_nsrec.cnr_fid;
	}

	rc = m0_cob_nskey_make(&key, &pfid, name, strlen(name));
	if (rc != 0) {
	        m0_cob_put(cob);
		M0_RETURN(rc);
	}

        stobid = stob_id_alloc();
        m0_fid_set(&nsrec.cnr_fid, stobid.u_hi, stobid.u_lo);
	nsrec.cnr_nlink = 1;

        rc = m0_cob_fabrec_make(&fabrec, NULL, 0);
        if (rc != 0) {
	        m0_cob_put(cob);
		M0_RETURN(rc);
        }

	/*
	 * Temporary assignment for lsn
	 */
	fabrec->cfb_version.vn_lsn = M0_LSN_RESERVED_NR + 2;
	fabrec->cfb_version.vn_vc = 0;

        omgrec.cor_uid = 0;
        omgrec.cor_gid = 0;
        omgrec.cor_mode = S_IFDIR |
                          S_IRUSR | S_IWUSR | S_IXUSR | /* rwx for owner */
                          S_IRGRP | S_IXGRP |           /* r-x for group */
                          S_IROTH | S_IXOTH;            /* r-x for others */

	rc = m0_cob_create(cob, key, &nsrec, fabrec, &omgrec, tx);
	if (rc == 0) {
		*out = cob;
	} else {
	        m0_cob_put(cob);
                m0_free(key);
                m0_free(fabrec);
	}

	M0_RETURN(rc);
}

M0_INTERNAL int m0_rpc_cob_lookup_helper(struct m0_cob_domain *dom,
					 struct m0_cob *pcob,
					 const char *name,
					 struct m0_cob **out,
					 struct m0_db_tx *tx)
{
	struct m0_cob_nskey *key = NULL;
	struct m0_fid        pfid;
	int                  rc;

	M0_ENTRY("cob_dom: %p, pcob; %p, name: %s", dom, pcob,
		 (char *)name);
	M0_PRE(dom != NULL && name != NULL && out != NULL);

	*out = NULL;
	if (pcob == NULL) {
	        m0_fid_set(&pfid, 1, 1);
	} else {
	        pfid = pcob->co_nsrec.cnr_fid;
	}

	rc = m0_cob_nskey_make(&key, &pfid, name, strlen(name));
	if (rc != 0)
	        M0_RETURN(rc);
	if (key == NULL)
		M0_RETURN(-ENOMEM);
	rc = m0_cob_lookup(dom, key, M0_CA_NSKEY_FREE | M0_CA_FABREC, out, tx);

	M0_POST(ergo(rc == 0, *out != NULL));
	M0_RETURN(rc);
}

M0_INTERNAL int m0_rpc_root_session_cob_get(struct m0_cob_domain *dom,
					    struct m0_cob **out,
					    struct m0_db_tx *tx)
{
	return m0_rpc_cob_lookup_helper(dom, NULL, M0_COB_SESSIONS_NAME,
						out, tx);
}

#ifdef __KERNEL__

int m0_rpc_root_session_cob_create(struct m0_cob_domain *dom,
				   struct m0_db_tx      *tx)
{
	return 0;
}

#else /* !__KERNEL__ */

int m0_rpc_root_session_cob_create(struct m0_cob_domain *dom,
				   struct m0_db_tx *tx)
{
	int rc;

	if (M0_FI_ENABLED("fake_error"))
		M0_RETURN(-EINVAL);

	rc = m0_cob_domain_mkfs(dom, &M0_COB_SLASH_FID, &M0_COB_SESSIONS_FID, tx);
	if (rc == -EEXIST)
		rc = 0;

	return rc;
}

#endif /* __KERNEL__ */

/**
  XXX temporary routine that submits the fop inside item for execution.
 */
M0_INTERNAL void m0_rpc_item_dispatch(struct m0_rpc_item *item)
{
	M0_ENTRY("item : %p", item);

	if (item->ri_ops != NULL && item->ri_ops->rio_deliver != NULL)
		item->ri_ops->rio_deliver(item->ri_rmachine, item);
	else
		/**
		 * @todo this assumes that the item is a fop.
		 */
		m0_reqh_fop_handle(item->ri_rmachine->rm_reqh,
				   m0_rpc_item_to_fop(item));
	M0_LEAVE();
}

/** @} */
