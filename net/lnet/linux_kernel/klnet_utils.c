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

#ifdef NLX_DEBUG
static void nlx_kprint_lnet_handle(const char *pre, lnet_handle_any_t h)
{
	char buf[32];
	LNetSnprintHandle(buf, sizeof buf, h);
	printk("%s: %s (lnet_handle_any_t)\n", pre, buf);
}

static void nlx_kprint_lnet_process_id(const char *pre, lnet_process_id_t p)
{
	printk("%s: NID=%lu PID=%u\n", pre,
	       (long unsigned) p.nid, (unsigned) p.pid);
}

static void nlx_kprint_lnet_md(const char *pre, const lnet_md_t *md)
{
	printk("%s: %p (lnet_md_t)\n", pre, md);
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
		for(i = 0; i < kcb->kb_kiov_len; ++i) {
			printk("\t[%d] %p %d %d\n", i,
			       kcb->kb_kiov[i].kiov_page,
			       kcb->kb_kiov[i].kiov_len,
			       kcb->kb_kiov[i].kiov_offset);
		}
	}
#endif
}

static const char *nlx_kcore_lnet_event_type_to_string(lnet_event_kind_t et)
{
	static const char *lnet_event_s[7] = {
		"GET", "PUT", "REPLY", "ACK", "SEND", "UNLINK", "<Unknown>"
	};
	const char *name;
	C2_CASSERT(ARRAY_SIZE(lnet_event_s) == LNET_EVENT_UNLINK + 2);
	if (et >= 0 && et <= LNET_EVENT_UNLINK)
		name = lnet_event_s[et];
	else
		name = lnet_event_s[6];
	return name;
}

static void nlx_kprint_lnet_event(const char *pre, const lnet_event_t *e)
{

	if (e == NULL) {
		printk("%s: <null> (lnet_event_t)\n", pre);
		return;
	}
	printk("%s: %p (lnet_event_t)\n", pre, e);
	nlx_kprint_lnet_process_id("\t   target:", e->target);
	nlx_kprint_lnet_process_id("\tinitiator:", e->target);
	printk("\t    sender: %ld\n", (long unsigned) e->sender);
	printk("\t      type: %d %s\n", e->type,
	       nlx_kcore_lnet_event_type_to_string(e->type));
	printk("\t  pt_index: %u\n", e->pt_index);
	printk("\tmatch_bits: %lx\n", (long unsigned) e->match_bits);
	printk("\t   rlength: %u\n", e->rlength);
	printk("\t   mlength: %u\n", e->mlength);
	nlx_kprint_lnet_handle("\t md_handle", e->md_handle);
	printk("\t  hdr_data: %lx\n", (long unsigned) e->hdr_data);
	printk("\t    status: %d\n", e->status);
	printk("\t  unlinked: %d\n", e->unlinked);
	printk("\t    offset: %u\n", e->offset);
	nlx_kprint_lnet_md("\t        md", &e->md);
}

static void nlx_kprint_kcore_tm(const char *pre,
				const struct nlx_kcore_transfer_mc *ktm)
{
	printk("%s: %p (nlx_kcore_transfer_mc)\n", pre, ktm);
	if (ktm == NULL)
		return;
	printk("\t      magic: %lu\n", (unsigned long) ktm->ktm_magic);
	nlx_kprint_lnet_handle("\t        eqh", ktm->ktm_eqh);
}
#endif

/**
   @addtogroup KLNetCore
   @{
 */

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
   @param kctm Pointer to kcore TM private data.
   @see nlx_kcore_hdr_data_decode(), nlx_kcore_hdr_data_encode_raw()
 */
static uint64_t nlx_kcore_hdr_data_encode(struct nlx_kcore_transfer_mc *kctm)
{
	struct nlx_core_ep_addr *cepa;

	C2_PRE(nlx_kcore_tm_invariant(kctm));
	cepa = &kctm->ktm_addr;
	return nlx_kcore_hdr_data_encode_raw(cepa->cepa_tmid,cepa->cepa_portal);
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
   @param lctm Pointer to core TM private data.
   @param kctm Pointer to kcore TM private data.
   @param lcbuf Pointer to core buffer private data with match bits set.
   @param kcb Pointer to kcore buffer private data with match bits set.
   @param threshold Value for threshold field. Should be at least 1.
   @param max_size Max size value, if not zero. If provided the
   LNET_MD_MAX_SIZE flag is set.
   @param options Optional flags to be set.  If not 0, only LNET_MD_OP_PUT or
   LNET_MD_OP_GET are accepted.
   @param isLNetGetOp Set to true if the lnet_md_t is to be used to create an
   MD that will be used in an LNetGet operation. The threshold field is forced
   to 2, and the out-of-order fields of the nlx_kcore_buffer are reset.
   @param umd Pointer to return structure to be filled in.  The ktm_eqh handle
   is used by default. Adjust if necessary.
   @post ergo(isLNetGetOp, umd->threshold == 2 && !kcb->kb_ooo_reply)
 */
static void nlx_kcore_umd_init(struct nlx_core_transfer_mc *lctm,
			       struct nlx_kcore_transfer_mc *kctm,
			       struct nlx_core_buffer *lcbuf,
			       struct nlx_kcore_buffer *kcb,
			       int threshold,
			       int max_size,
			       unsigned options,
			       bool isLNetGetOp,
			       lnet_md_t *umd)
{
	C2_PRE(nlx_core_tm_invariant(lctm));
	C2_PRE(nlx_kcore_tm_invariant(kctm));
	C2_PRE(nlx_core_buffer_invariant(lcbuf));
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
	kcb->kb_qtype = lcbuf->cb_qtype;
	if (isLNetGetOp) {
		umd->threshold = 2;
		kcb->kb_ooo_reply   = false;
		kcb->kb_ooo_mlength = 0;
		kcb->kb_ooo_status  = 0;
		kcb->kb_ooo_offset  = 0;
	} else
		umd->threshold = threshold;
	if (max_size != 0) {
		umd->max_size = max_size;
		umd->options |= LNET_MD_MAX_SIZE;
	}
	umd->user_ptr = kcb;
	umd->eq_handle = kctm->ktm_eqh;

	NLXDBG(lctm, 2, nlx_kprint_lnet_md("umd init", umd));
	C2_POST(ergo(isLNetGetOp, umd->threshold == 2 && !kcb->kb_ooo_reply));
}

/**
   Helper subroutine to adjust the length of the kiov vector in a UMD
   to match a specified byte length.
   This is needed for SEND and active buffer operations.
   Restore the kiov with nlx_kcore_kiov_restore_length().
   @param lctm Pointer to kcore TM private data.
   @param kcb Pointer to kcore buffer private data with match bits set.
   @param umd Pointer to the UMD.
   @param bytes The byte count desired.
   @see nlx_kcore_kiov_restore_length()
   @post kcb->kb_kiov_adj_idx >= 0
   @post kcb->kb_kiov_adj_idx < kcb->kb_kiov_len
   @post nlx_kcore_kiov_invariant(umd->start, umd->length)
 */
static void nlx_kcore_kiov_adjust_length(struct nlx_core_transfer_mc *lctm,
					 struct nlx_kcore_buffer *kcb,
					 lnet_md_t *umd,
					 c2_bcount_t bytes)
{
	size_t num;
	unsigned last;

	C2_PRE(umd->start != NULL);
	C2_PRE(umd->options & LNET_MD_KIOV);
	C2_PRE(umd->length > 0);
	C2_PRE(nlx_core_tm_invariant(lctm));
	C2_PRE(nlx_kcore_buffer_invariant(kcb));
	C2_PRE(umd->start == kcb->kb_kiov);

	num = nlx_kcore_num_kiov_entries_for_bytes((lnet_kiov_t *) umd->start,
						   umd->length, bytes, &last);
	NLXDBGP(lctm, 2, "%p: kbuf:%p size:%ld vec:%lu/%lu loff:%u\n",
		lctm, kcb, (unsigned long) bytes,
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
   @param kcb Pointer to kcore buffer private data with match bits set.
   @see nlx_kcore_kiov_adjust_length()
   @pre kcb->kb_kiov_adj_idx >= 0
   @pre kcb->kb_kiov_adj_idx < kcb->kb_kiov_len
   @post nlx_kcore_kiov_invariant(kcb->kb_kiov, kcb->kb_kiov_len)
*/
static void nlx_kcore_kiov_restore_length(struct nlx_core_transfer_mc *lctm,
					  struct nlx_kcore_buffer *kcb)
{
	C2_PRE(nlx_core_tm_invariant(lctm));
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
   @param lctm Pointer to core TM private data.
   @param kctm Pointer to kcore TM private data.
   @param lcbuf Pointer to core buffer private data with match bits set in
   the cb_match_bits field, and the network address in cb_addr.
   @param kcb Pointer to kcore buffer private data.
   @param umd Pointer to lnet_md_t structure for the buffer, with appropriate
   values set for the desired operation.
   @note LNet event could potentially be delivered before this sub returns.
 */
static int nlx_kcore_LNetMDAttach(struct nlx_core_transfer_mc *lctm,
				  struct nlx_kcore_transfer_mc *kctm,
				  struct nlx_core_buffer *lcbuf,
				  struct nlx_kcore_buffer *kcb,
				  lnet_md_t *umd)
{
	lnet_handle_me_t meh;
	lnet_process_id_t id;
	int rc;

	C2_PRE(nlx_core_tm_invariant(lctm));
	C2_PRE(nlx_kcore_tm_invariant(kctm));
	C2_PRE(nlx_core_buffer_invariant(lcbuf));
	C2_PRE(nlx_kcore_buffer_invariant(kcb));
	C2_PRE(lcbuf->cb_match_bits != 0);

	id.nid = lcbuf->cb_addr.cepa_nid;
	id.pid = lcbuf->cb_addr.cepa_pid;
	rc = LNetMEAttach(lcbuf->cb_addr.cepa_portal, id,
			  lcbuf->cb_match_bits, 0,
			  LNET_UNLINK, LNET_INS_AFTER, &meh);
	if (rc != 0) {
		NLXDBGP(lctm, 1,"LNetMEAttach: %d\n", rc);
		return rc;
	}
	C2_POST(!LNetHandleIsInvalid(meh));
	NLXDBG(lctm, 2, nlx_print_core_buffer("nlx_kcore_LNetMDAttach", lcbuf));

	kcb->kb_ktm = kctm; /* loopback can deliver in the LNetPut call */
	rc = LNetMDAttach(meh, *umd, LNET_UNLINK, &kcb->kb_mdh);
	if (rc == 0) {
		NLXDBG(lctm, 1, nlx_kprint_lnet_handle("MDAttach",kcb->kb_mdh));
	} else {
		int trc = LNetMEUnlink(meh);
		NLXDBGP(lctm, 1, "LNetMDAttach: %d\n", rc);
		NLXDBGP(lctm, 1, "LNetMEUnlink: %d\n", trc);
		C2_ASSERT(trc == 0);
		LNetInvalidateHandle(&kcb->kb_mdh);
		kcb->kb_ktm = NULL;
	}

	/* Cannot make these assertions here as delivery is asynchronous, and
	   could have completed before we got here.
	   C2_POST(ergo(rc == 0, !LNetHandleIsInvalid(kcb->kb_mdh)));
	   C2_POST(ergo(rc == 0, kcb->kb_ktm == kctm));
	*/
	return rc;
}

/**
   Helper subroutine to unlink an MD.
   @param lctm Pointer to core TM private data.
   @param kctm Pointer to kcore TM private data.
   @param kcb Pointer to kcore buffer private data with kb_mdh set.
   @note LNet event could potentially be delivered before this sub returns.
 */
static int nlx_kcore_LNetMDUnlink(struct nlx_core_transfer_mc *lctm,
				  struct nlx_kcore_transfer_mc *kctm,
				  struct nlx_kcore_buffer *kcb)
{
	int rc;

	C2_PRE(nlx_core_tm_invariant(lctm));
	C2_PRE(nlx_kcore_tm_invariant(kctm));
	C2_PRE(nlx_kcore_buffer_invariant(kcb));
	rc = LNetMDUnlink(kcb->kb_mdh);
	NLXDBG(lctm, 1, NLXP("LNetMDUnlink: %d\n", rc));
	if (rc != 0)
		LNET_ADDB_FUNCFAIL_ADD(kctm->ktm_addb, rc);
	return rc;
}

/**
   Helper subroutine to send a buffer to a remote destination using
   @c LNetPut().
   - The MD is set up to automatically unlink.
   - The MD handle is set in the struct nlx_kcore_buffer::kb_mdh field.
   - The TM's portal and TMID are encoded in the header data.
   - Sets the kb_ktm field in the KCore buffer private data.
   @param lctm Pointer to core TM private data.
   @param kctm Pointer to kcore TM private data.
   @param lcbuf Pointer to core buffer private data with match bits set, and
   the address of the remote destination in struct nlx_core_buffer::cb_addr.
   @param kcb Pointer to kcore buffer private data.
   @param umd Pointer to lnet_md_t structure for the buffer, with appropriate
   values set for the desired operation.
   @see nlx_kcore_hdr_data_encode(), nlx_kcore_hdr_data_decode()
   @note LNet event could potentially be delivered before this sub returns.
 */
static int nlx_kcore_LNetPut(struct nlx_core_transfer_mc *lctm,
			     struct nlx_kcore_transfer_mc *kctm,
			     struct nlx_core_buffer *lcbuf,
			     struct nlx_kcore_buffer *kcb,
			     lnet_md_t *umd)
{
	lnet_process_id_t target;
	int rc;

	C2_PRE(nlx_core_tm_invariant(lctm));
	C2_PRE(nlx_kcore_tm_invariant(kctm));
	C2_PRE(nlx_core_buffer_invariant(lcbuf));
	C2_PRE(nlx_kcore_buffer_invariant(kcb));
	C2_PRE(lcbuf->cb_match_bits != 0);

	rc = LNetMDBind(*umd, LNET_UNLINK, &kcb->kb_mdh);
	if (rc != 0) {
		NLXDBGP(lctm, 1,"LNetMDBind: %d\n", rc);
		return rc;
	}
	NLXDBG(lctm, 2, nlx_print_core_buffer("nlx_kcore_LNetPut", lcbuf));
	NLXDBG(lctm, 2, nlx_kprint_lnet_handle("LNetMDBind", kcb->kb_mdh));

	target.nid = lcbuf->cb_addr.cepa_nid;
	target.pid = lcbuf->cb_addr.cepa_pid;
	kcb->kb_ktm = kctm; /* loopback can deliver in the LNetPut call */
	rc = LNetPut(kctm->ktm_addr.cepa_nid, kcb->kb_mdh, LNET_NOACK_REQ,
		     target, lcbuf->cb_addr.cepa_portal,
		     lcbuf->cb_match_bits, 0,
		     nlx_kcore_hdr_data_encode(kctm));
	if (rc != 0) {
		int trc = LNetMDUnlink(kcb->kb_mdh);
		NLXDBGP(lctm, 1, "LNetPut: %d\n", rc);
		NLXDBGP(lctm, 1, "LNetMDUnlink: %d\n", trc);
		C2_ASSERT(trc == 0);
		LNetInvalidateHandle(&kcb->kb_mdh);
		kcb->kb_ktm = NULL;
	}

	/* Cannot make these assertions here, because loopback can deliver
	   before we get here.  Leaving the assertions in the comment.
	   C2_POST(ergo(rc == 0, !LNetHandleIsInvalid(kcb->kb_mdh)));
	   C2_POST(ergo(rc == 0, kcb->kb_ktm == kctm));
	*/
	return rc;
}

/**
   Helper subroutine to fetch a buffer from a remote destination using
   @c LNetGet().
   - The MD is set up to automatically unlink.
   - The MD handle is set in the struct nlx_kcore_buffer::kb_mdh field.
   - The TM's portal and TMID are encoded in the header data.
   - Sets the kb_ktm field in the KCore buffer private data.
   @param lctm Pointer to core TM private data.
   @param kctm Pointer to kcore TM private data.
   @param lcbuf Pointer to kcore buffer private data with match bits set, and
   the address of the remote destination in struct nlx_core_buffer::cb_addr.
   @param kcb Pointer to kcore buffer private data.
   @param umd Pointer to lnet_md_t structure for the buffer, with appropriate
   values set for the desired operation.
   @pre umd->threshold == 2
   @see nlx_kcore_hdr_data_encode(), nlx_kcore_hdr_data_decode()
   @note LNet event could potentially be delivered before this sub returns.
 */
static int nlx_kcore_LNetGet(struct nlx_core_transfer_mc *lctm,
			     struct nlx_kcore_transfer_mc *kctm,
			     struct nlx_core_buffer *lcbuf,
			     struct nlx_kcore_buffer *kcb,
			     lnet_md_t *umd)
{
	lnet_process_id_t target;
	int rc;

	C2_PRE(nlx_core_tm_invariant(lctm));
	C2_PRE(nlx_kcore_tm_invariant(kctm));
	C2_PRE(nlx_core_buffer_invariant(lcbuf));
	C2_PRE(nlx_kcore_buffer_invariant(kcb));
	C2_PRE(lcbuf->cb_match_bits != 0);

	C2_PRE(umd->threshold == 2);

	rc = LNetMDBind(*umd, LNET_UNLINK, &kcb->kb_mdh);
	if (rc != 0) {
		NLXDBGP(lctm, 1,"LNetMDBind: %d\n", rc);
		return rc;
	}
	NLXDBG(lctm, 2, nlx_print_core_buffer("nlx_kcore_LNetGet", lcbuf));
	NLXDBG(lctm, 2, nlx_kprint_lnet_handle("LNetMDBind", kcb->kb_mdh));

	target.nid = lcbuf->cb_addr.cepa_nid;
	target.pid = lcbuf->cb_addr.cepa_pid;
	kcb->kb_ktm = kctm; /* loopback can deliver in the LNetGet call */
	rc = LNetGet(kctm->ktm_addr.cepa_nid, kcb->kb_mdh,
		     target, lcbuf->cb_addr.cepa_portal,
		     lcbuf->cb_match_bits, 0);
	if (rc != 0) {
		int trc = LNetMDUnlink(kcb->kb_mdh);
		NLXDBGP(lctm, 1, "LNetGet: %d\n", rc);
		NLXDBGP(lctm, 1, "LNetMDUnlink: %d\n", trc);
		C2_ASSERT(trc == 0);
		LNetInvalidateHandle(&kcb->kb_mdh);
		kcb->kb_ktm = NULL;
	}

	/* Cannot make these assertions here, because loopback can deliver
	   before we get here.  Leaving the assertions in the comment.
	   C2_POST(ergo(rc == 0, !LNetHandleIsInvalid(kcb->kb_mdh)));
	   C2_POST(ergo(rc == 0, kcb->kb_ktm == kctm));
	*/
	return rc;
}

/**
   Maps a page that should point to a nlx_core_domain.
   Uses kmap() because this subroutine can be used on contexts that will block.
   @pre nlx_kcore_domain_invariant(kd) &&
   !nlx_core_kmem_loc_is_empty(&kd->kd_cd_loc)
   @param kd kernel private object containing location reference to be mapped
   @returns core object, never NULL
 */
static struct nlx_core_domain *nlx_kcore_core_domain_map(
						   struct nlx_kcore_domain *kd)
{
	char *ptr;
	struct nlx_core_kmem_loc *loc;
	struct nlx_core_domain *ret;

	C2_PRE(nlx_kcore_domain_invariant(kd));
	loc = &kd->kd_cd_loc;
	C2_PRE(!nlx_core_kmem_loc_is_empty(loc));
	ptr = kmap(loc->kl_page);
	ret = (struct nlx_core_domain *) (ptr + loc->kl_offset);
	C2_POST(ret != NULL);
	return ret;
}

/**
   Unmaps the page that contains a nlx_core_domain using kunmap().
   @pre nlx_kcore_domain_invariant(kd)
   @param kd Pointer to kcore domain private data.
 */
static void nlx_kcore_core_domain_unmap(struct nlx_kcore_domain *kd)
{
	C2_PRE(nlx_kcore_domain_invariant(kd));
	kunmap(kd->kd_cd_loc.kl_page);
}

/**
   Maps a page that should point to a nlx_core_buffer.
   Uses kmap() because this subroutine can be used on contexts that will block.
   @pre nlx_kcore_buffer_invariant(kb)
   @post nlx_core_buffer_invariant(ret)
   @param kb Pointer to kcore buffer private data.
   @returns core object, never NULL
 */
static struct nlx_core_buffer *nlx_kcore_core_buffer_map(
						   struct nlx_kcore_buffer *kb)
{
	char *ptr;
	struct nlx_core_kmem_loc *loc;
	struct nlx_core_buffer *ret;

	C2_PRE(nlx_kcore_buffer_invariant(kb));
	loc = &kb->kb_cb_loc;
	ptr = kmap(loc->kl_page);
	ret = (struct nlx_core_buffer *) (ptr + loc->kl_offset);
	C2_POST(nlx_core_buffer_invariant(ret));
	return ret;
}

/**
   Unmaps the page that contains a nlx_core_buffer using kunmap().
   @pre nlx_kcore_buffer_invariant(kb)
   @param kb Pointer to kcore buffer private data.
 */
static void nlx_kcore_core_buffer_unmap(struct nlx_kcore_buffer *kb)
{
	C2_PRE(nlx_kcore_buffer_invariant(kb));
	kunmap(kb->kb_cb_loc.kl_page);
}

/**
   Maps a page that should point to a nlx_core_transfer_mc.
   Uses kmap() because this subroutine can be used on contexts that will block.
   @pre nlx_kcore_tm_invariant(ktm)
   @post nlx_core_tm_invariant(ret)
   @param ktm Pointer to kcore transfer machine private data.
   @returns core object, never NULL
 */
static struct nlx_core_transfer_mc *nlx_kcore_core_tm_map(
					     struct nlx_kcore_transfer_mc *ktm)
{
	char *ptr;
	struct nlx_core_kmem_loc *loc;
	struct nlx_core_transfer_mc *ret;

	C2_PRE(nlx_kcore_tm_invariant(ktm));
	loc = &ktm->ktm_ctm_loc;
	ptr = kmap(loc->kl_page);
	ret = (struct nlx_core_transfer_mc *) (ptr + loc->kl_offset);
	C2_POST(nlx_core_tm_invariant(ret));
	return ret;
}

/**
   Unmaps the page that contains a nlx_core_transfer_mc using kunmap().
   @pre nlx_kcore_tm_invariant(ktm)
   @param ktm Pointer to kcore transfer machine private data.
 */
static void nlx_kcore_core_tm_unmap(struct nlx_kcore_transfer_mc *ktm)
{
	C2_PRE(nlx_kcore_tm_invariant(ktm));
	kunmap(ktm->ktm_ctm_loc.kl_page);
}

/**
   Maps a page that should point to a nlx_core_transfer_mc.
   Uses kmap_atomic() and consumes the KM_USER0 slot.
   @pre nlx_kcore_tm_invariant(ktm)
   @post nlx_core_tm_invariant(ret)
   @param ktm Pointer to kcore transfer machine private data.
   @returns core object, never NULL
 */
static struct nlx_core_transfer_mc *nlx_kcore_core_tm_map_atomic(
					     struct nlx_kcore_transfer_mc *ktm)
{
	char *ptr;
	struct nlx_core_kmem_loc *loc;
	struct nlx_core_transfer_mc *ret;

	C2_PRE(nlx_kcore_tm_invariant(ktm));
	loc = &ktm->ktm_ctm_loc;
	ptr = kmap_atomic(loc->kl_page, KM_USER0);
	ret = (struct nlx_core_transfer_mc *) (ptr + loc->kl_offset);
	C2_POST(nlx_core_tm_invariant(ret));
	return ret;
}

/**
   Unmaps the page that contains a nlx_core_transfer_mc.
   Uses kunmap_atomic() on the KM_USER0 slot.
   @note this signature differs from nlx_kcore_core_tm_unmap() due to the
   differing requirements of kunmap() vs kunmap_atomic(); the former requires
   a struct page while the latter requires a mapped address.
   @pre nlx_core_tm_invariant(ctm)
   @param ctm Pointer to corresponding core TM private data.
 */
static void nlx_kcore_core_tm_unmap_atomic(struct nlx_core_transfer_mc *ctm)
{
	C2_PRE(nlx_core_tm_invariant(ctm));
	kunmap_atomic(ctm, KM_USER0);
}

/** @} */ /* KLNetCore */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 79
 *  scroll-step: 1
 *  End:
 */
