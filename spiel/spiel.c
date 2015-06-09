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
#include "fid/fid.h"           /* m0_fid */
#include "conf/schema.h"
#include "conf/cache.h"
#include "conf/obj.h"
#include "conf/obj_ops.h"
#include "conf/objs/common.h"
#include "conf/onwire.h"     /* arr_fid */

#include "reqh/reqh.h"
#include "spiel/spiel.h"

int m0_spiel_start(struct m0_spiel *spiel,
		   struct m0_reqh  *reqh,
		   const char     **confd_eps,
		   const char      *profile)
{
	int rc;

	M0_ENTRY();

	if (reqh == NULL || confd_eps == NULL || profile == NULL)
		return M0_ERR(-EINVAL);

	M0_SET0(spiel);

	spiel->spl_rmachine = m0_reqh_rpc_mach_tlist_head(
			&reqh->rh_rpc_machines);

	if (spiel->spl_rmachine == NULL)
		return M0_ERR(-ENOENT);

	spiel->spl_confd_eps = m0_strings_dup(confd_eps);

	if (spiel->spl_confd_eps == NULL)
		return M0_ERR(-ENOMEM);

	rc = m0_fid_sscanf(profile, &spiel->spl_profile) ?:
	     m0_confc_init(&spiel->spl_confc,
			   m0_locality0_get()->lo_grp,
			   spiel->spl_confd_eps[0],
			   spiel->spl_rmachine,
			   NULL);
	if (rc != 0)
		m0_strings_free(spiel->spl_confd_eps);

	return M0_RC(rc);
}
M0_EXPORTED(m0_spiel_start);

void m0_spiel_stop(struct m0_spiel *spiel)
{
	M0_ENTRY();

	m0_strings_free(spiel->spl_confd_eps);
	m0_confc_fini(&spiel->spl_confc);

	M0_LEAVE();
}
M0_EXPORTED(m0_spiel_stop);

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
