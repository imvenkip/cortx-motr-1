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
 * Original author: Carl Braganza <Carl_Braganza@xyratex.com>
 *                  Dave Cohrs <Dave_Cohrs@xyratex.com>
 * Original creation date: 11/16/2011
 */

#ifdef NLX_DEBUG
static void nlx_print_core_ep_addr(const char *pre,
				   const struct nlx_core_ep_addr *cepa)
{
	NLXP("%s: %p nlx_core_ep_addr\n", pre, cepa);
	NLXP("\t    nid = %ld\n", (unsigned long) cepa->cepa_nid);
	NLXP("\t    pid = %d\n",  (unsigned) cepa->cepa_pid);
	NLXP("\t portal = %d\n", (unsigned) cepa->cepa_portal);
	NLXP("\t   tmid = %d\n", (unsigned) cepa->cepa_tmid);
}

static void nlx_print_core_buffer_event(const char *pre,
				const struct nlx_core_buffer_event *lcbev)
{
	NLXP("%s: %p nlx_core_buffer_event\n", pre, lcbev);
	NLXP("\tcbe_buffer_id: %lx\n", (unsigned long) lcbev->cbe_buffer_id);
	NLXP("\t     cbe_time: %lx\n", (unsigned long) lcbev->cbe_time);
	NLXP("\t   cbe_status: %d\n", lcbev->cbe_status);
	NLXP("\t cbe_unlinked: %d\n", (int) lcbev->cbe_unlinked);
	NLXP("\t   cbe_length: %ld\n", (unsigned long) lcbev->cbe_length);
	NLXP("\t   cbe_offset: %ld\n", (unsigned long) lcbev->cbe_offset);
	NLXP("\t   cbe_sender: %ld %d %d %d\n",
	     (unsigned long) lcbev->cbe_sender.cepa_nid,
	     (unsigned) lcbev->cbe_sender.cepa_pid,
	     (unsigned) lcbev->cbe_sender.cepa_portal,
	     (unsigned) lcbev->cbe_sender.cepa_tmid);
}

static void nlx_print_net_buffer_event(const char *pre,
				       const struct c2_net_buffer_event *nbev)
{
	NLXP("%s: %p c2_net_buffer_event\n", pre, nbev);
	NLXP("\t  nbe_time: %lx\n", (unsigned long) nbev->nbe_time);
	NLXP("\tnbe_status: %d\n", nbev->nbe_status);
	NLXP("\tnbe_length: %ld\n", (unsigned long) nbev->nbe_length);
	NLXP("\tnbe_offset: %ld\n", (unsigned long) nbev->nbe_offset);
	if (nbev->nbe_ep != NULL)
		NLXP("\t    nbe_ep: %s\n", nbev->nbe_ep->nep_addr);
	else
		NLXP("\t    nbe_ep: %s\n", "NULL");
	NLXP("\tnbe_buffer: %p\n", nbev->nbe_buffer);
	if (nbev->nbe_buffer != NULL) {
		struct c2_net_buffer *nb = nbev->nbe_buffer;
		NLXP("\t\t  nb_qtype: %d\n", nb->nb_qtype);
		NLXP("\t\t  nb_flags: %lx\n", (unsigned long) nb->nb_flags);
	}
}
#endif

/**
   @addtogroup LNetCore
   @{
 */

static const struct c2_addb_ctx_type nlx_core_domain_addb_ctx = {
	.act_name = "net-lnet-core-domain"
};

static const struct c2_addb_ctx_type nlx_core_buffer_addb_ctx = {
	.act_name = "net-lnet-core-buffer"
};

static const struct c2_addb_ctx_type nlx_core_tm_addb_ctx = {
	.act_name = "net-lnet-core-tm"
};

/**
   Core TM invariant.
   @note Shouldn't require the mutex as it is called from nlx_kcore_eq_cb.
 */
static bool nlx_core_tm_invariant(const struct nlx_core_transfer_mc *lctm)
{
	if (lctm == NULL || lctm->ctm_magic != C2_NET_LNET_CORE_TM_MAGIC)
		return false;
	if (lctm->ctm_mb_counter < C2_NET_LNET_BUFFER_ID_MIN ||
	    lctm->ctm_mb_counter > C2_NET_LNET_BUFFER_ID_MAX)
		return false;
	return true;
}

/**
   Test that the network TM is locked.  Consumer address space only.
   The subroutine takes advantage of the fact that the core data structure
   is known to be embedded in the xo data structure, which keeps a pointer to
   the network TM structure.
   @param lctm LNet core TM pointer.
 */
static bool nlx_core_tm_is_locked(const struct nlx_core_transfer_mc *lctm)
{
	const struct nlx_xo_transfer_mc *xtm;
	if (!nlx_core_tm_invariant(lctm))
		return false;
	xtm = container_of(lctm, struct nlx_xo_transfer_mc, xtm_core);
	if (!nlx_tm_invariant(xtm->xtm_tm))
		return false;
	if (!c2_mutex_is_locked(&xtm->xtm_tm->ntm_mutex))
		return false;
	return true;
}

/**
   Core buffer invariant.
   @note Shouldn't require the mutex as it is called from nlx_kcore_eq_cb.
 */
static bool nlx_core_buffer_invariant(const struct nlx_core_buffer *lcb)
{
	return lcb != NULL && lcb->cb_magic == C2_NET_LNET_CORE_BUF_MAGIC &&
		lcb->cb_buffer_id != 0;
}

int nlx_core_bevq_provision(struct nlx_core_transfer_mc *lctm, size_t need)
{
	size_t have;
	int num_to_alloc;
	int rc = 0;

	C2_PRE(nlx_core_tm_is_locked(lctm));
	C2_PRE(need > 0);

	have = bev_cqueue_size(&lctm->ctm_bevq) - C2_NET_LNET_BEVQ_NUM_RESERVED;
	C2_ASSERT(have >= lctm->ctm_bev_needed);
	num_to_alloc = lctm->ctm_bev_needed + need - have;
	while (num_to_alloc > 0) {
		struct nlx_core_buffer_event *bev;
		rc = nlx_core_new_blessed_bev(lctm, &bev); /* {u,k} specific */
		if (rc != 0)
			break;
		C2_ASSERT(bev->cbe_tm_link.cbl_p_self != 0); /* is blessed */
		bev_cqueue_add(&lctm->ctm_bevq, &bev->cbe_tm_link);
		--num_to_alloc;
	}
	if (rc == 0)
		lctm->ctm_bev_needed += need;
	have = bev_cqueue_size(&lctm->ctm_bevq) - C2_NET_LNET_BEVQ_NUM_RESERVED;
	C2_POST(have >= lctm->ctm_bev_needed);
	return rc;
}

void nlx_core_bevq_release(struct nlx_core_transfer_mc *lctm, size_t release)
{
	C2_PRE(nlx_core_tm_is_locked(lctm));
	C2_PRE(release > 0);
	C2_PRE(lctm->ctm_bev_needed >= release);

	lctm->ctm_bev_needed -= release;
	return;
}

/**
   Helper subroutine to construct the match bit value from its components.
   @param tmid Transfer machine identifier.
   @param counter Buffer counter value.  The value of 0 is reserved for
   the TM receive message queue.
   @see nlx_core_match_bits_decode()
 */
static uint64_t nlx_core_match_bits_encode(uint32_t tmid, uint64_t counter)
{
	uint64_t mb;
	mb = ((uint64_t) tmid << C2_NET_LNET_TMID_SHIFT) |
		(counter & C2_NET_LNET_BUFFER_ID_MASK);
	return mb;
}

/**
   Helper subroutine to decode the match bits into its components.
   @param mb Match bit field.
   @param tmid Pointer to returned Transfer Machine id.
   @param counter Pointer to returned buffer counter value.
   @see nlx_core_match_bits_encode()
 */
static inline void nlx_core_match_bits_decode(uint64_t mb,
					      uint32_t *tmid,
					      uint64_t *counter)
{
	*tmid = (uint32_t) (mb >> C2_NET_LNET_TMID_SHIFT);
	*counter = mb & C2_NET_LNET_BUFFER_ID_MASK;
	return;
}

/**
   Helper subroutine to encode the internal form of a network buffer
   descriptor.
   @param lctm Pointer to the TM core private data.
   @param lcbuf Pointer to the buffer core private data with the cb_match_bits,
   cb_qtype and cb_length fields set.
   @param cbd Pointer to the descriptor structure to set up. The values are
   all little-endian.
 */
static void nlx_core_buf_desc_encode(struct nlx_core_transfer_mc *lctm,
				     struct nlx_core_buffer *lcbuf,
				     struct nlx_core_buf_desc *cbd)
{
	C2_PRE(nlx_core_tm_invariant(lctm));
	C2_PRE(nlx_core_buffer_invariant(lcbuf));

	cbd->cbd_match_bits = __cpu_to_le64(lcbuf->cb_match_bits);

#define CBD_EP(f) cbd->cbd_passive_ep.cepa_ ## f
#define TM_EP(f) lctm->ctm_addr.cepa_ ## f

	CBD_EP(nid)         = __cpu_to_le64(TM_EP(nid));
	CBD_EP(pid)         = __cpu_to_le32(TM_EP(pid));
	CBD_EP(portal)      = __cpu_to_le32(TM_EP(portal));
	CBD_EP(tmid)        = __cpu_to_le32(TM_EP(tmid));

#undef TM_EP
#undef CBD_EP

	cbd->cbd_qtype      = __cpu_to_le32(lcbuf->cb_qtype);
	cbd->cbd_size       = __cpu_to_le64(lcbuf->cb_length);
	cbd->cbd_magic      = __cpu_to_le64(C2_NET_LNET_CORE_NBD_MAGIC);

	return;
}

void nlx_core_buf_match_bits_set(struct nlx_core_transfer_mc *lctm,
				 struct nlx_core_buffer *lcbuf,
				 struct nlx_core_buf_desc *cbd)
{
	C2_PRE(nlx_core_tm_is_locked(lctm));
	C2_PRE(nlx_core_tm_invariant(lctm));
	C2_PRE(nlx_core_buffer_invariant(lcbuf));

	lcbuf->cb_match_bits =
		nlx_core_match_bits_encode(lcbuf->cb_addr.cepa_tmid,
					   lctm->ctm_mb_counter);
	if (++lctm->ctm_mb_counter > C2_NET_LNET_BUFFER_ID_MAX)
		lctm->ctm_mb_counter = C2_NET_LNET_BUFFER_ID_MIN;

	nlx_core_buf_desc_encode(lctm, lcbuf, cbd);

	C2_POST(nlx_core_tm_invariant(lctm));
	C2_POST(nlx_core_buffer_invariant(lcbuf));
	return;
}

void nlx_core_dom_set_debug(struct nlx_core_domain *lcdom, unsigned dbg)
{
	lcdom->_debug_ = dbg;
}

void nlx_core_tm_set_debug(struct nlx_core_transfer_mc *lctm, unsigned dbg)
{
	lctm->_debug_ = dbg;
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
