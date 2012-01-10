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
 * Original creation date: 1/9/2012
 */

/* Kernel specific LNet unit tests.
 * The tests cases are loaded from the address space agnostic ../lnet_ut.c
 * file.
 */

static void ktest_buf_shape(void)
{
	static struct c2_net_domain dom1 = {
		.nd_xprt = NULL
	};

	C2_UT_ASSERT(!c2_net_domain_init(&dom1, &c2_net_lnet_xprt));

	/* buffer shape APIs */
	C2_UT_ASSERT(c2_net_domain_get_max_buffer_size(&dom1)
		     == LNET_MAX_PAYLOAD);
	C2_UT_ASSERT(c2_net_domain_get_max_buffer_segment_size(&dom1)
		     == PAGE_SIZE);
	C2_UT_ASSERT(c2_net_domain_get_max_buffer_segments(&dom1)
		     == LNET_MAX_IOV);

	/* fini */
	c2_net_domain_fini(&dom1);
}

static void ktest_buf_reg(void)
{
	static struct c2_net_domain dom1 = {
		.nd_xprt = NULL
	};
	static struct c2_net_buffer nb1;
	c2_bcount_t bsize;
	c2_bcount_t bsegsize;
	int32_t     bsegs;
	struct nlx_xo_buffer *xb;
	struct nlx_core_buffer *cb;
	struct nlx_kcore_buffer *kcb;
	int i;

	C2_UT_ASSERT(!c2_net_domain_init(&dom1, &c2_net_lnet_xprt));

	/* Allocate a bufvec into a buffer.
	   Maximal and perfectly aligned on page boundaries.
	 */
	bsize    = LNET_MAX_PAYLOAD;
	bsegsize = PAGE_SIZE;
	bsegs    = LNET_MAX_IOV;
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
	kcb = cb->cb_kpvt;
	C2_UT_ASSERT(kcb->kb_magic == C2_NET_LNET_KCORE_BUF_MAGIC);
	C2_UT_ASSERT(kcb->kb_min_recv_size == 0);
	C2_UT_ASSERT(kcb->kb_max_recv_msgs == 0);
	C2_UT_ASSERT(LNetHandleIsInvalid(kcb->kb_mdh));
	C2_UT_ASSERT(kcb->kb_kiov != NULL);
	C2_UT_ASSERT(kcb->kb_kiov_len == bsegs);
	for (i = 0; i < kcb->kb_kiov_len; ++i) {
	  void *addr;
	  C2_UT_ASSERT(kcb->kb_kiov[i].kiov_len == bsegsize);
	  C2_UT_ASSERT(kcb->kb_kiov[i].kiov_offset == 0);
	  addr = page_address(kcb->kb_kiov[i].kiov_page);
	  C2_UT_ASSERT(addr == nb1.nb_buffer.ov_buf[i]);
	}

	/* fini */
	c2_net_buffer_deregister(&nb1, &dom1);
	C2_UT_ASSERT(!(nb1.nb_flags & C2_NET_BUF_REGISTERED));
	C2_UT_ASSERT(nb1.nb_xprt_private == NULL);

	c2_bufvec_free(&nb1.nb_buffer);
	c2_net_domain_fini(&dom1);
}
