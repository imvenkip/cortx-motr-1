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
 * Original creation date: 01/13/2012
 */

/* This file is designed to be included in klnet_core.c. */

static void nlx_kprint_lnet_handle(const char *pre, lnet_handle_any_t h)
{
	char buf[32];
	LNetSnprintHandle(buf, sizeof(buf)-1, h);
	printk("%s: %s\n", pre, buf);
}

static void nlx_kprint_lnet_process_id(const char *pre, lnet_process_id_t p)
{
	printk("%s: NID=%lu PID=%u\n", pre,
	       (long unsigned) p.nid, (unsigned) p.pid);
}

static void nlx_kprint_lnet_md(const char *pre, const lnet_md_t *md)
{
	printk("%s: %p\n", pre, md);
	printk("\t    start: %p\n", md->start);
	printk("\t  options: %x\n", md->options);
	printk("\t   length: %d\n", md->length);
	printk("\tthreshold: %d\n", md->threshold);
	printk("\t max_size: %d\n", md->max_size);
	printk("\t user_ptr: %p\n", md->user_ptr);
	nlx_kprint_lnet_handle("\teq_handle", md->eq_handle);
#if 0
	{
		int i;
		for(i=0; i < kcb->kb_kiov_len; ++i) {
			printk("\t[%d] %p %d %d\n", i,
			       kcb->kb_kiov[i].kiov_page,
			       kcb->kb_kiov[i].kiov_len,
			       kcb->kb_kiov[i].kiov_offset);
		}
	}
#endif
}

static void nlx_kprint_lnet_event(const char *pre, const lnet_event_t *e)
{
	if (e == NULL) {
		printk("%s: <null>\n", pre);
		return;
	}
	printk("%s: %p\n", pre, e);
	nlx_kprint_lnet_process_id("\t   target:", e->target);
	nlx_kprint_lnet_process_id("\tinitiator:", e->target);
	printk("\t    sender: %ld\n", (long unsigned) e->sender);
	printk("\t      type: %d\n", e->type);
	printk("\t  pt_index: %u\n", e->pt_index);
	printk("\tmatch_bits: %lx\n", (long unsigned) e->match_bits);
	printk("\t   rlength: %u\n", e->rlength);
	printk("\t   mlength: %u\n", e->mlength);
	nlx_kprint_lnet_handle("\t md_handle:", e->md_handle);
	printk("\t  hdr_data: %lx\n", (long unsigned) e->hdr_data);
	printk("\t    status: %d\n", e->status);
	printk("\t  unlinked: %d\n", e->unlinked);
	printk("\t    offset: %u\n", e->offset);
	nlx_kprint_lnet_md("\t        md:", &e->md);
}

/**
   @addtogroup KLNetCore
   @{
 */

/**
   Helper subroutine to construct the match bit value from its components.
   @param tmid Transfer machine identifier.
   @param counter Buffer counter value.  The value of 0 is reserved for
   the TM receive message queue.
   @see nlx_kcore_match_bits_decode()
 */
static uint64_t nlx_kcore_match_bits_encode(uint32_t tmid, uint64_t counter)
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
   @see nlx_kcore_match_bits_encode()
 */
static inline void nlx_kcore_match_bits_decode(uint64_t mb,
					       uint32_t *tmid,
					       uint64_t *counter)
{
	*tmid = (uint32_t) (mb >> C2_NET_LNET_TMID_SHIFT);
	*counter = mb & C2_NET_LNET_BUFFER_ID_MASK;
	return;
}

/**
   Helper subroutine to encode header data for LNetPut operations.
   @param tmid Transfer machine id
   @param portal Portal number
   @see nlx_kcore_hdr_data_encode(), nlx_kcore_hdr_data_decode()
 */
static inline uint64_t nlx_kcore_hdr_data_encode_raw(uint32_t tmid,
						     uint32_t portal)
{
	return ((uint64_t) tmid << C2_NET_LNET_TMID_SHIFT) |
		(portal & C2_NET_LNET_PORTAL_MASK);
}

/**
   Helper subroutine to encode header data for LNetPut operations.
   @param lctm Pointer to kcore TM private data.
   @see nlx_kcore_hdr_data_decode(), nlx_kcore_hdr_data_encode_raw()
 */
static uint64_t nlx_kcore_hdr_data_encode(struct nlx_core_transfer_mc *lctm)
{
	struct nlx_core_ep_addr *cepa;

	C2_PRE(nlx_core_tm_invariant(lctm));
	cepa = &lctm->ctm_addr;
	return nlx_kcore_hdr_data_encode_raw(cepa->cepa_tmid, cepa->cepa_portal);
}


/**
   Helper subroutine to decode header data from an LNetPut event.
   @param hdr_data
   @param portal Pointer to portal.
   @param tmid Pointer to transfer machine identifier.
   @see nlx_kcore_hdr_data_encode()
 */
static inline void nlx_kcore_hdr_data_decode(uint64_t hdr_data,
					     uint32_t *portal,
					     uint32_t *tmid)
{
	*portal = (uint32_t) (hdr_data & C2_NET_LNET_PORTAL_MASK);
	*tmid = hdr_data >> C2_NET_LNET_TMID_SHIFT;
	return;
}

/**
   Helper subroutine to fill in the common fields of the lnet_md_t associated
   with a network buffer.
   @param lctm Pointer to kcore TM private data.
   @param lcbuf Pointer to kcore buffer private data with match bits set.
   @param threshold Value for threshold field. Should be at least 1.
   @param max_size Max size value, if not zero. If provided the
   LNET_MD_MAX_SIZE flag is set.
   @param options Optional flags to be set.  If not 0, only LNET_MD_OP_PUT or
   LNET_MD_OP_GET are accepted.
   @param umd Pointer to return structure to be filled in.
 */
static void nlx_kcore_umd_init(struct nlx_core_transfer_mc *lctm,
			       struct nlx_core_buffer *lcbuf,
			       int threshold,
			       int max_size,
			       unsigned options,
			       lnet_md_t *umd)
{
	struct nlx_kcore_transfer_mc *kctm;
	struct nlx_kcore_buffer *kcb;

	C2_PRE(nlx_core_tm_invariant(lctm));
	kctm = lctm->ctm_kpvt;
	C2_PRE(nlx_kcore_tm_invariant(kctm));
	C2_PRE(nlx_core_buffer_invariant(lcbuf));
	kcb = lcbuf->cb_kpvt;
	C2_PRE(nlx_kcore_buffer_invariant(kcb));
	C2_PRE(threshold > 0);
	C2_PRE(kcb->kb_kiov_len > 0);
	C2_PRE(max_size >= 0);
	C2_PRE(options == 0 ||
	       options == LNET_MD_OP_PUT ||
	       options == LNET_MD_OP_GET);

	C2_SET0(umd);
	umd->options = options;
	umd->start = kcb->kb_kiov;
	umd->options |= LNET_MD_KIOV;
	umd->length = kcb->kb_kiov_len;
	umd->threshold = threshold;
	if (max_size != 0) {
		umd->max_size = max_size;
		umd->options |= LNET_MD_MAX_SIZE;
	}
	umd->user_ptr = lcbuf;
	umd->eq_handle = kctm->ktm_eqh;
	NLXDBG(lctm, 1, nlx_kprint_lnet_md("umd init", umd));
}

/**
   Helper subroutine to adjust the length of the kiov vector in a UMD
   to match a specified byte length.
   This is needed for SEND and active buffer operations.
   Restore the kiov with nlx_kcore_kiov_restore_length().
   @param lctm Pointer to kcore TM private data.
   @param lcbuf Pointer to kcore buffer private data with match bits set.
   @param umd Pointer to the UMD.
   @param bytes The byte count desired.
   @param nlx_kcore_kiov_restore_length()
   @post kcb->kb_kiov_adj_idx >= 0
   @post kcb->kb_kiov_adj_idx < kcb->kb_kiov_len
   @post nlx_kcore_kiov_invariant(umd->start, umd->length)
 */
static void nlx_kcore_kiov_adjust_length(struct nlx_core_transfer_mc *lctm,
					 struct nlx_core_buffer *lcbuf,
					 lnet_md_t *umd,
					 c2_bcount_t bytes)
{
	struct nlx_kcore_buffer *kcb;
	size_t num;
	unsigned last;

	C2_PRE(umd->start != NULL);
	C2_PRE(umd->options & LNET_MD_KIOV);
	C2_PRE(umd->length > 0);
	C2_PRE(nlx_core_tm_invariant(lctm));
	C2_PRE(nlx_core_buffer_invariant(lcbuf));
	kcb = lcbuf->cb_kpvt;
	C2_PRE(nlx_kcore_buffer_invariant(kcb));
	C2_PRE(umd->start == kcb->kb_kiov);

	num = nlx_kcore_num_kiov_entries_for_bytes((lnet_kiov_t *) umd->start,
						   umd->length, bytes, &last);
	NLXDBGP(lctm, 2, "%p: buf:%p size:%ld vec:%lu/%lu loff:%u\n",
		lctm, lcbuf, (unsigned long) bytes,
		(unsigned long) num, (unsigned long) umd->length, last);
	kcb->kb_kiov_adj_idx = num - 1;
	C2_POST(kcb->kb_kiov_adj_idx >= 0);
	C2_POST(kcb->kb_kiov_adj_idx < kcb->kb_kiov_len);
	kcb->kb_kiov_orig_len = kcb->kb_kiov[kcb->kb_kiov_adj_idx].kiov_len;
	kcb->kb_kiov[kcb->kb_kiov_adj_idx].kiov_len = last;
	umd->length = num;
	C2_POST(nlx_kcore_kiov_invariant(umd->start, umd->length));
	return;
}

/**
   Helper subroutine to restore the original length of the buffer's kiov.
   @param lctm Pointer to kcore TM private data.
   @param lcbuf Pointer to kcore buffer private data with match bits set.
   @see nlx_kcore_kiov_adjust_length()
   @pre kcb->kb_kiov_adj_idx >= 0
   @pre kcb->kb_kiov_adj_idx < kcb->kb_kiov_len
   @post nlx_kcore_kiov_invariant(kcb->kb_kiov, kcb->kb_kiov_len)
*/
static void nlx_kcore_kiov_restore_length(struct nlx_core_transfer_mc *lctm,
					  struct nlx_core_buffer *lcbuf)
{
	struct nlx_kcore_buffer *kcb;

	C2_PRE(nlx_core_tm_invariant(lctm));
	C2_PRE(nlx_core_buffer_invariant(lcbuf));
	kcb = lcbuf->cb_kpvt;
	C2_PRE(nlx_kcore_buffer_invariant(kcb));
	C2_PRE(kcb->kb_kiov_adj_idx >= 0);
	C2_PRE(kcb->kb_kiov_adj_idx < kcb->kb_kiov_len);
	kcb->kb_kiov[kcb->kb_kiov_adj_idx].kiov_len = kcb->kb_kiov_orig_len;
	C2_POST(nlx_kcore_kiov_invariant(kcb->kb_kiov, kcb->kb_kiov_len));
	return;
}

/**
   Helper subroutine to attach a network buffer to the match list
   associated with the transfer machine's portal.
   - The ME entry created is put at the end of the match list.
   - The ME and MD are set up to automatically unlink.
   - The MD handle is set in the struct nlx_kcore_buffer::kb_mdh field.
   - Sets the kb_ktm field in the KCore buffer private data.
   @param lctm Pointer to kcore TM private data.
   @param lcbuf Pointer to kcore buffer private data with match bits set.
   @param umd Pointer to lnet_md_t structure for the buffer, with appropriate
   values set for the desired operation.
   @post ergo(rc == 0, LNetHandleIsValid(kcb->kb_mdh))
   @post ergo(rc == 0, kcb->kb_ktm == kctm)
 */
static int nlx_kcore_LNetMDAttach(struct nlx_core_transfer_mc *lctm,
				  struct nlx_core_buffer *lcbuf,
				  lnet_md_t *umd)
{
	struct nlx_kcore_transfer_mc *kctm;
	struct nlx_kcore_buffer *kcb;
	lnet_handle_me_t meh;
	lnet_process_id_t id;
	int rc;

	C2_PRE(nlx_core_tm_invariant(lctm));
	kctm = lctm->ctm_kpvt;
	C2_PRE(nlx_kcore_tm_invariant(kctm));
	C2_PRE(nlx_core_buffer_invariant(lcbuf));
	kcb = lcbuf->cb_kpvt;
	C2_PRE(nlx_kcore_buffer_invariant(kcb));
	C2_PRE(lcbuf->cb_match_bits != 0);

	id.nid = lctm->ctm_addr.cepa_nid;
	id.pid = lctm->ctm_addr.cepa_pid;
	rc = LNetMEAttach(lctm->ctm_addr.cepa_portal, id,
			  lcbuf->cb_match_bits, 0,
			  LNET_UNLINK, LNET_INS_AFTER, &meh);
	if (rc != 0)
		return rc;
	C2_POST(!LNetHandleIsInvalid(meh));

	rc = LNetMDAttach(meh, *umd, LNET_UNLINK, &kcb->kb_mdh);
	if (rc == 0) {
		kcb->kb_ktm = kctm;
		NLXDBG(lctm, 1, nlx_kprint_lnet_handle("MDAttach", kcb->kb_mdh));
	} else {
		int trc = LNetMEUnlink(meh);
		NLXDBG(lctm, 1, NLXP("LNetMDAttach: %d\n", rc));
		NLXDBG(lctm, 1, NLXP("LNetMEUnlink: %d\n", trc));
		C2_ASSERT(trc == 0);
		LNetInvalidateHandle(&kcb->kb_mdh);
	}

	C2_POST(ergo(rc == 0, !LNetHandleIsInvalid(kcb->kb_mdh)));
	C2_POST(ergo(rc == 0, kcb->kb_ktm == kctm));
	return rc;
}

/**
   Helper subroutine to unlink an MD.
   @param lctm Pointer to kcore TM private data.
   @param lcbuf Pointer to kcore buffer private data with kb_mdh set.
   @pre kcb->kb_mdh set (may or may not be valid by the time the call is made).
   @pre kcb->kb_ktm == kctm
 */
static int nlx_kcore_LNetMDUnlink(struct nlx_core_transfer_mc *lctm,
				   struct nlx_core_buffer *lcbuf)
{
	struct nlx_kcore_transfer_mc *kctm;
	struct nlx_kcore_buffer *kcb;
	int rc;

	C2_PRE(nlx_core_tm_invariant(lctm));
	kctm = lctm->ctm_kpvt;
	C2_PRE(nlx_kcore_tm_invariant(kctm));
	C2_PRE(nlx_core_buffer_invariant(lcbuf));
	kcb = lcbuf->cb_kpvt;
	C2_PRE(nlx_kcore_buffer_invariant(kcb));
	C2_PRE(kcb->kb_ktm == kctm);
	rc = LNetMDUnlink(kcb->kb_mdh);
	NLXDBG(lctm, 1, NLXP("LNetMDUnlink: %d\n", rc));
	return rc;
}

/**
   Helper subroutine to send a buffer to a remote destination using @c LNetPut().
   - The MD is set up to automatically unlink.
   - The MD handle is set in the struct nlx_kcore_buffer::kb_mdh field.
   - The TM's portal and TMID are encoded in the header data.
   - Sets the kb_ktm field in the KCore buffer private data.
   @param lctm Pointer to kcore TM private data.
   @param lcbuf Pointer to kcore buffer private data with match bits set, and
   the address of the remote destination in struct nlx_core_buffer::cb_addr.
   @param umd Pointer to lnet_md_t structure for the buffer, with appropriate
   values set for the desired operation.
   @post ergo(rc == 0, LNetHandleIsValid(kcb->kb_mdh))
   @post ergo(rc == 0, kcb->kb_ktm == kctm)
   @see nlx_kcore_hdr_data_encode(), nlx_kcore_hdr_data_decode()
 */
static int nlx_kcore_LNetPut(struct nlx_core_transfer_mc *lctm,
			     struct nlx_core_buffer *lcbuf,
			     lnet_md_t *umd)
{
	struct nlx_kcore_transfer_mc *kctm;
	struct nlx_kcore_buffer *kcb;
	lnet_process_id_t target;
	int rc;

	C2_PRE(nlx_core_tm_invariant(lctm));
	kctm = lctm->ctm_kpvt;
	C2_PRE(nlx_kcore_tm_invariant(kctm));
	C2_PRE(nlx_core_buffer_invariant(lcbuf));
	kcb = lcbuf->cb_kpvt;
	C2_PRE(nlx_kcore_buffer_invariant(kcb));
	C2_PRE(lcbuf->cb_match_bits != 0);

	rc = LNetMDBind(*umd, LNET_UNLINK, &kcb->kb_mdh);
	if (rc != 0)
		return rc;

	target.nid = lcbuf->cb_addr.cepa_nid;
	target.pid = lcbuf->cb_addr.cepa_pid;
	rc = LNetPut(LNET_NID_ANY, kcb->kb_mdh, LNET_NOACK_REQ,
		     target, lcbuf->cb_addr.cepa_portal,
		     lcbuf->cb_match_bits, 0,
		     nlx_kcore_hdr_data_encode(lctm));
	if (rc == 0) {
		kcb->kb_ktm = kctm;
		NLXDBG(lctm, 1,
		       nlx_kprint_lnet_handle("LNetMDBind", kcb->kb_mdh));
	} else {
		int trc = LNetMDUnlink(kcb->kb_mdh);
		NLXDBG(lctm, 1, NLXP("LNetPut: %d\n", rc));
		NLXDBG(lctm, 1, NLXP("LNetMDUnlink: %d\n", trc));
		C2_ASSERT(trc == 0);
		LNetInvalidateHandle(&kcb->kb_mdh);
	}

	C2_POST(ergo(rc == 0, !LNetHandleIsInvalid(kcb->kb_mdh)));
	C2_POST(ergo(rc == 0, kcb->kb_ktm == kctm));
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
