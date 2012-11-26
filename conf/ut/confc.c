/* -*- c -*- */
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
 * Original author: Valery V. Vorotyntsev <valery_vorotyntsev@xyratex.com>
 * Original creation date: 17-Sep-2012
 */

#include "conf/confc.h"
#include "conf/buf_ext.h" /* c2_buf_streq */
#include "conf/conf_xcode.h"
#include "lib/ut.h"

static struct c2_sm_group g_grp;

/*
 * CAUTION: This is not a proper format of configuration string!
 *
 * Double quotes are substituted with single quotes to improve
 * readability (escaping double quotes with backslashes adds too much
 * noise). This string needs to be processed with
 * conf_source_normalize() prior to passing to c2_confc_init().
 */
static char g_conf_source[] = "local-conf:"
"[6: ('prof', {1| ('fs')}),\n"
"    ('fs', {2| ((11, 22),\n"
"                [3: 'par1', 'par2', 'par3'],\n"
"                [3: 'svc-0', 'svc-1', 'svc-2'])}),\n"
"    ('svc-0', {3| (1, [1: 'addr0'], 'node-0')}),\n"
"    ('svc-1', {3| (3, [3: 'addr1', 'addr2', 'addr3'], 'node-1')}),\n"
"    ('svc-2', {3| (2, [0], 'node-1')}),\n"
"    ('node-0', {4| (8000, 2, 3, 2, 0, [2: 'nic-0', 'nic-1'],\n"
"                    [1: 'sdev-0'])})]\n";

struct waiter {
	struct c2_confc_ctx w_ctx;
	struct c2_clink     w_clink;
};

static void waiter_init(struct waiter *w, struct c2_confc *confc);
static void waiter_fini(struct waiter *w);

static void test_confc_preload(void)
{
	struct c2_confc confc;
	int             rc;

	rc = c2_confc_init(&confc, "local-conf:bad configuration string",
			   &(const struct c2_buf)C2_BUF_INITS("_"), &g_grp);
	C2_UT_ASSERT(rc != 0);
	C2_UT_ASSERT(confc.cc_root == NULL);

	rc = c2_confc_init(&confc, g_conf_source,
			   &(const struct c2_buf)C2_BUF_INITS("prof"), &g_grp);
	C2_UT_ASSERT(rc == 0);

	c2_confc_fini(&confc);
}

static bool streq(const char *s1, const char *s2)
{
	return strcmp(s1, s2) == 0;
}

static void test_confc_open(void)
{
	struct c2_confc         confc;
	struct waiter           w;
	struct c2_conf_service *svc;
	int                     rc;

	rc = c2_confc_init(&confc, g_conf_source,
			   &(const struct c2_buf)C2_BUF_INITS("prof"), &g_grp);
	C2_UT_ASSERT(rc == 0);

	waiter_init(&w, &confc);

	rc = c2_confc_open(&w.w_ctx, NULL, C2_BUF_INITS("filesystem"),
			   C2_BUF_INITS("services"), C2_BUF_INITS("svc-0"));
	C2_UT_ASSERT(rc == 0);

	while (!c2_confc_ctx_is_completed(&w.w_ctx))
		c2_chan_wait(&w.w_clink);

	rc = c2_confc_ctx_error(&w.w_ctx);
	C2_UT_ASSERT(rc == 0);

	svc = C2_CONF_CAST(c2_confc_ctx_result(&w.w_ctx), c2_conf_service);
	C2_UT_ASSERT(svc->cs_obj.co_status == C2_CS_READY);
	C2_UT_ASSERT(svc->cs_obj.co_confc == &confc);
	C2_UT_ASSERT(c2_buf_streq(&svc->cs_obj.co_id, "svc-0"));
	C2_UT_ASSERT(svc->cs_type == 1);
	C2_UT_ASSERT(streq(svc->cs_endpoints[0], "addr0"));
	C2_UT_ASSERT(svc->cs_endpoints[1] == NULL);
	C2_UT_ASSERT(c2_buf_streq(&svc->cs_node->cn_obj.co_id, "node-0"));

	waiter_fini(&w);

	{ /* Synchronous calls. */
		struct c2_conf_obj *obj;
		struct c2_conf_obj *node_obj;

		rc = c2_confc_open_sync(&obj, confc.cc_root,
					C2_BUF_INITS("filesystem"),
					C2_BUF_INITS("services"),
					C2_BUF_INITS("svc-0"));
		C2_UT_ASSERT(rc == 0);
		C2_UT_ASSERT(obj == &svc->cs_obj);

		rc = c2_confc_open_sync(&node_obj, obj, C2_BUF_INITS("node"));
		C2_UT_ASSERT(rc == 0);
		C2_UT_ASSERT(node_obj == &svc->cs_node->cn_obj);

		c2_confc_close(obj);
		c2_confc_close(node_obj);
	}

	c2_confc_close(&svc->cs_obj);
	c2_confc_fini(&confc);
}

static struct {
	bool             run;
	struct c2_thread thread;
} g_ast;

static void ast_thread(int _ __attribute__((unused)))
{
	while (g_ast.run) {
		c2_chan_wait(&g_grp.s_clink);
		c2_sm_group_lock(&g_grp);
		c2_sm_asts_run(&g_grp);
		c2_sm_group_unlock(&g_grp);
	}
}

/** Normalizes `g_conf_source' string, replacing ' with ". */
static void conf_source_normalize(void)
{
	static bool done = false;
	char       *p;

	if (!done) {
		for (p = g_conf_source; *p != '\0'; ++p) {
			if (*p == '\'')
				*p = '"';
		}
		done = true;
	}
}

static int ast_thread_init(void)
{
	int rc;

	conf_source_normalize();

	c2_sm_group_init(&g_grp);
	g_ast.run = true;
	rc = C2_THREAD_INIT(&g_ast.thread, int, NULL, &ast_thread, 0,
			    "ast_thread");
	C2_ASSERT(rc == 0);

	return 0;
}

static int ast_thread_fini(void)
{
	g_ast.run = false;
	c2_clink_signal(&g_grp.s_clink);
	c2_thread_join(&g_ast.thread);
	c2_sm_group_fini(&g_grp);

	return 0;
}

/* Filters out intermediate state transitions of c2_confc_ctx::fc_mach. */
static bool filter(struct c2_clink *link)
{
	return !c2_confc_ctx_is_completed(&container_of(link, struct waiter,
							w_clink)->w_ctx);
}

static void waiter_init(struct waiter *w, struct c2_confc *confc)
{
	c2_confc_ctx_init(&w->w_ctx, confc);
	c2_clink_init(&w->w_clink, filter);
	c2_clink_add(&w->w_ctx.fc_mach.sm_chan, &w->w_clink);
}

static void waiter_fini(struct waiter *w)
{
	c2_clink_del(&w->w_clink);
	c2_clink_fini(&w->w_clink);
	c2_confc_ctx_fini(&w->w_ctx);
}

const struct c2_test_suite confc_ut = {
	.ts_name  = "confc-ut",
	.ts_init  = ast_thread_init,
	.ts_fini  = ast_thread_fini,
	.ts_tests = {
		{ "confc-preload", test_confc_preload },
		{ "confc-open",    test_confc_open },
		{ NULL, NULL }
	}
};
