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
 * Original author: Carl Braganza <Carl_Braganza@xyratex.com>
 *                  Dave Cohrs <Dave_Cohrs@xyratex.com>
 * Original creation date: 11/16/2011
 */

/**
   @addtogroup LNetCore
   @{
 */

static bool nlx_core_tm_invariant(const struct nlx_core_transfer_mc *ctm)
{
	return ctm != NULL && ctm->ctm_magic == C2_NET_LNET_CORE_TM_MAGIC;
}

static bool nlx_core_buffer_invariant(const struct nlx_core_buffer *cb)
{
	return cb != NULL && cb->cb_magic == C2_NET_LNET_CORE_BUF_MAGIC &&
		cb->cb_buffer_id != 0;
}

/**
   Subroutine to provision additional buffer event entries on the
   buffer event queue if needed.

   The subroutine is to be used in the consumer address space, and uses
   a kernel or user space specific allocator subroutine to obtain an
   appropriately blessed entry in the producer space.

   The invoker must lock the transfer machine prior to this call.

   @param ctm Pointer to core TM data structure.
   @param need Number of additional buffer entries required.
   @see nlx_core_new_buffer_event()
 */
static int nlx_core_bevq_provision(struct nlx_core_transfer_mc *ctm, size_t need)
{
	size_t have;
	int num_to_alloc;
	int rc = 0;

	C2_PRE(nlx_core_tm_invariant(ctm));
	C2_PRE(need > 0);

	have = bev_cqueue_size(&ctm->ctm_bevq) - 2;
	C2_ASSERT(have >= ctm->ctm_bev_needed);
	num_to_alloc = ctm->ctm_bev_needed + need - have;
	while (num_to_alloc > 0) {
		struct nlx_core_buffer_event *bev;
		rc = nlx_core_new_buffer_event(ctm, &bev); /* u/k specific */
		if (rc != 0)
			break;
		C2_ASSERT(bev->cbe_tm_link.cbl_p_self != 0); /* is blessed */
		bev->cbe_tm_link.cbl_c_self =
			(nlx_core_opaque_ptr_t) &bev->cbe_tm_link;
		bev_cqueue_add(&ctm->ctm_bevq, &bev->cbe_tm_link);
		--num_to_alloc;
	}
	if (rc == 0)
		ctm->ctm_bev_needed += need;
	have = bev_cqueue_size(&ctm->ctm_bevq) - 2;
	C2_POST(have >= ctm->ctm_bev_needed);
	return rc;
}

/**
   @}
 */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 79
 *  scroll-step: 1
 *  End:
 */
