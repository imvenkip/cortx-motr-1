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
 * Original creation date: 1/9/2012
 */

/* Kernel specific LNet unit tests.
 * The tests cases are loaded from the address space agnostic ../lnet_ut.c
 * file.
 */

static bool ut_bufvec_alloc(struct c2_bufvec *bv, size_t n)
{
	C2_ALLOC_ARR(bv->ov_vec.v_count, n);
	C2_ALLOC_ARR(bv->ov_buf, n);
	if (bv->ov_vec.v_count == 0 || bv->ov_buf == NULL) {
		c2_free(bv->ov_vec.v_count);
		return false;
	}
	bv->ov_vec.v_nr = n;
	return true;
}

#define UT_BUFVEC_ALLOC(v,n)	\
if (!ut_bufvec_alloc(&v,n)) {	\
	C2_UT_FAIL("no memory");\
	return;			\
}

#define UT_BUFVEC_FREE(v)				\
	c2_free(v.ov_vec.v_count);			\
	c2_free(v.ov_buf)

static void ktest_buf_shape(void)
{
	struct c2_net_domain dom1;
	struct c2_bufvec bv1;
	void *base;
	unsigned num_pages;

	C2_SET0(&dom1);
	C2_UT_ASSERT(!c2_net_domain_init(&dom1, &c2_net_lnet_xprt));

	/* buffer shape APIs */
	C2_UT_ASSERT(c2_net_domain_get_max_buffer_size(&dom1)
		     == LNET_MAX_PAYLOAD);
	C2_UT_ASSERT(c2_net_domain_get_max_buffer_segment_size(&dom1)
		     == PAGE_SIZE);
	C2_UT_ASSERT(c2_net_domain_get_max_buffer_segments(&dom1)
		     == LNET_MAX_IOV);

	/* test the segment page count computation */
	UT_BUFVEC_ALLOC(bv1, 1);
	base = (void *)((uint64_t)&base & PAGE_MASK); /* arbitrary, page aligned */

#define EXP_SEG_COUNT(ptr,segsize,expcount)		\
	bv1.ov_buf[0] = (ptr);				\
	bv1.ov_vec.v_count[0] = (segsize);		\
	num_pages = bufvec_seg_page_count(&bv1, 0);	\
	C2_UT_ASSERT(num_pages == (expcount))

	EXP_SEG_COUNT(base,             PAGE_SIZE,     1);/* pg aligned, 1 pg */
	EXP_SEG_COUNT(base,             PAGE_SIZE+1,   2);/* pg aligned,>1 pg */
	EXP_SEG_COUNT(base,             PAGE_SIZE-1,   1);/* pg aligned,<1 pg */
	EXP_SEG_COUNT(base,             2*PAGE_SIZE,   2);/* pg aligned, 2 pg */
	EXP_SEG_COUNT(base+PAGE_SIZE/2, 2*PAGE_SIZE,   3);/* mid-pg,  2 pg */
	EXP_SEG_COUNT(base+PAGE_SIZE/2, PAGE_SIZE,     2);/* mid-pg,  1 pg */
	EXP_SEG_COUNT(base+PAGE_SIZE/2, PAGE_SIZE/2+1, 2);/* mid-pg, >0.5 pg */
	EXP_SEG_COUNT(base+PAGE_SIZE/2, PAGE_SIZE/2,   1);/* mid-pg,  0.5 pg */
	EXP_SEG_COUNT(base+PAGE_SIZE/2, PAGE_SIZE/2-1, 1);/* mid-pg, <0.5 pg */

#undef EXP_SEG_COUNT

	/* fini */
	UT_BUFVEC_FREE(bv1);
	c2_net_domain_fini(&dom1);
}

static void ktest_buf_reg(void)
{
	struct c2_net_domain dom1;
	struct c2_net_buffer nb1;
	struct c2_net_buffer nb2;
	struct c2_net_buffer nb3;
	c2_bcount_t bsize;
	c2_bcount_t bsegsize;
	int32_t     bsegs;
	struct nlx_xo_buffer *xb;
	struct nlx_core_buffer *cb;
	struct nlx_kcore_buffer *kcb1;
	struct nlx_kcore_buffer *kcb2;
	int i;
	struct c2_bufvec *v1;
	struct c2_bufvec *v2;
	struct c2_bufvec *v3;
	c2_bcount_t thunk;

	C2_SET0(&dom1);
	C2_SET0(&nb1);
	C2_SET0(&nb2);
	C2_SET0(&nb3);

	C2_UT_ASSERT(!c2_net_domain_init(&dom1, &c2_net_lnet_xprt));

	/* TEST
	   Register a network buffer of maximal size and perfectly aligned on
	   page boundaries.
	 */
	bsize    = LNET_MAX_PAYLOAD;
	bsegsize = PAGE_SIZE;
	bsegs    = LNET_MAX_IOV;
	/* Allocate a bufvec into the buffer. */
	C2_UT_ASSERT(c2_bufvec_alloc(&nb1.nb_buffer, bsegs, bsegsize) == 0);
	C2_UT_ASSERT(c2_vec_count(&nb1.nb_buffer.ov_vec) == bsize);

	/* register the buffer */
	nb1.nb_flags = 0;
	C2_UT_ASSERT(!c2_net_buffer_register(&nb1, &dom1));
	C2_UT_ASSERT(nb1.nb_flags & C2_NET_BUF_REGISTERED);

	/* check the kcore data structure */
	xb = nb1.nb_xprt_private;
	cb = &xb->xb_core;
	C2_UT_ASSERT(cb->cb_magic == C2_NET_LNET_CORE_BUF_MAGIC);
	kcb1 = cb->cb_kpvt;
	C2_UT_ASSERT(kcb1->kb_magic == C2_NET_LNET_KCORE_BUF_MAGIC);
	C2_UT_ASSERT(kcb1->kb_min_recv_size == 0);
	C2_UT_ASSERT(kcb1->kb_max_recv_msgs == 0);
	C2_UT_ASSERT(LNetHandleIsInvalid(kcb1->kb_mdh));
	C2_UT_ASSERT(kcb1->kb_kiov != NULL);
	C2_UT_ASSERT(kcb1->kb_kiov_len == bsegs);
	for (i = 0; i < kcb1->kb_kiov_len; ++i) {
		void *addr;
		C2_UT_ASSERT(kcb1->kb_kiov[i].kiov_len == bsegsize);
		C2_UT_ASSERT(kcb1->kb_kiov[i].kiov_offset == 0);
		addr = page_address(kcb1->kb_kiov[i].kiov_page);
		C2_UT_ASSERT(addr == nb1.nb_buffer.ov_buf[i]);
	}

	/* TEST
	   Register a network buffer with half page segments.
	   Use the same allocated memory segments from the first network buffer.
	   We should get a kiov of the same length with 2 adjacent elements
	   for each page, with the first having offset 0 and the second
	   PAGE_SIZE/2.
	 */
	UT_BUFVEC_ALLOC(nb2.nb_buffer, bsegs);
	v1 = &nb1.nb_buffer;
	v2 = &nb2.nb_buffer;
	for (i = 0; i < v2->ov_vec.v_nr; i += 2) {
		v2->ov_vec.v_count[i] = v1->ov_vec.v_count[i] / 2;
		v2->ov_buf[i] = v1->ov_buf[i];
		v2->ov_vec.v_count[i+1] = v2->ov_vec.v_count[i];
		v2->ov_buf[i+1] = v2->ov_buf[i] + v2->ov_vec.v_count[i];
	}

	/* register the buffer */
	nb2.nb_flags = 0;
	C2_UT_ASSERT(!c2_net_buffer_register(&nb2, &dom1));
	C2_UT_ASSERT(nb2.nb_flags & C2_NET_BUF_REGISTERED);

	/* check the kcore data structure */
	xb = nb2.nb_xprt_private;
	cb = &xb->xb_core;
	C2_UT_ASSERT(cb->cb_magic == C2_NET_LNET_CORE_BUF_MAGIC);
	kcb2 = cb->cb_kpvt;
	C2_UT_ASSERT(kcb2->kb_magic == C2_NET_LNET_KCORE_BUF_MAGIC);
	C2_UT_ASSERT(kcb2->kb_min_recv_size == 0);
	C2_UT_ASSERT(kcb2->kb_max_recv_msgs == 0);
	C2_UT_ASSERT(LNetHandleIsInvalid(kcb2->kb_mdh));
	C2_UT_ASSERT(kcb2->kb_kiov != NULL);
	C2_UT_ASSERT(kcb2->kb_kiov_len == bsegs);
	for (i = 0; i < kcb2->kb_kiov_len; ++i) {
		void *addr;
		C2_UT_ASSERT(kcb2->kb_kiov[i].kiov_len == bsegsize/2);
		addr = page_address(kcb2->kb_kiov[i].kiov_page);
		if (i % 2 == 0) {
			C2_UT_ASSERT(addr == nb2.nb_buffer.ov_buf[i]);
			C2_UT_ASSERT(kcb2->kb_kiov[i].kiov_offset == 0);
		} else {
			C2_UT_ASSERT(kcb2->kb_kiov[i].kiov_offset == bsegsize/2);
			C2_UT_ASSERT(kcb2->kb_kiov[i].kiov_page ==
				     kcb2->kb_kiov[i-1].kiov_page);
			C2_UT_ASSERT(addr ==
				     nb2.nb_buffer.ov_buf[i] - bsegsize/2);
		}
	}

	/* TEST
	   Provide a buffer whose c2_bufvec shape is legal, but whose kiov will
	   exceed the internal limits.
	   Use the same allocated memory segments from the first network buffer.
	 */
	UT_BUFVEC_ALLOC(nb3.nb_buffer, bsegs);
	v3 = &nb3.nb_buffer;
	thunk = PAGE_SIZE;
	for (i = 0; i < v3->ov_vec.v_nr; ++i) {
		/* each segment spans 2 pages */
		v3->ov_vec.v_count[i] = thunk;
		v3->ov_buf[i] = v1->ov_buf[i] + PAGE_SIZE - 1;
	}

	/* register the buffer */
	nb3.nb_flags = 0;
	i = c2_net_buffer_register(&nb3, &dom1);
	C2_UT_ASSERT(i == -EFBIG);

	/* fini */
	c2_net_buffer_deregister(&nb1, &dom1);
	C2_UT_ASSERT(!(nb1.nb_flags & C2_NET_BUF_REGISTERED));
	C2_UT_ASSERT(nb1.nb_xprt_private == NULL);
	c2_net_buffer_deregister(&nb2, &dom1);
	C2_UT_ASSERT(!(nb2.nb_flags & C2_NET_BUF_REGISTERED));
	C2_UT_ASSERT(nb2.nb_xprt_private == NULL);

	UT_BUFVEC_FREE(nb3.nb_buffer); /* just vector, not segs */
	UT_BUFVEC_FREE(nb2.nb_buffer); /* just vector, not segs */
	c2_bufvec_free(&nb1.nb_buffer);
	c2_net_domain_fini(&dom1);
}

static void ktest_core_ep_addr(void)
{
	struct nlx_xo_domain dom;
	struct nlx_core_ep_addr tmaddr;
	const char *epstr[] = {
		"127.0.0.1@tcp:12345:30:10",
		"127.0.0.1@tcp:12345:30:*",
		"4.4.4.4@tcp:42:29:28"
	};
	const char *failepstr[] = {
		"notip@tcp:12345:30:10",
		"notnid:12345:30:10",
		"127.0.0.1@tcp:notpid:30:10",
		"127.0.0.1@tcp:12:notportal:10",
		"127.0.0.1@tcp:12:30:nottm",
		"127.0.0.1@tcp:12:30:-10",        /* positive required */
		"127.0.0.1@tcp:12:30:4096",       /* in range */
	};
	const struct nlx_core_ep_addr ep_addr[] = {
		{
			.cepa_pid = 12345,
			.cepa_portal = 30,
			.cepa_tmid = 10,
		},
		{
			.cepa_pid = 12345,
			.cepa_portal = 30,
			.cepa_tmid = C2_NET_LNET_TMID_INVALID,
		},
		{
			.cepa_pid = 42,
			.cepa_portal = 29,
			.cepa_tmid = 28,
		},
	};
	char buf[C2_NET_LNET_XEP_ADDR_LEN];
	char * const *nidstrs;
	int rc;
	int i;

	C2_UT_ASSERT(!nlx_core_nidstrs_get(&nidstrs));
	C2_UT_ASSERT(nidstrs != NULL);
	for (i = 0; nidstrs[i] != NULL; ++i) {
		char *network;
		network = strchr(nidstrs[i], '@');
		if (network != NULL && strcmp(network, "@tcp") == 0)
			break;
	}
	if (nidstrs[i] == NULL) {
		C2_UT_PASS("skipped successful LNet address tests, "
			   "no tcp network");
	} else {
		C2_CASSERT(ARRAY_SIZE(epstr) == ARRAY_SIZE(ep_addr));
		for (i = 0; i < ARRAY_SIZE(epstr); ++i) {
			rc = nlx_core_ep_addr_decode(&dom.xd_core, epstr[i],
						     &tmaddr);
			C2_UT_ASSERT(rc == 0);
			C2_UT_ASSERT(ep_addr[i].cepa_pid == tmaddr.cepa_pid);
			C2_UT_ASSERT(ep_addr[i].cepa_portal ==
				     tmaddr.cepa_portal);
			C2_UT_ASSERT(ep_addr[i].cepa_tmid == tmaddr.cepa_tmid);
			nlx_core_ep_addr_encode(&dom.xd_core, &tmaddr, buf);
			C2_UT_ASSERT(strcmp(buf, epstr[i]) == 0);
		}
	}
	nlx_core_nidstrs_put(&nidstrs);
	C2_UT_ASSERT(nidstrs == NULL);

	for (i = 0; i < ARRAY_SIZE(failepstr); ++i) {
		rc = nlx_core_ep_addr_decode(&dom.xd_core, failepstr[i],
					     &tmaddr);
		C2_UT_ASSERT(rc == -EINVAL);
	}
}

static void ktest_enc_dec(void)
{
	uint32_t tmid;
	uint64_t counter;
	uint32_t portal;
	struct nlx_core_transfer_mc lctm;

	/* TEST
	   Check that match bit decode reverses encode.
	*/
#define TEST_MATCH_BIT_ENCODE(_t, _c)					\
	nlx_kcore_match_bits_decode(nlx_kcore_match_bits_encode((_t),(_c)), \
				    &tmid, &counter);			\
	C2_UT_ASSERT(tmid == (_t));					\
	C2_UT_ASSERT(counter == (_c))

	TEST_MATCH_BIT_ENCODE(0, 0);
	TEST_MATCH_BIT_ENCODE(C2_NET_LNET_TMID_MAX, 0);
	TEST_MATCH_BIT_ENCODE(C2_NET_LNET_TMID_MAX, C2_NET_LNET_BUFFER_ID_MIN);
	TEST_MATCH_BIT_ENCODE(C2_NET_LNET_TMID_MAX, C2_NET_LNET_BUFFER_ID_MAX);

#undef TEST_MATCH_BIT_ENCODE

	/* TEST
	   Check that hdr data decode reverses encode.
	*/
	C2_SET0(&lctm);
	lctm.ctm_magic = C2_NET_LNET_CORE_TM_MAGIC; /* fake */
	C2_UT_ASSERT(nlx_core_tm_invariant(&lctm)); /* to make this pass */

#define TEST_HDR_DATA_ENCODE(_p, _t)					\
	lctm.ctm_addr.cepa_tmid = (_t);					\
	lctm.ctm_addr.cepa_portal = (_p);				\
	nlx_kcore_hdr_data_decode(nlx_kcore_hdr_data_encode(&lctm),	\
				  &portal, &tmid);			\
	C2_UT_ASSERT(portal == (_p));					\
	C2_UT_ASSERT(tmid == (_t))

	TEST_HDR_DATA_ENCODE(0,  0);
	TEST_HDR_DATA_ENCODE(30, 0);
	TEST_HDR_DATA_ENCODE(30, C2_NET_LNET_TMID_MAX);
	TEST_HDR_DATA_ENCODE(63, 0);
	TEST_HDR_DATA_ENCODE(63, C2_NET_LNET_TMID_MAX);

#undef TEST_HDR_DATA_ENCODE
}

/* ktest_msg */
enum {
	UT_KMSG_OPS  = 3,
};
static bool ut_ktest_msg_LNetMDAttach1_called;
static int ut_ktest_msg_LNetMDAttach1(struct nlx_core_transfer_mc *lctm,
				      struct nlx_core_buffer *lcbuf,
				      lnet_md_t *umd)
{
	struct nlx_kcore_transfer_mc *kctm = lctm->ctm_kpvt;
	struct nlx_kcore_buffer       *kcb = lcbuf->cb_kpvt;
	uint32_t portal;
	uint64_t counter;

	ut_ktest_msg_LNetMDAttach1_called = true;
	NLXDBG(lctm, 1, printk("intercepted LNetMDAttach\n"));
	NLXDBG(lctm, 1, nlx_kprint_lnet_md("ktest_msg", umd));

	C2_UT_ASSERT(umd->options & LNET_MD_KIOV);
	C2_UT_ASSERT(umd->start == kcb->kb_kiov);
	C2_UT_ASSERT(umd->length == kcb->kb_kiov_len);

	C2_UT_ASSERT(umd->threshold == UT_KMSG_OPS);

	C2_UT_ASSERT(umd->options & LNET_MD_MAX_SIZE);
	C2_UT_ASSERT(umd->max_size == UT_MSG_SIZE);

	C2_UT_ASSERT(umd->options & LNET_MD_OP_PUT);
	C2_UT_ASSERT(umd->user_ptr == lcbuf);
	C2_UT_ASSERT(LNetHandleIsEqual(umd->eq_handle, kctm->ktm_eqh));

	nlx_kcore_match_bits_decode(lcbuf->cb_match_bits, &portal, &counter);
	C2_UT_ASSERT(portal == lctm->ctm_addr.cepa_tmid);
	C2_UT_ASSERT(counter == 0);

	kcb->kb_ktm = kctm;

	return 0;
}

static void ut_ktest_msg_post_event(struct nlx_core_buffer *lcbuf,
				    unsigned mlength,
				    unsigned offset,
				    int status,
				    int unlinked,
				    uint64_t hdr_data)
{
	lnet_event_t ev;

	C2_SET0(&ev);
	ev.md.user_ptr = lcbuf;
	ev.type        = LNET_EVENT_PUT;
	ev.mlength     = mlength;
	ev.rlength     = mlength;
	ev.offset      = offset;
	ev.status      = status;
	ev.unlinked    = unlinked;
	ev.hdr_data    = hdr_data;
	nlx_kcore_eq_cb(&ev);
}

static void ktest_msg_body(struct test_msg_data *td)
{
	struct c2_net_buffer            *nb1 = &td->bufs1[0];
	struct nlx_xo_transfer_mc       *tp1 = TM1->ntm_xprt_private;
	struct nlx_core_transfer_mc   *lctm1 = &tp1->xtm_core;
	struct nlx_xo_buffer            *bp1 = nb1->nb_xprt_private;
	struct nlx_core_buffer       *lcbuf1 = &bp1->xb_core;
	struct nlx_kcore_transfer_mc  *kctm1 = lctm1->ctm_kpvt;
	struct nlx_kcore_buffer        *kcb1 = lcbuf1->cb_kpvt;
	lnet_md_t umd;
	int needed;
	uint32_t tmid;
	uint32_t portal;
	unsigned len;
	unsigned offset;

	/* TEST
	   Check that the lnet_md_t is properly constructed from a registered
	   network buffer.
	 */
	nb1->nb_min_receive_size = UT_MSG_SIZE;
	nb1->nb_max_receive_msgs = 1;
	nb1->nb_qtype = C2_NET_QT_MSG_RECV;

	nlx_kcore_umd_init(lctm1, lcbuf1, 1, 1, 0, &umd);
	C2_UT_ASSERT(umd.start == kcb1->kb_kiov);
	C2_UT_ASSERT(umd.length == kcb1->kb_kiov_len);
	C2_UT_ASSERT(umd.options & LNET_MD_KIOV);
	C2_UT_ASSERT(umd.user_ptr == lcbuf1);
	C2_UT_ASSERT(LNetHandleIsEqual(umd.eq_handle, kctm1->ktm_eqh));

	/* TEST
	   Enqueue a buffer for send.
	   Check that buf_msg_recv sends the correct arguments to LNet.
	   Check that the needed count is correctly incremented.
	   Intercept the utils sub to validate.
	*/
	nlx_kcore_iv._nlx_kcore_LNetMDAttach = ut_ktest_msg_LNetMDAttach1;
	ut_ktest_msg_LNetMDAttach1_called = false;

	nb1->nb_min_receive_size = UT_MSG_SIZE;
	nb1->nb_max_receive_msgs = UT_KMSG_OPS;
	nb1->nb_qtype = C2_NET_QT_MSG_RECV;
	needed = lctm1->ctm_bev_needed;

	c2_net_lnet_tm_set_debug(TM1, 2);
	zUT(c2_net_buffer_add(nb1, TM1), done);
	C2_UT_ASSERT(ut_ktest_msg_LNetMDAttach1_called);
	C2_UT_ASSERT(lctm1->ctm_bev_needed == needed + UT_KMSG_OPS);
	C2_UT_ASSERT(nb1->nb_flags & C2_NET_BUF_QUEUED);

	/* TEST
	   Send a sequence of events.
	   The buffer should not get dequeued until the last event, even
	   if there are intermediate failures.
	   The length and offset reported should be as sent.
	 */
	ut_cbreset();
	cb_save_ep1 = true;
	tmid = 3;
	portal = 35;
	offset = 0;
	len = 1;
	c2_clink_add(&TM1->ntm_chan, &td->tmwait1);
	NLXDBGP(lctm1, 2, "Posting message (%d, %d)\n", (int)portal, (int)tmid);
	ut_ktest_msg_post_event(lcbuf1, len, offset, 0, 0,
				nlx_kcore_hdr_data_encode_raw(tmid, portal));
	c2_chan_wait(&td->tmwait1);
	c2_clink_del(&td->tmwait1);
	C2_UT_ASSERT(cb_nb1 == nb1);
	C2_UT_ASSERT(cb_qt1 == C2_NET_QT_MSG_RECV);
	C2_UT_ASSERT(cb_status1 == 0);
	C2_UT_ASSERT(cb_length1 == len);
	C2_UT_ASSERT(cb_offset1 == offset);
	C2_UT_ASSERT(cb_ep1 != NULL);
	NLXDBGP(lctm1, 2, "ep: %s\n", cb_ep1->nep_addr);
	zUT(c2_net_end_point_put(cb_ep1), done);
	cb_ep1 = NULL;
	C2_UT_ASSERT(nb1->nb_flags & C2_NET_BUF_QUEUED);

	ut_cbreset();
	cb_save_ep1 = true;
	tmid = 9;
	portal = 37;
	offset += len;
	len = 10;
	c2_clink_add(&TM1->ntm_chan, &td->tmwait1);
	NLXDBGP(lctm1, 2, "Posting message (%d, %d)\n", (int)portal, (int)tmid);
	ut_ktest_msg_post_event(lcbuf1, len, offset, 0, 0,
				nlx_kcore_hdr_data_encode_raw(tmid, portal));
	c2_chan_wait(&td->tmwait1);
	c2_clink_del(&td->tmwait1);
	C2_UT_ASSERT(cb_nb1 == nb1);
	C2_UT_ASSERT(cb_qt1 == C2_NET_QT_MSG_RECV);
	C2_UT_ASSERT(cb_status1 == 0);
	C2_UT_ASSERT(cb_length1 == len);
	C2_UT_ASSERT(cb_offset1 == offset);
	C2_UT_ASSERT(cb_ep1 != NULL);
	NLXDBGP(lctm1, 2, "ep: %s\n", cb_ep1->nep_addr);
	zUT(c2_net_end_point_put(cb_ep1), done);
	cb_ep1 = NULL;
	C2_UT_ASSERT(nb1->nb_flags & C2_NET_BUF_QUEUED);

	ut_cbreset();
	cb_save_ep1 = true;
	tmid = 12;
	portal = 45;
	offset += len;
	len = 11;
	c2_clink_add(&TM1->ntm_chan, &td->tmwait1);
	NLXDBGP(lctm1, 2, "Posting message (%d, %d)\n", (int)portal, (int)tmid);
	ut_ktest_msg_post_event(lcbuf1, len, offset, 0, 1,
				nlx_kcore_hdr_data_encode_raw(tmid, portal));
	c2_chan_wait(&td->tmwait1);
	c2_clink_del(&td->tmwait1);
	C2_UT_ASSERT(cb_nb1 == nb1);
	C2_UT_ASSERT(cb_qt1 == C2_NET_QT_MSG_RECV);
	C2_UT_ASSERT(cb_status1 == 0);
	C2_UT_ASSERT(cb_length1 == len);
	C2_UT_ASSERT(cb_offset1 == offset);
	C2_UT_ASSERT(cb_ep1 != NULL);
	NLXDBGP(lctm1, 2, "ep: %s\n", cb_ep1->nep_addr);
	zUT(c2_net_end_point_put(cb_ep1), done);
	cb_ep1 = NULL;

	C2_UT_ASSERT(!(nb1->nb_flags & C2_NET_BUF_QUEUED));
 done:
	cb_ep1 = NULL;
	c2_net_lnet_tm_set_debug(TM1, 0);
}

static void ktest_msg(void) {
	ut_save_subs();
	ut_test_framework(&ktest_msg_body);
	ut_restore_subs();
}

#undef UT_BUFVEC_FREE
#undef UT_BUFVEC_ALLOC

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 79
 *  scroll-step: 1
 *  End:
 */
