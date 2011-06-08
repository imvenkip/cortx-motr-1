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
 * Original author: Nikita Danilov <nikita_danilov@xyratex.com>
 * Original creation date: 06/19/2010
 */

#include "lib/cdefs.h"

#include "lib/user_space/thread.h"
#include "stob/stob.h"
#include "net/net.h"
#include "net/bulk_emulation/sunrpc_xprt.h"
#include "net/bulk_emulation/mem_xprt.h"
#include "net/usunrpc/usunrpc.h"
/* #include "rpc/rpclib.h" */
#include "fop/fop.h"
#include "addb/addb.h"
#include "lib/ut.h"
#include "layout/layout.h"
#include "pool/pool.h"
#include "lib/trace.h"
#include "db/db.h"
#include "stob/linux.h"
#include "stob/ad.h"
#include "fol/fol.h"
#include "desim/sim.h"

#include "colibri/init.h"

extern int  c2_memory_init(void);
extern void c2_memory_fini(void);

/** @addtogroup init @{ */

struct init_fini_call {
	int  (*ifc_init)(void);
	void (*ifc_fini)(void);
	const char *ifc_name;
};

struct init_fini_call subsystem[] = {
	{ &c2_trace_init,    &c2_trace_fini,   "trace" },
	{ &c2_memory_init,   &c2_memory_fini,  "memory" },
	{ &c2_uts_init,      &c2_uts_fini,     "ut" },
	{ &c2_threads_init,  &c2_threads_fini, "thread" },
	{ &c2_addb_init,     &c2_addb_fini,    "addb" },
	{ &c2_db_init,       &c2_db_fini,      "db" },
/*	{ &c2_rpclib_init,   &c2_rpclib_fini,  "rpc" }, */
	{ &c2_layouts_init,  &c2_layouts_fini, "layout" },
	{ &c2_pools_init,    &c2_pools_fini,   "pool" },
	{ &c2_fops_init,     &c2_fops_fini,    "fop" },
	{ &c2_net_init,      &c2_net_fini,     "net" },
	{ &c2_mem_xprt_init, &c2_mem_xprt_fini, "bulk/mem" },
	{ &c2_sunrpc_fop_init, &c2_sunrpc_fop_fini, "bulk/sunrpc" },
	{ &usunrpc_init,     &usunrpc_fini,     "user/sunrpc"},
	{ &linux_stobs_init, &linux_stobs_fini, "linux-stob" },
	{ &ad_stobs_init,    &ad_stobs_fini,    "ad-stob" },
	{ &c2_fols_init,     &c2_fols_fini,     "fol" },
	{ &sim_global_init,  &sim_global_fini,  "desim" }
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
			fprintf(stderr,
				"Subsystem \"%s\" failed to initialize: %i.\n",
				subsystem[i].ifc_name, result);
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
