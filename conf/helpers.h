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

/* import */
struct m0_rpc_machine;
struct m0_sm_group;
struct m0_tl;
struct m0_confc;
struct m0_conf_filesystem;
struct m0_conf_pver;
struct m0_conf_obj;
struct m0_conf_root;
struct m0_conf_service;
struct m0_rpc_session;

M0_TL_DESCR_DECLARE(m0_conf_failure_sets, M0_EXTERN);
M0_TL_DECLARE(m0_conf_failure_sets, M0_EXTERN, struct m0_conf_obj);

struct m0_confc_args {
	/** Cofiguration profile. */
	const char            *ca_profile;
	/** Cofiguration string. */
	const char            *ca_confstr;
	/** Cofiguration server endpoint. */
	const char            *ca_confd;
	/** Configuration retrieval state machine. */
	struct m0_sm_group    *ca_group;
	/** Configuration retrieval rpc machine. */
	struct m0_rpc_machine *ca_rmach;
};

/**
 * Obtains filesystem object associated with given profile.
 *
 * @note m0_conf_fs_get() initialises @confc. It is user's responsibility
 *       to finalise it.
 */
M0_INTERNAL int m0_conf_fs_get(const char                 *profile,
			       struct m0_confc            *confc,
			       struct m0_conf_filesystem **result);

/** Finds pool version which does not intersect with the given failure set. */
M0_INTERNAL int m0_conf_poolversion_get(struct m0_conf_filesystem *fs,
					struct m0_tl *failure_set,
					struct m0_conf_pver **result);

/**
 * Load full configuration tree.
 */
M0_INTERNAL int m0_conf_full_load(struct m0_conf_filesystem *fs);

/**
 * Build failure set of resources by scanning ha state.
 */
M0_INTERNAL int
m0_conf_failure_sets_build(struct m0_rpc_session     *session,
			   struct m0_conf_filesystem *fs,
			   struct m0_tl              *failure_sets);

M0_INTERNAL void m0_conf_failure_sets_destroy(struct m0_tl *failure_sets);

/**
 * Open root configuration object.
 * @param confc already initialised confc instance
 * @param root  output parameter. Should be closed by user.
 */
M0_INTERNAL int m0_conf_root_open(struct m0_confc      *confc,
			          struct m0_conf_root **root);

/** Get service name by service configuration object */
M0_INTERNAL char *m0_conf_service_name_dup(const struct m0_conf_service *svc);
M0_INTERNAL bool m0_obj_is_pver(const struct m0_conf_obj *obj);

#endif /* __MERO_CONF_HELPERS_H__ */
