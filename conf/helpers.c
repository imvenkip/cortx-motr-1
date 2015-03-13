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

#define M0_TRACE_SUBSYSTEM M0_TRACE_SUBSYS_CONF
#include "lib/trace.h"

#include "lib/errno.h"     /* EINVAL */
#include "lib/string.h"    /* m0_strdup */
#include "conf/confc.h"
#include "conf/obj_ops.h"  /* m0_conf_dirval */
#include "conf/dir_iter.h" /* m0_conf_diter_init, m0_conf_diter_next_sync */

M0_INTERNAL int m0_conf_fs_get(const char *profile,
			       const char *confd_addr,
			       struct m0_rpc_machine *rmach,
			       struct m0_sm_group *grp,
			       struct m0_confc *confc,
			       struct m0_conf_filesystem **fs)
{
	struct m0_fid       prof_fid;
	struct m0_conf_obj *fs_obj = NULL;
	int                 rc;

	M0_PRE(rmach != NULL);

	if (confd_addr == NULL)
		return M0_ERR_INFO(-EINVAL, "confd address is unknown");

	rc = m0_fid_sscanf(profile, &prof_fid);
	if (rc != 0)
		return M0_ERR_INFO(rc, "Cannot parse profile `%s'", profile);

	m0_fid_tset(&prof_fid, M0_CONF_PROFILE_TYPE.cot_ftype.ft_id,
		    prof_fid.f_container, prof_fid.f_key);

	if (!m0_conf_fid_is_valid(&prof_fid))
		return M0_ERR_INFO(-EINVAL, "Wrong profile fid "FID_F,
				   FID_P(&prof_fid));

	rc = m0_confc_init(confc, grp, confd_addr, rmach, NULL);
	if (rc != 0)
		return M0_ERR_INFO(rc, "m0_confc_init() failed");

	rc = m0_confc_open_sync(&fs_obj, confc->cc_root,
				M0_CONF_ROOT_PROFILES_FID, prof_fid,
				M0_CONF_PROFILE_FILESYSTEM_FID);
	if (rc != 0) {
		M0_LOG(M0_FATAL, "m0_confc_open_sync() failed: rc=%d", rc);
		m0_confc_fini(confc);
		return M0_RC(rc);
	}
	*fs = M0_CONF_CAST(fs_obj, m0_conf_filesystem);

	return M0_RC(rc);
}

static bool _filter_pver(const struct m0_conf_obj *obj)
{
	return m0_conf_obj_type(obj) == &M0_CONF_PVER_TYPE;
}

/*
 * Simplistic implementation to make progress.
 * XXX TODO : Use failure set to find a valid pool version which does not
 * contain any device from the @failure_set.
 * This will be done as part of MERO-617
 */
M0_INTERNAL int m0_conf_poolversion_get(struct m0_conf_filesystem *fs,
					struct m0_tl *failure_set,
					struct m0_conf_pver **pver)
{
	struct m0_conf_diter  it;
	struct m0_conf_pver  *pv;
	struct m0_confc      *confc;
	struct m0_conf_obj   *obj;
	int                   rc;

	confc = m0_confc_from_obj(&fs->cf_obj);
	rc = m0_conf_diter_init(&it, confc, &fs->cf_obj,
				M0_CONF_FILESYSTEM_POOLS_FID,
				M0_CONF_POOL_PVERS_FID);
	if (rc != 0)
		return M0_RC(rc);

	while ((rc = m0_conf_diter_next_sync(&it, _filter_pver)) ==
							M0_CONF_DIRNEXT) {
		obj = m0_conf_diter_result(&it);
		M0_ASSERT(m0_conf_obj_type(obj) == &M0_CONF_PVER_TYPE);
		pv = M0_CONF_CAST(obj, m0_conf_pver);
		*pver = pv;
		rc = 0;
		break;
	}

	m0_conf_diter_fini(&it);
	if (*pver == NULL)
		rc = -ENOENT;

	return M0_RC(rc);
}

M0_INTERNAL int m0_conf_root_open(struct m0_confc      *confc,
				  struct m0_conf_root **root)
{
	struct m0_conf_obj *root_obj;
	int                 rc;

	M0_ENTRY();
	M0_PRE(confc->cc_root != NULL);

	rc = m0_confc_open_sync(&root_obj, confc->cc_root, M0_FID0);
	if (rc == 0)
		*root = M0_CONF_CAST(root_obj, m0_conf_root);
	return M0_RC(rc);
}

static const char *service_name[] = {
	[0]          = NULL,/* unused, enum declarations start from 1 */
	[M0_CST_MDS] = "mdservice",  /* Meta-data service. */
	[M0_CST_IOS] = "ioservice",  /* IO/data service. */
	[M0_CST_MGS] = "confd",      /* Management service (confd). */
	[M0_CST_RMS] = "rmservice",  /* RM service. */
	[M0_CST_STS] = "stats",      /* Stats service */
	[M0_CST_HA]  = "haservice",  /* HA service */
	[M0_CST_SSS] = "sss"         /* Start/stop service */
};

M0_INTERNAL char *m0_conf_service_name_dup(const struct m0_conf_service *svc)
{
	M0_PRE(IS_IN_ARRAY(svc->cs_type, service_name));
	return m0_strdup(service_name[svc->cs_type]);
}

#undef M0_TRACE_SUBSYSTEM
