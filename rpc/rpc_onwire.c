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
 * Original author: Subhash Arya <subhash_arya@xyratex.com>
 * Original creation date: 06/25/2011
 */

#include "lib/errno.h"
#include "rpc/rpc_onwire.h"

static int slot_ref_encdec(struct c2_bufvec_cursor *cur,
			   struct c2_rpc_slot_ref *slot_ref,
			   enum c2_bufvec_what what);
static int sender_uuid_encdec(struct c2_bufvec_cursor *cur,
			      struct c2_rpc_sender_uuid *uuid,
			      enum c2_bufvec_what what);

/**
    Encodes/decodes the rpc item header into a bufvec
    @param cur Current bufvec cursor position
    @param item RPC item for which the header is to be encoded/decoded
    @param what Denotes type of operation (Encode or Decode)
    @retval 0 (success)
    @retval -errno  (failure)
*/
int c2_rpc_item_header_encdec(struct c2_rpc_item      *item,
			      struct c2_bufvec_cursor *cur,
			      enum c2_bufvec_what      what)
{
	uint64_t		 len;
	int			 rc;
	struct c2_rpc_item_type *item_type;

	C2_PRE(cur != NULL);
	C2_PRE(item != NULL);

	item_type = item->ri_type;
	if (what == C2_BUFVEC_ENCODE)
		len = c2_rpc_item_size(item);

	rc = c2_bufvec_uint64(cur, &len, what) ?:
	     slot_ref_encdec(cur, item->ri_slot_refs, what);
	return rc;
}

static int slot_ref_encdec(struct c2_bufvec_cursor *cur,
			   struct c2_rpc_slot_ref *slot_ref,
			   enum c2_bufvec_what what)
{
	struct c2_rpc_slot_ref    *sref;
	int			   rc;
	int			   slot_ref_cnt;
	int			   i;

	C2_PRE(slot_ref != NULL);
	C2_PRE(cur != NULL);

	/* Currently MAX slot references in sessions is 1. */
	slot_ref_cnt = 1;
	for (i = 0; i < slot_ref_cnt; ++i) {
		sref = &slot_ref[i];
		rc = c2_bufvec_uint64(cur, &sref->sr_verno.vn_lsn, what) ?:
		c2_bufvec_uint64(cur, &sref->sr_sender_id, what) ?:
		c2_bufvec_uint64(cur, &sref->sr_session_id, what) ?:
		c2_bufvec_uint64(cur, &sref->sr_verno.vn_vc, what) ?:
		sender_uuid_encdec(cur, &sref->sr_uuid, what) ?:
		c2_bufvec_uint64(cur, &sref->sr_last_persistent_verno.vn_lsn,
				 what) ?:
		c2_bufvec_uint64(cur,&sref->sr_last_persistent_verno.vn_vc,
				 what) ?:
		c2_bufvec_uint64(cur, &sref->sr_last_seen_verno.vn_lsn, what) ?:
		c2_bufvec_uint64(cur, &sref->sr_last_seen_verno.vn_vc, what) ?:
		c2_bufvec_uint32(cur, &sref->sr_slot_id, what) ?:
		c2_bufvec_uint64(cur, &sref->sr_xid, what) ?:
		c2_bufvec_uint64(cur, &sref->sr_slot_gen, what);
		if (rc != 0)
			return -EFAULT;
	}
	return rc;
}

/** Helper functions to serialize uuid and slot references in rpc item header
    see rpc/rpc2.h */
static int sender_uuid_encdec(struct c2_bufvec_cursor *cur,
			      struct c2_rpc_sender_uuid *uuid,
			      enum c2_bufvec_what what)
{
	return c2_bufvec_uint64(cur, &uuid->su_uuid, what);
}

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 79
 *  scroll-step: 1
 *  End:
 */
