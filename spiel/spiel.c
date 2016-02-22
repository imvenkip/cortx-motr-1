/* -*- C -*- */
/*
 * COPYRIGHT 2015 XYRATEX TECHNOLOGY LIMITED
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
 * Original author: Mikhail Antropov <mikhail.v.antropov@seagate.com>
 * Original creation date: 11-Feb-2015
 */

#define M0_TRACE_SUBSYSTEM M0_TRACE_SUBSYS_SPIEL
#include "lib/trace.h"

#include "lib/tlist.h"
#include "lib/memory.h"
#include "lib/string.h"        /* m0_strdup, m0_strings_dup, m0_strings_free */
#include "lib/errno.h"
#include "lib/finject.h"       /* M0_FI_ENABLED */
#include "lib/locality.h"      /* m0_locality0_get */
#include "fid/fid.h"           /* m0_fid_sscanf */
#include "conf/schema.h"
#include "conf/cache.h"
#include "conf/obj.h"
#include "conf/obj_ops.h"
#include "conf/objs/common.h"

#include "reqh/reqh.h"
#include "spiel/spiel.h"

/**
 * @defgroup spiel-api-fspec-intr Spiel API Internals
 * @{
 */

int m0_spiel_init(struct m0_spiel *spiel, struct m0_reqh *reqh)
{
	M0_PRE(reqh != NULL);
	M0_ENTRY("spiel %p, reqh %p", spiel, reqh);

	M0_SET0(spiel);
	spiel->spl_rmachine = m0_reqh_rpc_mach_tlist_head(
				&reqh->rh_rpc_machines);
	if (spiel->spl_rmachine == NULL)
		return M0_ERR(-ENOENT);
	return M0_RC(0);
}
M0_EXPORTED(m0_spiel_init);

void m0_spiel_fini(struct m0_spiel *spiel)
{
	return;
}
M0_EXPORTED(m0_spiel_fini);

int m0_spiel_rconfc_start(struct m0_spiel    *spiel,
			  m0_rconfc_exp_cb_t  exp_cb)
{
	int               rc;
	struct m0_rconfc *rconfc = &spiel->spl_rconfc;

	M0_ENTRY();
	M0_PRE(spiel->spl_rmachine != NULL);

	rc = m0_rconfc_init(rconfc, m0_locality0_get()->lo_grp,
			    spiel->spl_rmachine, exp_cb);
	if (rc == 0) {
		rc = m0_rconfc_start_sync(rconfc);
		if (rc != 0) {
			m0_rconfc_stop_sync(rconfc);
			m0_rconfc_fini(rconfc);
		}
	}
	if (rc != 0) {
		return M0_ERR(rc);
	}
	return M0_RC(0);
}
M0_EXPORTED(m0_spiel_rconfc_start);

void m0_spiel_rconfc_stop(struct m0_spiel *spiel)
{
	M0_ENTRY();
	m0_rconfc_stop_sync(&spiel->spl_rconfc);
	m0_rconfc_fini(&spiel->spl_rconfc);
	M0_LEAVE();
}
M0_EXPORTED(m0_spiel_rconfc_stop);

int m0_spiel_cmd_profile_set(struct m0_spiel *spiel, const char *profile_str)
{
	M0_ENTRY("spiel = %p, profile = %s", spiel, profile_str);
	M0_PRE(spiel != NULL);
	if (profile_str == NULL)
		profile_str = "<0:0>";
	return M0_RC(m0_fid_sscanf(profile_str, &spiel->spl_profile));
}
M0_EXPORTED(m0_spiel_cmd_profile_set);

/** @} */
#undef M0_TRACE_SUBSYSTEM

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
