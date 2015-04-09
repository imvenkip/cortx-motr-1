/* -*- c -*- */
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
 * Original author: Andriy Tkachuk <andriy_tkachuk@xyratex.com>
 *                  Mandar Sawant <mandar_sawant@seagate.com>
 * Original creation date: 05-Dec-2014
 */
#pragma once
#ifndef __MERO_CONF_HELPERS_H__
#define __MERO_CONF_HELPERS_H__

/**
 * Obtains filesystem object associated with given profile.
 *
 * @note m0_conf_fs_get() initialises @confc. It is user's responsibility
 *       to finalise it.
 */
M0_INTERNAL int m0_conf_fs_get(const char                 *profile,
			       const char                 *confd_addr,
			       struct m0_rpc_machine      *rmach,
			       struct m0_sm_group         *grp,
			       struct m0_confc            *confc,
			       struct m0_conf_filesystem **result);

/** Finds pool version which does not intersect with the given failure set. */
M0_INTERNAL int m0_conf_poolversion_get(struct m0_conf_filesystem *fs,
					struct m0_tl *failure_set,
					struct m0_conf_pver **result);

/**
 * Opens root configuration object.
 *
 * @param confc already initialised confc instance
 * @param root  output parameter. Should be closed by user.
 */
M0_INTERNAL int m0_conf_root_open(struct m0_confc      *confc,
			          struct m0_conf_root **root);

#endif /* __MERO_CONF_HELPERS_H__ */
