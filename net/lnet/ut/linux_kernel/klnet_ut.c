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
 * Original creation date: 1/9/2012
 */

/* Kernel specific LNet unit tests.
 * The tests cases are loaded from the address space agnostic ../lnet_ut.c
 * file.
 */

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

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 79
 *  scroll-step: 1
 *  End:
 */
