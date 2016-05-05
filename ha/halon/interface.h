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

#pragma once

#ifndef __MERO_HA_HALON_INTERFACE_H__
#define __MERO_HA_HALON_INTERFACE_H__

/**
 * @defgroup ha
 *
 * @{
 */

#include "lib/types.h"          /* bool */

struct m0_halon_interface_internal;

struct m0_halon_interface {
	struct m0_halon_interface_internal *hif_internal;
};

/**
 * Mero is ready to work after the call.
 *
 * This function compares given version against current library version.
 * It detects if the given version is compatible with the current version.
 *
 * @param hi                   this structure should be zeroed.
 * @param build_git_rev_id     @see m0_build_info::bi_git_rev_id
 * @param build_configure_opts @see m0_build_info::bi_configure_opts
 * @param disable_compat_check disables compatibility check entirely if set
 */
int m0_halon_interface_init(struct m0_halon_interface *hi,
                            const char                *build_git_rev_id,
                            const char                *build_configure_opts,
                            bool                       disable_compat_check);

/**
 * Finalises everything has been initialised during the init() call.
 * Mero functions shouldn't be used after this call.
 *
 * @note This function should be called from the exactly the same thread init()
 * has been called.
 *
 * @see m0_halon_interface_init()
 */
void m0_halon_interface_fini(struct m0_halon_interface *hi);

/** @} end of ha group */
#endif /* __MERO_HA_HALON_INTERFACE_H__ */

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
