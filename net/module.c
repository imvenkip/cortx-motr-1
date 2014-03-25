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

struct m0_modlev levels_net[M0_LEVEL_NET__NR] = {
	[M0_LEVEL_NET] = {
		.ml_name  = "net is initialised",
		.ml_enter = level_net_enter,
		.ml_leave = level_net_leave
	}
};

struct m0_modlev levels_net_xprt[M0_LEVEL_NET_XPRT__NR] = {
	[M0_LEVEL_NET_DEP] = {
		.ml_name  = "net_xprt depends on net",
		.ml_enter = level_net_xprt_enter
	},
	[M0_LEVEL_NET_XPRT] = {
		.ml_name  = "net_xprt is initialised",
		.ml_enter = level_net_xprt_enter,
		.ml_leave = level_net_xprt_leave
	},
	[M0_LEVEL_NET_DOMAIN] = {
		.ml_name  = "net_domain is initialised",
		.ml_enter = level_net_xprt_enter,
		.ml_leave = level_net_xprt_leave
	}
};

M0_INTERNAL void m0_net_module_init(struct m0_net *net)
{
	struct m0 *instance = M0_AMB(instance, net, i_net);

#define NET_XPRT_INIT(short_name, instance) {                              \
		.nx_module = M0_MODULE_INIT(short_name " net_xprt module", \
					    (instance), levels_net_xprt,   \
					    ARRAY_SIZE(levels_net_xprt))   \
	}
	*net = (struct m0_net){
		.n_module = M0_MODULE_INIT(
			"net module", instance,
			levels_net, ARRAY_SIZE(levels_net),
			M0_MODULE_INVS((&instance->i_self,
					M0_LEVEL_INIT, M0_LEVEL_NET))),
		.n_xprts = {
			[M0_NET_XPRT_LNET] = NET_XPRT_INIT("lnet", instance),
			[M0_NET_XPRT_BULK_MEM] = NET_XPRT_INIT("bulk-mem",
							       instance)
		}
	};
#undef NET_XPRT_INIT
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
	static struct m0_net_xprt *xprts[] = {
		[M0_NET_XPRT_LNET]     = &m0_net_lnet_xprt,
		[M0_NET_XPRT_BULK_MEM] = &m0_net_bulk_mem_xprt
	};
	struct m0_net *net = M0_AMB(net, module, n_module);
	unsigned       i;

	M0_PRE(is_net_of(module, module->m_m0));

	/* We could have introduced a dedicated level for assigning
	 * m0_net_xprt_module::nx_xprt pointers, but assigning them
	 * here is good enough. */
	for (i = 0; i < ARRAY_SIZE(xprts); ++i)
		net->n_xprts[i].nx_xprt = xprts[i];

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

static struct m0_net_xprt_module *net_xprt_module(struct m0_module *module)
{
	return container_of(module, struct m0_net_xprt_module, nx_module);
}

static int level_net_xprt_enter(struct m0_module *module)
{
	switch (module->m_cur + 1) {
	case M0_LEVEL_NET_DEP:
		m0_module_dep_add(module, M0_LEVEL_NET_XPRT,
				  &module->m_m0->i_net.n_module, M0_LEVEL_NET);
		return 0;

	case M0_LEVEL_NET_XPRT:
		return m0_net_xprt_init(net_xprt_module(module)->nx_xprt);

	case M0_LEVEL_NET_DOMAIN: {
		struct m0_net_xprt_module *m = net_xprt_module(module);
		return m0_net_domain_init(&m->nx_domain, m->nx_xprt,
					  &m0_addb_proc_ctx);
	}
	default:
		return M0_IMPOSSIBLE(""), -EINVAL;
	}
}

static void level_net_xprt_leave(struct m0_module *module)
{
	switch (module->m_cur) {
	case M0_LEVEL_NET_DOMAIN:
		m0_net_domain_fini(&net_xprt_module(module)->nx_domain);
		break;
	case M0_LEVEL_NET_XPRT:
		m0_net_xprt_fini(net_xprt_module(module)->nx_xprt);
		break;
	case M0_LEVEL_NET_DEP:
		break;
	default:
		M0_IMPOSSIBLE("");
	}
}
