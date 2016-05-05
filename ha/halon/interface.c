/* -*- C -*- */
/*
 * COPYRIGHT 2016 XYRATEX TECHNOLOGY LIMITED
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
 * Original author: Maxim Medved <max.medved@seagate.com>
 * Original creation date: 5-May-2016
 */


/**
 * @addtogroup ha
 *
 * @{
 */

#define M0_TRACE_SUBSYSTEM M0_TRACE_SUBSYS_HA
#include "lib/trace.h"

#include "ha/halon/interface.h"

#include <stdlib.h>             /* calloc */

#include "lib/misc.h"           /* M0_IS0 */
#include "lib/memory.h"         /* M0_ALLOC_PTR */
#include "lib/bob.h"            /* M0_BOB_DEFINE */
#include "lib/errno.h"          /* ENOSYS */
#include "lib/string.h"         /* strcmp */
#include "module/instance.h"    /* m0 */

#include "mero/init.h"          /* m0_init */
#include "mero/magic.h"         /* M0_HALON_INTERFACE_MAGIC */
#include "mero/version.h"       /* m0_build_info_get */

struct m0_halon_interface_cfg {
	const char *hic_build_git_rev_id;
	const char *hic_build_configure_opts;
	bool        hic_disable_compat_check;
};

struct m0_halon_interface_internal {
	struct m0 hii_instance;
	uint64_t  hii_magix;
};

static const struct m0_bob_type halon_interface_bob_type = {
	.bt_name         = "halon interface",
	.bt_magix_offset = M0_MAGIX_OFFSET(struct m0_halon_interface_internal,
	                                   hii_magix),
	.bt_magix        = M0_HALON_INTERFACE_MAGIC,
};
M0_BOB_DEFINE(static, &halon_interface_bob_type, m0_halon_interface_internal);

static bool
halon_interface_is_compatible(struct m0_halon_interface *hi,
                              const char                *build_git_rev_id,
                              const char                *build_configure_opts,
                              bool                       disable_compat_check)
{
	const struct m0_build_info *bi = m0_build_info_get();

	M0_ENTRY("build_git_rev_id=%s build_configure_opts=%s "
	         "disable_compat_check=%d",
	         build_git_rev_id, build_configure_opts, disable_compat_check);
	if (disable_compat_check)
		return true;
	if (strcmp(bi->bi_git_rev_id, build_git_rev_id) != 0) {
		M0_LOG(M0_ERROR, "The loaded mero library (%s) "
		       "is not the expected one (%s)", bi->bi_git_rev_id,
		       build_git_rev_id);
		return false;
	}
	if (strcmp(bi->bi_configure_opts, build_configure_opts) != 0) {
		M0_LOG(M0_ERROR, "The configuration options of the loaded "
		       "mero library (%s) do not match the expected ones (%s)",
		       bi->bi_configure_opts, build_configure_opts);
		return false;
	}
	return true;
}

int m0_halon_interface_init(struct m0_halon_interface *hi,
                            const char                *build_git_rev_id,
                            const char                *build_configure_opts,
                            bool                       disable_compat_check)
{
	int rc;

	M0_PRE(M0_IS0(hi));

	M0_ENTRY("hi=%p", hi);

	if (!halon_interface_is_compatible(hi, build_git_rev_id,
	                                   build_configure_opts,
					   disable_compat_check))
		return M0_ERR(-EINVAL);

	/* M0_ALLOC_PTR() can't be used before m0_init() */
	hi->hif_internal = calloc(1, sizeof *hi->hif_internal);
	if (hi->hif_internal == NULL)
		return M0_ERR(-ENOMEM);
	m0_halon_interface_internal_bob_init(hi->hif_internal);
	rc = m0_init(&hi->hif_internal->hii_instance);
	if (rc != 0) {
		free(hi->hif_internal);
		return M0_ERR(rc);
	}
	return M0_RC(0);
}

void m0_halon_interface_fini(struct m0_halon_interface *hi)
{
	M0_ENTRY("hi=%p", hi);
	M0_ASSERT(m0_halon_interface_internal_bob_check(hi->hif_internal));
	m0_fini();
	m0_halon_interface_internal_bob_fini(hi->hif_internal);
	free(hi->hif_internal);
	M0_LEAVE();
}

#undef M0_TRACE_SUBSYSTEM

/** @} end of ha group */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
/*
 * vim: tabstop=8 shiftwidth=8 noexpandtab textwidth=80 nowrap
 */
