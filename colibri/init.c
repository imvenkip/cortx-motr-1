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
 * Original author: Nikita Danilov <nikita_danilov@xyratex.com>
 * Original creation date: 06/19/2010
 */

#include "lib/cdefs.h"
#include "fop/fop.h"

#ifndef __KERNEL__
#   include "lib/user_space/thread.h"
#   include "desim/sim.h"
#endif

#include "stob/stob.h"
#include "net/net.h"
#include "net/bulk_emulation/mem_xprt.h"
#include "net/lnet/lnet.h"
#include "rpc/rpc.h"
#include "addb/addb.h"
#include "lib/finject.h"
#include "lib/ut.h"
#include "layout/layout.h"
#include "pool/pool.h"
#include "lib/processor.h"
#include "lib/trace.h"
#include "db/db.h"
#include "stob/linux.h"
#include "stob/ad.h"
#include "fol/fol.h"
#include "reqh/reqh.h"
#include "lib/timer.h"
#include "fid/fid.h"
#include "fop/fom_generic.h"
#include "colibri/init.h"
#include "lib/cookie.h"
#include "conf/conf_xcode.h" /* c2_confx_types_init, c2_confx_types_fini */
#include "conf/conf_fop.h"   /* c2_conf_fops_init, c2_conf_fops_fini */
#ifdef __KERNEL__
#  include "c2t1fs/linux_kernel/c2t1fs.h"
#  include "build_kernel_modules/dummy_init_fini.h"
#else
#  include "conf/confd.h"    /* c2_confd_register, c2_confd_unregister */
#endif
#include "cob/cob.h"
#include "ioservice/io_fops.h"
#include "ioservice/io_service.h"
#include "mdservice/md_fops.h"
#include "mdservice/md_service.h"
#include "sns/sns.h"
#include "cm/cm.h"

C2_INTERNAL int c2_memory_init(void);
C2_INTERNAL void c2_memory_fini(void);

C2_INTERNAL int libc2_init(void);
C2_INTERNAL void libc2_fini(void);

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
	{ &c2_trace_init,       &c2_trace_fini,       "trace" },
	{ &c2_fi_init,          &c2_fi_fini,          "finject" },
	{ &c2_memory_init,      &c2_memory_fini,      "memory" },
	{ &libc2_init,          &libc2_fini,          "libc2" },
	{ &c2_fid_init,         &c2_fid_fini,         "fid" },
	{ &c2_cookie_global_init, &c2_cookie_global_fini, "cookie" },
	{ &c2_uts_init,         &c2_uts_fini,         "ut" },
	{ &c2_processors_init,  &c2_processors_fini,  "processors" },
	{ &c2_threads_init,     &c2_threads_fini,     "thread" },
	{ &c2_timers_init,      &c2_timers_fini,      "timer" },
	{ &c2_addb_init,        &c2_addb_fini,        "addb" },
	{ &c2_db_init,          &c2_db_fini,          "db" },
	/* fol must be initialised before fops, because fop type registration
	   registers a fol record type. */
	{ &c2_fols_init,        &c2_fols_fini,        "fol" },
	{ &c2_layouts_init,     &c2_layouts_fini,     "layout" },
	{ &c2_pools_init,       &c2_pools_fini,       "pool" },
	/* fops must be initialised before network, because network build fop
	   type for network descriptors. */
	{ &c2_fops_init,        &c2_fops_fini,        "fop" },
	{ &c2_net_init,         &c2_net_fini,         "net" },
	{ &c2_rpc_init,         &c2_rpc_fini,         "rpc" },
	/* fom generic must be after rpc, because it initialises rpc item
	   type for generic error reply. */
	{ &c2_fom_generic_init, &c2_fom_generic_fini, "fom-generic" },
	{ &c2_mem_xprt_init,    &c2_mem_xprt_fini,    "bulk/mem" },
	{ &c2_net_lnet_init,    &c2_net_lnet_fini,    "net/lnet" },
	{ &c2_linux_stobs_init, &c2_linux_stobs_fini, "linux-stob" },
	{ &c2_ad_stobs_init,    &c2_ad_stobs_fini,    "ad-stob" },
	{ &sim_global_init,     &sim_global_fini,     "desim" },
	{ &c2_reqhs_init,       &c2_reqhs_fini,       "reqh" },
	{ &c2_confx_types_init, &c2_confx_types_fini, "conf-xtypes" },
	{ &c2_conf_fops_init,   &c2_conf_fops_fini,   "conf-fops" },
#ifdef __KERNEL__
	{ &c2t1fs_init,         &c2t1fs_fini,         "c2t1fs" },
#else
	{ &c2_confd_register,   &c2_confd_unregister, "confd" },
	{ &c2_ios_register,     &c2_ios_unregister,   "ioservice" },
	{ &c2_mds_register,     &c2_mds_unregister,   "mdservice"},
	{ &c2_cm_module_init,   &c2_cm_module_fini,   "copy machine" },
	{ &c2_sns_init,         &c2_sns_fini,         "sns" },
#endif
};

static void fini_nr(int i)
{
	while (--i >= 0) {
		if (subsystem[i].ifc_fini != NULL)
			subsystem[i].ifc_fini();
	}
}

int c2_init(void)
{
	int i;
	int result;

	for (result = i = 0; i < ARRAY_SIZE(subsystem); ++i) {
		result = subsystem[i].ifc_init();
		if (result != 0) {
			fini_nr(i);
			break;
		}
	}
	return result;
}

void c2_fini()
{
	fini_nr(ARRAY_SIZE(subsystem));
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
