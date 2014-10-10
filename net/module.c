/*
 * COPYRIGHT 2014 XYRATEX TECHNOLOGY LIMITED
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
 * Original creation date: 18-Jan-2014
 */

#include "net/module.h"
#include "module/instance.h"
#include "net/lnet/lnet.h"    /* m0_net_lnet_xprt */
#include "net/bulk_mem.h"     /* m0_net_bulk_mem_xprt */
#include "lib/errno.h"        /* EINVAL */

static int  level_net_enter(struct m0_module *module);
static void level_net_leave(struct m0_module *module);
static int  level_net_xprt_enter(struct m0_module *module);
static void level_net_xprt_leave(struct m0_module *module);

static const struct m0_modlev levels_net[] = {
	[M0_LEVEL_NET] = {
		.ml_name  = "net is initialised",
		.ml_enter = level_net_enter,
		.ml_leave = level_net_leave
	}
};

static const struct m0_modlev levels_net_xprt[] = {
	[M0_LEVEL_NET_DOMAIN] = {
		.ml_name  = "net_domain is initialised",
		.ml_enter = level_net_xprt_enter,
		.ml_leave = level_net_xprt_leave
	}
};

static struct {
	const char         *name;
	struct m0_net_xprt *xprt;
} net_xprt_mods[] = {
	[M0_NET_XPRT_LNET] = {
		.name = "lnet net_xprt module",
		.xprt = &m0_net_lnet_xprt
	},
	[M0_NET_XPRT_BULKMEM] = {
		.name = "bulk-mem net_xprt module",
		.xprt = &m0_net_bulk_mem_xprt
	},
};
M0_BASSERT(ARRAY_SIZE(net_xprt_mods) ==
	   ARRAY_SIZE(((struct m0_net *)0)->n_xprts));

M0_INTERNAL void m0_net_module_setup(struct m0_net *net)
{
	struct m0        *instance = M0_AMB(instance, net, i_net);
	struct m0_module *m;
	unsigned          i;

	m0_module_setup(&net->n_module, "net module",
			levels_net, ARRAY_SIZE(levels_net));
	net->n_module.m_m0 = instance;
	for (i = 0; i < ARRAY_SIZE(net->n_xprts); ++i) {
		m = &net->n_xprts[i].nx_module;
		m0_module_setup(m, net_xprt_mods[i].name, levels_net_xprt,
				ARRAY_SIZE(levels_net_xprt));
		m->m_m0 = instance;
		m0_module_dep_add(m, M0_LEVEL_NET_DOMAIN,
				  &net->n_module, M0_LEVEL_NET);
	}
}

static bool
is_net_of(const struct m0_module *net_module, const struct m0 *instance)
{
	const struct m0_net *net = M0_AMB(net, net_module, n_module);
	const struct m0 *m0 = M0_AMB(m0, net, i_net);

	return m0 == instance;
}

static int level_net_enter(struct m0_module *module)
{
	struct m0_net *net = M0_AMB(net, module, n_module);
	unsigned       i;

	M0_PRE(is_net_of(module, module->m_m0));

	/* We could have introduced a dedicated level for assigning
	 * m0_net_xprt_module::nx_xprt pointers, but assigning them
	 * here is good enough. */
	for (i = 0; i < ARRAY_SIZE(net_xprt_mods); ++i)
		net->n_xprts[i].nx_xprt = net_xprt_mods[i].xprt;

#if 0 /* XXX TODO
       * Rename current m0_net_init() to m0_net__init(), exclude it
       * from subsystem[] of mero/init.c, and ENABLEME. */
	return m0_net__init();
#else
	return 0;
#endif
}

static void level_net_leave(struct m0_module *module)
{
	M0_PRE(is_net_of(module, module->m_m0));
#if 0 /* XXX TODO
       * Rename current m0_net_fini() to m0_net__fini(), exclude it
       * from subsystem[] of mero/init.c, and ENABLEME. */
	m0_net__fini();
#endif
}

static int level_net_xprt_enter(struct m0_module *module)
{
	struct m0_net_xprt_module *m = M0_AMB(m, module, nx_module);

	M0_PRE(module->m_cur + 1 == M0_LEVEL_NET_DOMAIN);
	return m0_net_domain_init(&m->nx_domain, m->nx_xprt, &m0_addb_proc_ctx);
}

static void level_net_xprt_leave(struct m0_module *module)
{
	struct m0_net_xprt_module *m = M0_AMB(m, module, nx_module);

	M0_PRE(module->m_cur == M0_LEVEL_NET_DOMAIN);
	m0_net_domain_fini(&m->nx_domain);
}
