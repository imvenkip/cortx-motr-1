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

static int  level_net_enter(struct m0_module *module);
static void level_net_leave(struct m0_module *module);
static int  level_net_dep_enter(struct m0_module *module);
static int  level_net_xprt_enter(struct m0_module *module);
static void level_net_xprt_leave(struct m0_module *module);
static int  level_net_domain_enter(struct m0_module *module);
static void level_net_domain_leave(struct m0_module *module);

static struct m0_modlev levels_net[] = {
	[M0_LEVEL_NET] = {
		.ml_name  = "net is initialised",
		.ml_enter = level_net_enter,
		.ml_leave = level_net_leave
	}
};

static struct m0_modlev levels_net_xprt[] = {
	[M0_LEVEL_NET_DEP] = {
		.ml_name  = "net_xprt depends on net",
		.ml_enter = level_net_dep_enter
	},
	[M0_LEVEL_NET_XPRT] = {
		.ml_name  = "net_xprt is initialised",
		.ml_enter = level_net_xprt_enter,
		.ml_leave = level_net_xprt_leave
	},
	[M0_LEVEL_NET_DOMAIN] = {
		.ml_name  = "net_domain is initialised",
		.ml_enter = level_net_domain_enter,
		.ml_leave = level_net_domain_leave
	}
};

M0_INTERNAL void m0_net_modules_setup(struct m0_net *net)
{
	static struct {
		const char         *name;
		struct m0_net_xprt *ptr;
	} xprts[] = {
		[M0_NET_XPRT_LNET] = {
			.name = "lnet net_xprt module",
			.ptr  = &m0_net_lnet_xprt
		},
		[M0_NET_XPRT_BULK_MEM] = {
			.name = "bulk-mem net_xprt module",
			.ptr  = &m0_net_bulk_mem_xprt
		}
	};
	struct m0 *instance = m0_get();
	size_t     i;

	M0_PRE(M0_IN(instance, (NULL, container_of(net, struct m0, i_net))));

	net->n_module = (struct m0_module){
		.m_name     = "net module",
		.m_m0       = instance,
		.m_level    = levels_net,
		.m_level_nr = ARRAY_SIZE(levels_net)
	};

	for (i = 0; i < ARRAY_SIZE(net->n_xprts); ++i) {
		M0_ASSERT(IS_IN_ARRAY(i, xprts));
		net->n_xprts[i] = (struct m0_net_xprt_module){
			.nx_module = {
				.m_name     = xprts[i].name,
				.m_m0       = instance,
				.m_level    = levels_net_xprt,
				.m_level_nr = ARRAY_SIZE(levels_net_xprt)
			},
			.nx_xprt = xprts[i].ptr
		};
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
	M0_PRE(is_net_of(module, module->m_m0));
#if 0 /* XXX ENABLEME after m0_net_init is excluded from subsystem[]
       * of mero/init.c. */
	return m0_net_init();
#else
	return 0;
#endif
}

static void level_net_leave(struct m0_module *module)
{
	M0_PRE(is_net_of(module, module->m_m0));
#if 0 /* XXX ENABLEME after m0_net_fini is excluded from subsystem[]
       * of mero/init.c. */
	m0_net_fini();
#endif
}

static int level_net_dep_enter(struct m0_module *module)
{
	struct m0 *instance = m0_get();

	m0_module_dep_add(module, M0_LEVEL_NET_XPRT,
			  &instance->i_net.n_module, M0_LEVEL_NET);
	return 0;
}

static struct m0_net_xprt_module *net_xprt_module(struct m0_module *module)
{
	return container_of(module, struct m0_net_xprt_module, nx_module);
}

static int level_net_xprt_enter(struct m0_module *module)
{
	return m0_net_xprt_init(net_xprt_module(module)->nx_xprt);
}

static void level_net_xprt_leave(struct m0_module *module)
{
	m0_net_xprt_fini(net_xprt_module(module)->nx_xprt);
}

static int level_net_domain_enter(struct m0_module *module)
{
	struct m0_net_xprt_module *m = net_xprt_module(module);
	return m0_net_domain_init(&m->nx_domain, m->nx_xprt, &m0_addb_proc_ctx);
}

static void level_net_domain_leave(struct m0_module *module)
{
	m0_net_domain_fini(&net_xprt_module(module)->nx_domain);
}
