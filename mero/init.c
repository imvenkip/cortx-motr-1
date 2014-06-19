/* -*- C -*- */
/*
 * COPYRIGHT 2013 XYRATEX TECHNOLOGY LIMITED
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
 * Original author: Nikita Danilov <nikita_danilov@xyratex.com>
 * Original creation date: 06/19/2010
 */

#include "fop/fop.h"
#ifndef __KERNEL__
#  include "desim/sim.h"
#endif
#include "lib/trace.h"   /* m0_trace_init */
#include "lib/thread.h"
#include "stob/type.h"
#include "stob/stob.h"
#include "ut/stob.h"
#include "net/net.h"
#include "net/bulk_emulation/mem_xprt.h"
#include "net/lnet/lnet.h"
#include "rpc/rpc.h"
#include "addb/addb.h"
#include "lib/finject.h"
#include "lib/locality.h"
#include "layout/layout.h"
#include "pool/pool.h"
#include "lib/processor.h"
#include "db/db.h"
#include "sm/sm.h"
#include "dtm/dtm.h"
#include "fol/fol.h"
#include "dtm/dtm.h"
#include "reqh/reqh.h"
#include "lib/timer.h"
#include "fid/fid.h"
#include "fop/fom_simple.h"
#include "fop/fom_generic.h"
#include "fop/fop_rate_monitor.h"
#include "graph/graph.h"
#include "mero/init.h"
#include "lib/cookie.h"
#include "conf/fop.h"           /* m0_conf_fops_init, m0_confx_types_init */
#include "conf/obj.h"           /* m0_conf_obj_init */
#ifdef __KERNEL__
#  include "m0t1fs/linux_kernel/m0t1fs.h"
#  include "mero/linux_kernel/dummy_init_fini.h"
#  include "net/test/initfini.h" /* m0_net_test_init */
#else
#  include "be/tx_service.h"    /* m0_be_txs_register */
#  include "be/be.h"            /* m0_backend_init */
#  include "conf/confd.h"       /* m0_confd_register */
#  include "conf/addb.h"        /* m0_conf_addb_init */
#  include "mdstore/mdstore.h"  /* m0_mdstore_mod_init */
#endif
#include "cob/cob.h"
#include "ioservice/io_fops.h"
#include "ioservice/io_service.h"
#include "mdservice/md_fops.h"
#include "mdservice/md_service.h"
#include "rm/rm_service.h"
#include "stats/stats_srv.h"
#include "sns/sns.h"
#include "sns/parity_ops.h"
#include "sss/ss_svc.h"
#include "cm/cm.h"
#include "addb/addb_fops.h"
#include "stats/stats_fops.h"
#include "ha/epoch.h"
#include "module/instance.h"  /* m0_instance_init */

M0_INTERNAL int m0_utime_init(void);
M0_INTERNAL void m0_utime_fini(void);

M0_INTERNAL int m0_memory_init(void);
M0_INTERNAL void m0_memory_fini(void);

M0_INTERNAL int libm0_init(void);
M0_INTERNAL void libm0_fini(void);

/**
   @addtogroup init
   @{
 */

struct init_fini_call {
	int  (*ifc_init)(void);
	void (*ifc_fini)(void);
	const char *ifc_name;
};

/*
  XXX dummy_init_fini.c defines dummy init() and fini() routines for
  subsystems, that are not yet ported to kernel mode.
 */
struct init_fini_call subsystem[] = {
#ifndef __KERNEL__
	{ &m0_utime_init,       &m0_utime_fini,       "time" },
#endif
	{ &m0_trace_init,       &m0_trace_fini,       "trace" },
	{ &m0_fi_init,          &m0_fi_fini,          "finject" },
	{ &m0_memory_init,      &m0_memory_fini,      "memory" },
	{ &libm0_init,          &libm0_fini,          "libm0" },
	{ &m0_ha_global_init ,  &m0_ha_global_fini,   "ha" },
	{ &m0_fid_init,         &m0_fid_fini,         "fid" },
	{ &m0_cookie_global_init, &m0_cookie_global_fini, "cookie" },
	{ &m0_processors_init,  &m0_processors_fini,  "processors" },
	/* localities must be initialised before lib/processor.h */
	{ &m0_localities_init,  &m0_localities_fini,  "locality" },
	{ &m0_timers_init,      &m0_timers_fini,      "timer" },
	{ &m0_addb_init,        &m0_addb_fini,        "addb" },
	{ &m0_db_init,          &m0_db_fini,          "db" },
	{ &m0_fols_init,        &m0_fols_fini,        "fol" },
	{ &m0_layouts_init,     &m0_layouts_fini,     "layout" },
	/* fops must be initialised before network, because network build fop
	   type for network descriptors. */
	{ &m0_fops_init,        &m0_fops_fini,        "fop" },
	{ &m0_fop_rate_monitor_module_init, NULL,     "fop_rate_monitor" },
	{ &m0_net_init,         &m0_net_fini,         "net" },
#ifdef __KERNEL__
	{ &m0_net_test_init,    &m0_net_test_fini,    "net-test" },
#endif
	{ &m0_reqhs_init,       &m0_reqhs_fini,       "reqhs" },
	/* fom-simple must go after reqh init */
	{ &m0_fom_simples_init, &m0_fom_simples_fini, "fom-simple" },
	{ &m0_rpc_init,         &m0_rpc_fini,         "rpc" },
	/* fom generic must be after rpc, because it initialises rpc item
	   type for generic error reply. */
	{ &m0_fom_generic_init, &m0_fom_generic_fini, "fom-generic" },
	{ &m0_mem_xprt_init,    &m0_mem_xprt_fini,    "bulk/mem" },
	{ &m0_net_lnet_init,    &m0_net_lnet_fini,    "net/lnet" },
#ifndef __KERNEL__
	{ &m0_cob_mod_init,     &m0_cob_mod_fini,     "cob" },
	{ &m0_stob_types_init,  &m0_stob_types_fini,  "stob-types" },
	{ &m0_ut_stob_init,	&m0_ut_stob_fini,     "ut-stob" },
	{ &sim_global_init,     &sim_global_fini,     "desim" },
#endif
	{ &m0_addb_svc_mod_init, &m0_addb_svc_mod_fini, "addbsvc" },
	{ &m0_graph_mod_init,   &m0_graph_mod_fini,   "graph" },
	{ &m0_conf_obj_init,    &m0_conf_obj_fini,    "conf-obj" },
	{ &m0_confx_types_init, &m0_confx_types_fini, "conf-xtypes" },
	{ &m0_conf_fops_init,   &m0_conf_fops_fini,   "conf-fops" },
	{ &m0_addb_service_fop_init, &m0_addb_service_fop_fini, "addb_fops" },
	{ &m0_stats_fops_init,  &m0_stats_fops_fini,  "stats_fops"},
	{ &m0_rms_register,     &m0_rms_unregister,   "rmservice"},
#ifdef __KERNEL__
	{ &m0t1fs_init,         &m0t1fs_fini,         "m0t1fs" },
#else
	{ &m0_backend_init,     &m0_backend_fini,     "be" },
	{ &m0_be_txs_register,  &m0_be_txs_unregister, "be-tx-service" },
	{ &m0_confd_register,   &m0_confd_unregister, "confd" },
	{ &m0_ios_register,     &m0_ios_unregister,   "ioservice" },
	{ &m0_mds_register,     &m0_mds_unregister,   "mdservice"},
	{ &m0_pools_init,       &m0_pools_fini,       "pool" },
	{ &m0_cm_module_init,   &m0_cm_module_fini,   "copy machine" },
	{ &m0_sns_init,         &m0_sns_fini,         "sns" },
	{ &m0_conf_addb_init,   &m0_conf_addb_fini,   "conf-addb" },
	{ &m0_mdstore_mod_init, &m0_mdstore_mod_fini, "mdstore" },
	{ &m0_stats_svc_init,   &m0_stats_svc_fini,   "stats-service" },
	{ &m0_ss_svc_init,      &m0_ss_svc_fini,      "sss" },
#endif /* __KERNEL__ */
	{ &m0_parity_init,      &m0_parity_fini,      "parity_math" },
	{ &m0_dtm_global_init,  &m0_dtm_global_fini,  "dtm" }
};

struct init_fini_call once[] = {
	{ &m0_threads_once_init, &m0_threads_once_fini, "threads" }
};

static void fini_calls(struct init_fini_call *arr, int nr)
{
	while (--nr >= 0) {
		if (arr[nr].ifc_fini != NULL)
			arr[nr].ifc_fini();
	}
}

static void fini_nr(int i, int j)
{
	fini_calls(once, i);
	fini_calls(subsystem, j);
}

/**
 * Flag protecting initialisations to be done only once per process address
 * space (or kernel).
 */
static bool initialised_once = false;

int m0_init(struct m0 *instance)
{
	int i;
	int rc;

	if (!initialised_once) {
		/*
		 * Bravely ignore all issues of concurrency and memory
		 * consistency models, which occupy weaker minds.
		 */
		for (i = 0; i < ARRAY_SIZE(once); ++i) {
			rc = once[i].ifc_init();
			if (rc != 0) {
				m0_console_printf("subsystem %s "
						  "init failed: rc = %d\n",
						  subsystem[i].ifc_name, rc);
				fini_nr(i, 0);
				return rc;
			}
		}
		initialised_once = true;
	}

	rc = m0_threads_init(instance);
	if (rc != 0)
		return rc;

	m0_instance_init(instance);
	if (0 /*XXX ENABLEME*/)
		m0_module_init(&instance->i_self, M0_LEVEL_INIT);

	for (i = 0; i < ARRAY_SIZE(subsystem); ++i) {
		rc = subsystem[i].ifc_init();
		if (rc != 0) {
			m0_console_printf("subsystem %s init failed: rc = %d\n",
					  subsystem[i].ifc_name, rc);
			fini_nr(ARRAY_SIZE(once), i);
			break;
		}
	}
	return rc;
}

void m0_fini(void)
{
	fini_nr(ARRAY_SIZE(once), ARRAY_SIZE(subsystem));
	m0_threads_fini();
}

/** @} end of init group */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
