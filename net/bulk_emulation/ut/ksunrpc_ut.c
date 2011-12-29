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
 * Original author: Dave Cohrs <Dave_Cohrs@us.xyratex.com>
 * Original creation date: 06/14/2011
 */

#include "lib/memory.h"
#include "lib/misc.h"
#include "lib/ut.h"
#include "net/bulk_emulation/sunrpc_xprt.h"
#include "net/ksunrpc/ksunrpc.h"
#include "fop/fop.h"

#include <linux/highmem.h> /* kmap, kunmap */
#include <linux/pagemap.h> /* PAGE_CACHE_SIZE */

enum {
	NUM = 100,
	IPADDR = 0x7f000001,	/* 127.0.0.1 */
	PORT = 10001,
	FAKEPORT = 10701,
	S_EPID = 42,
	C_EPID = 24
};

static int sunrpc_ut_get_handler(struct c2_fop *fop, struct c2_fop_ctx *ctx);
static int sunrpc_ut_put_handler(struct c2_fop *fop, struct c2_fop_ctx *ctx);

static struct c2_fop_type_ops sunrpc_get_ops = {
	.fto_execute = sunrpc_ut_get_handler,
};

static struct c2_fop_type_ops sunrpc_put_ops = {
	.fto_execute = sunrpc_ut_put_handler,
};

static struct c2_net_domain dom;

extern struct c2_fop_type sunrpc_get_fopt; /* opcode = 31 */
extern struct c2_fop_type sunrpc_put_fopt; /* opcode = 32 */

extern struct c2_fop_type sunrpc_get_resp_fopt; /* opcode = 36 */
extern struct c2_fop_type sunrpc_put_resp_fopt; /* opcode = 37 */

static struct c2_fop_type *s_fops[] = {
	&sunrpc_get_fopt,
	&sunrpc_put_fopt,
};

static char *get_put_buf;

static int sunrpc_ut_get_handler(struct c2_fop *fop, struct c2_fop_ctx *ctx)
{
	struct sunrpc_get         *in = c2_fop_data(fop);
	struct sunrpc_get_resp    *ex;
	struct c2_fop             *reply;
	c2_bcount_t                i;
	int                        rc;
	struct c2_bufvec           inbuf =
		C2_BUFVEC_INIT_BUF((void **)&get_put_buf, &i);
	struct c2_bufvec_cursor    incur;

	reply = c2_fop_alloc(&sunrpc_get_resp_fopt, NULL);
	C2_UT_ASSERT(reply != NULL);
	ex = c2_fop_data(reply);
	i = in->sg_desc.sbd_id;

	C2_UT_ASSERT(in->sg_offset == 0);
	C2_UT_ASSERT(in->sg_desc.sbd_passive_ep.sep_addr == IPADDR);
	C2_UT_ASSERT(in->sg_desc.sbd_passive_ep.sep_port == FAKEPORT);
	C2_UT_ASSERT(in->sg_desc.sbd_passive_ep.sep_id == C_EPID);
	C2_UT_ASSERT(in->sg_desc.sbd_qtype == C2_NET_QT_ACTIVE_BULK_SEND);
	C2_UT_ASSERT(in->sg_desc.sbd_total == i);

	ex->sgr_rc = i + 1;
	ex->sgr_eof = 1;
	c2_bufvec_cursor_init(&incur, &inbuf);
	rc = sunrpc_buffer_init(&ex->sgr_buf, &incur, i);
	C2_UT_ASSERT(rc == 0);
	C2_UT_ASSERT(ex->sgr_buf.sb_buf != NULL);
	C2_UT_ASSERT(ex->sgr_buf.sb_len == i);

	c2_net_reply_post(ctx->ft_service, reply, ctx->fc_cookie);
	return 0;
}

static int sunrpc_ut_put_handler(struct c2_fop *fop, struct c2_fop_ctx *ctx)
{
	struct sunrpc_put         *in = c2_fop_data(fop);
	struct sunrpc_put_resp    *ex;
	struct c2_fop             *reply;
	int                        i;
	const char                *buf;

	reply = c2_fop_alloc(&sunrpc_put_resp_fopt, NULL);
	C2_UT_ASSERT(reply != NULL);
	ex = c2_fop_data(reply);
	i = in->sp_desc.sbd_id;

	C2_UT_ASSERT(in->sp_offset == 0);
	C2_UT_ASSERT(in->sp_desc.sbd_passive_ep.sep_addr == IPADDR);
	C2_UT_ASSERT(in->sp_desc.sbd_passive_ep.sep_port == FAKEPORT);
	C2_UT_ASSERT(in->sp_desc.sbd_passive_ep.sep_id == C_EPID);
	C2_UT_ASSERT(in->sp_desc.sbd_qtype == C2_NET_QT_ACTIVE_BULK_RECV);
	C2_UT_ASSERT(in->sp_desc.sbd_total == i);
	C2_UT_ASSERT(in->sp_buf.sb_len == i);

	buf = kmap(in->sp_buf.sb_buf[0]);
	buf += in->sp_buf.sb_pgoff;
	C2_UT_ASSERT(memcmp(buf, get_put_buf, i) == 0);
	kunmap(in->sp_buf.sb_buf[0]);

	ex->spr_rc = i + 1;
	c2_net_reply_post(ctx->ft_service, reply, ctx->fc_cookie);
	return 0;
}

static int ksunrpc_service_handler(struct c2_service *service,
				   struct c2_fop *fop,
				   void *cookie)
{
        struct c2_fop_ctx ctx;

        ctx.ft_service = service;
        ctx.fc_cookie  = cookie;
        return fop->f_type->ft_ops->fto_execute(fop, &ctx);
}

int get_call(struct c2_net_conn *conn, int i)
{
	int                      rc;
	char                    *buf;
	struct c2_fop           *f;
	struct c2_fop           *r;
	struct sunrpc_get       *fop;
	struct sunrpc_get_resp  *rep;
	struct sunrpc_buf_desc   fake_desc = {
		.sbd_id = i,
		.sbd_passive_ep.sep_addr = IPADDR,
		.sbd_passive_ep.sep_port = FAKEPORT,
		.sbd_passive_ep.sep_id = C_EPID,
		.sbd_qtype = C2_NET_QT_ACTIVE_BULK_SEND,
		.sbd_total = i
	};

	C2_UT_ASSERT(conn != NULL);
	C2_UT_ASSERT(i >= 0);
	f = c2_fop_alloc(&sunrpc_get_fopt, NULL);
	r = c2_fop_alloc(&sunrpc_get_resp_fopt, NULL);
	if (f == NULL || r == NULL) {
		rc = -ENOMEM;
		goto done;
	}
	fop = c2_fop_data(f);

	fop->sg_desc = fake_desc;
	fop->sg_offset = 0;

	/* kxdr requires that caller pre-allocate buffers for sequences.
	   This assumes client knows the max required.  When call returns,
	   the actual length returned will be updated.
	 */
	rep = c2_fop_data(r);
	rc = sunrpc_buffer_init(&rep->sgr_buf, NULL, i);
	C2_UT_ASSERT(rc == 0);
	{
		struct c2_net_call call = {
			.ac_arg = f,
			.ac_ret = r
		};

		rc = c2_net_cli_call(conn, &call);
		C2_UT_ASSERT(rc == 0);
	}

	C2_UT_ASSERT(c2_fop_data(r) == rep);
	C2_UT_ASSERT(rep->sgr_rc == i + 1);
	C2_UT_ASSERT(rep->sgr_eof == 1);
	C2_UT_ASSERT(rep->sgr_buf.sb_len == i);
	buf = kmap(rep->sgr_buf.sb_buf[0]);
	C2_UT_ASSERT(memcmp(buf, get_put_buf, i) == 0);
	kunmap(rep->sgr_buf.sb_buf[0]);
	sunrpc_buffer_fini(&rep->sgr_buf);

done:
	if (r != NULL)
		c2_fop_free(r);
	if (f != NULL)
		c2_fop_free(f);
	return rc;
}

int put_call(struct c2_net_conn *conn, int i)
{
	int                      rc;
	struct c2_fop           *f;
	struct c2_fop           *r;
	struct sunrpc_put       *fop;
	struct sunrpc_put_resp  *rep;
	struct sunrpc_buf_desc   fake_desc = {
		.sbd_id = i,
		.sbd_passive_ep.sep_addr = IPADDR,
		.sbd_passive_ep.sep_port = FAKEPORT,
		.sbd_passive_ep.sep_id = C_EPID,
		.sbd_qtype = C2_NET_QT_ACTIVE_BULK_RECV,
		.sbd_total = i
	};
	c2_bcount_t              inlen = i;
	struct c2_bufvec         inbuf =
		C2_BUFVEC_INIT_BUF((void **)&get_put_buf, &inlen);
	struct c2_bufvec_cursor  incur;

	C2_UT_ASSERT(conn != NULL);
	C2_UT_ASSERT(i >= 0);
	f = c2_fop_alloc(&sunrpc_put_fopt, NULL);
	r = c2_fop_alloc(&sunrpc_put_resp_fopt, NULL);
	if (f == NULL || r == NULL) {
		rc = -ENOMEM;
		goto done;
	}
	fop = c2_fop_data(f);

	fop->sp_desc = fake_desc;
	{
		struct c2_net_call call = {
			.ac_arg = f,
			.ac_ret = r
		};
		fop->sp_offset = 0;
		c2_bufvec_cursor_init(&incur, &inbuf);
		rc = sunrpc_buffer_init(&fop->sp_buf, &incur, inlen);
		C2_UT_ASSERT(rc == 0);
		rc = c2_net_cli_call(conn, &call);
		if (rc == 0) {
			rep = c2_fop_data(r);
			C2_UT_ASSERT(rep->spr_rc == i + 1);
		}
		C2_UT_ASSERT(rc == 0);
		sunrpc_buffer_fini(&fop->sp_buf);
	}

done:
	if (r != NULL)
		c2_fop_free(r);
	if (f != NULL)
		c2_fop_free(f);
	return rc;
}

void test_ksunrpc_buffer(void)
{
	char *buf;
	char *bp;
	c2_bcount_t len = PAGE_CACHE_SIZE * NUM;
	int i;
	struct sunrpc_buffer sb;
	struct c2_bufvec inbuf = C2_BUFVEC_INIT_BUF((void **)&buf, &len);
	struct c2_bufvec_cursor incur;
	struct c2_bufvec outbuf = C2_BUFVEC_INIT_BUF((void **)&bp, &len);
	struct c2_bufvec_cursor outcur;
	int rc;

	buf = c2_alloc(len);
	C2_UT_ASSERT(buf != NULL);
	for (i = 0; i < NUM; ++i)
		buf[i * PAGE_CACHE_SIZE] = i;

	c2_bufvec_cursor_init(&incur, &inbuf);
	rc = sunrpc_buffer_init(&sb, &incur, len);
	C2_UT_ASSERT(rc == 0);
	for (i = 0; i < NUM; ++i) {
		C2_UT_ASSERT(page_count(sb.sb_buf[i]) == 1);
		bp = kmap(sb.sb_buf[i]);
		C2_UT_ASSERT(*bp == buf[i * PAGE_CACHE_SIZE]);
		kunmap(sb.sb_buf[i]);
	}
	bp = c2_alloc(len);
	c2_bufvec_cursor_init(&outcur, &outbuf);
	rc = sunrpc_buffer_copy_out(&outcur, &sb);
	C2_UT_ASSERT(rc == 0);
	C2_UT_ASSERT(memcmp(buf, bp, len) == 0);
	sunrpc_buffer_fini(&sb);
	c2_free(buf);
	c2_free(bp);
}

void test_ksunrpc_server(void)
{
	int rc;
	int i;

	struct c2_service_id sid1 = { .si_uuid = "node-1" };
	struct c2_net_conn *conn1;
	struct c2_service s1;
	c2_time_t t;

	/* NB: fops used are parsed when knet2 module is loaded,
	   override the ops vector for UT.
	 */
	C2_UT_ASSERT(sunrpc_get_fopt.ft_top != NULL);
	C2_UT_ASSERT(sunrpc_put_fopt.ft_top != NULL);
	sunrpc_get_fopt.ft_ops = &sunrpc_get_ops;
	sunrpc_put_fopt.ft_ops = &sunrpc_put_ops;

	C2_SET0(&s1);
	s1.s_table.not_start = s_fops[0]->ft_rpc_item_type.rit_opcode;
	s1.s_table.not_nr    = ARRAY_SIZE(s_fops);
	s1.s_table.not_fopt  = s_fops;
	s1.s_handler         = &ksunrpc_service_handler;

	rc = c2_net_xprt_init(&c2_net_ksunrpc_minimal_xprt);
	C2_UT_ASSERT(rc == 0);

	rc = c2_net_domain_init(&dom, &c2_net_ksunrpc_minimal_xprt);
	C2_UT_ASSERT(rc == 0);

	rc = c2_service_id_init(&sid1, &dom, "127.0.0.1", PORT);
	C2_UT_ASSERT(rc == 0);

	rc = c2_service_start(&s1, &sid1);
	C2_UT_ASSERT(rc >= 0);

	c2_nanosleep(c2_time_set(&t, 1, 0), NULL);

	rc = c2_net_conn_create(&sid1);
	C2_UT_ASSERT(rc == 0);

	conn1 = c2_net_conn_find(&sid1);
	C2_UT_ASSERT(conn1 != NULL);

	get_put_buf = c2_alloc(NUM + 1);
	C2_UT_ASSERT(get_put_buf != NULL);
	for (i = 0; i < NUM; ++i)
		get_put_buf[i] = i+1;

	/* call get API, tests receiving basic record and sequence */
	for (i = 0; i <= NUM; ++i) {
		rc = get_call(conn1, i);
		C2_UT_ASSERT(rc == 0);
	}

	/* call put API, tests sending basic record and sequence */
	for (i = 0; i <= NUM; ++i) {
		rc = put_call(conn1, i);
		C2_UT_ASSERT(rc == 0);
	}

	c2_free(get_put_buf);
	c2_net_conn_unlink(conn1);
	c2_net_conn_release(conn1);
	c2_service_stop(&s1);
	c2_service_id_fini(&sid1);
	c2_net_domain_fini(&dom);
	c2_net_xprt_fini(&c2_net_ksunrpc_minimal_xprt);
}

const struct c2_test_suite c2_net_ksunrpc_ut = {
        .ts_name = "ksunrpc-ut",
        .ts_init = NULL,
        .ts_fini = NULL,
        .ts_tests = {
                { "ksunrpc_buffer", test_ksunrpc_buffer },
                { "ksunrpc_server", test_ksunrpc_server },
                { NULL, NULL }
        }
};
C2_EXPORTED(c2_net_ksunrpc_ut);

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
