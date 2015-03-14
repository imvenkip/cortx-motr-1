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
 * Original author: Mikhail Antropov <mikhail.v.antropov@seagate.com>
 * Original creation date: 2/11/2015
 */


#define M0_TRACE_SUBSYSTEM M0_TRACE_SUBSYS_SPIEL
#include "lib/trace.h"

#include "lib/memory.h"
#include "lib/string.h"        /* m0_strdup, m0_strings_dup */
#include "lib/errno.h"
#include "lib/finject.h"       /* M0_FI_ENABLED */
#include "lib/locality.h"      /* m0_locality0_get */
#include "fid/fid.h"           /* m0_fid */
#include "conf/schema.h"
#include "reqh/reqh.h"
#include "conf/objs/common.h"  /* strings_free */
#include "spiel/spiel.h"


int m0_spiel_start(struct m0_spiel *spiel,
		   struct m0_reqh  *reqh,
		   const char     **confd_eps,
		   const char      *profile)
{
	int           rc;

	M0_ENTRY();

	if (reqh == NULL || confd_eps == NULL || profile == NULL) {
		return M0_ERR(-EINVAL);
	}

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
		strings_free(spiel->spl_confd_eps);

	return M0_RC(rc);
}
M0_EXPORTED(m0_spiel_start);

void m0_spiel_stop(struct m0_spiel *spiel)
{
	M0_ENTRY();

	strings_free(spiel->spl_confd_eps);
	m0_confc_fini(&spiel->spl_confc);

	M0_LEAVE();
}
M0_EXPORTED(m0_spiel_stop);

struct m0_spiel_tx *m0_spiel_tx_open(struct m0_spiel    *spiel,
		                     struct m0_spiel_tx *tx)
{
	M0_ENTRY();

	M0_LEAVE();

	return tx;
}
M0_EXPORTED(m0_spiel_tx_open);

void m0_spiel_tx_cancel(struct m0_spiel_tx *tx)
{
	M0_ENTRY();
	M0_LEAVE();
}
M0_EXPORTED(m0_spiel_tx_cancel);

int m0_spiel_tx_done(struct m0_spiel_tx *tx)
{
	int rc = 0;
	M0_ENTRY();

	return M0_RC(rc);
}
M0_EXPORTED(m0_spiel_tx_done);

int m0_spiel_profile_add(struct m0_spiel_tx *tx, const struct m0_fid *fid)
{
	int rc = 0;
	M0_ENTRY();

	return M0_RC(rc);
}
M0_EXPORTED(m0_spiel_profile_add);

int m0_spiel_filesystem_add(struct m0_spiel_tx    *tx,
		            const struct m0_fid   *fid,
		            const struct m0_fid   *parent,
		            unsigned               redundancy,
		            const struct m0_fid   *rootfid,
		            const char           **fs_params)
{
	int rc = 0;
	M0_ENTRY();

	return M0_RC(rc);
}
M0_EXPORTED(m0_spiel_filesystem_add);

int m0_spiel_node_add(struct m0_spiel_tx  *tx,
		      const struct m0_fid *fid,
		      const struct m0_fid *parent,
		      uint32_t             memsize,
		      uint32_t             nr_cpu,
		      uint64_t             last_state,
		      uint64_t             flags,
		      struct m0_fid       *pool_fid)
{
	int rc = 0;
	M0_ENTRY();

	return M0_RC(rc);
}
M0_EXPORTED(m0_spiel_node_add);

int m0_spiel_process_add(struct m0_spiel_tx  *tx,
			 const struct m0_fid *fid,
			 const struct m0_fid *parent,
			 uint32_t             cores,
			 uint32_t             memlimit)
{
	int rc = 0;
	M0_ENTRY();

	return M0_RC(rc);
}
M0_EXPORTED(m0_spiel_process_add);

int m0_spiel_service_add(struct m0_spiel_tx                 *tx,
			 const struct m0_fid                *fid,
			 const struct m0_fid                *parent,
			 const struct m0_spiel_service_info *service_info)
{
	int rc = 0;
	M0_ENTRY();

	return M0_RC(rc);
}
M0_EXPORTED(m0_spiel_service_add);

int m0_spiel_device_add(struct m0_spiel_tx                        *tx,
		        const struct m0_fid                       *fid,
		        const struct m0_fid                       *parent,
		        enum m0_cfg_storage_device_interface_type  iface,
		        enum m0_cfg_storage_device_media_type      media,
		        uint32_t                                   bsize,
		        uint64_t                                   size,
		        uint64_t                                   last_state,
		        uint64_t                                   flags,
		        const char                                *filename)
{
	int rc = 0;
	M0_ENTRY();

	return M0_RC(rc);
}
M0_EXPORTED(m0_spiel_device_add);

int m0_spiel_pool_add(struct m0_spiel_tx  *tx,
		      const struct m0_fid *fid,
		      const struct m0_fid *parent,
		      uint32_t             order)
{
	int rc = 0;
	M0_ENTRY();

	return M0_RC(rc);
}
M0_EXPORTED(m0_spiel_pool_add);

int m0_spiel_rack_add(struct m0_spiel_tx  *tx,
		      const struct m0_fid *fid,
		      const struct m0_fid *parent)
{
	int rc = 0;
	M0_ENTRY();

	return M0_RC(rc);
}
M0_EXPORTED(m0_spiel_rack_add);

int m0_spiel_enclosure_add(struct m0_spiel_tx  *tx,
	        	   const struct m0_fid *fid,
			   const struct m0_fid *parent)
{
	int rc = 0;
	M0_ENTRY();

	return M0_RC(rc);
}
M0_EXPORTED(m0_spiel_enclosure_add);

int m0_spiel_controller_add(struct m0_spiel_tx  *tx,
			    const struct m0_fid *fid,
			    const struct m0_fid *parent,
			    const struct m0_fid *node)
{
	int rc = 0;
	M0_ENTRY();

	return M0_RC(rc);
}
M0_EXPORTED(m0_spiel_controller_add);

int m0_spiel_pool_version_add(struct m0_spiel_tx     *tx,
			      const struct m0_fid    *fid,
			      const struct m0_fid    *parent,
			      struct m0_pdclust_attr *attrs)
{
	int rc = 0;
	M0_ENTRY();

	return M0_RC(rc);
}
M0_EXPORTED(m0_spiel_pool_version_add);

int m0_spiel_rack_v_add(struct m0_spiel_tx *tx,
		        const struct m0_fid *fid,
		        const struct m0_fid *parent,
		        const struct m0_fid *real)
{
	int rc = 0;
	M0_ENTRY();

	return M0_RC(rc);
}
M0_EXPORTED(m0_spiel_rack_v_add);

int m0_spiel_enclosure_v_add(struct m0_spiel_tx  *tx,
			     const struct m0_fid *fid,
			     const struct m0_fid *parent,
			     const struct m0_fid *real)
{
	int rc = 0;
	M0_ENTRY();

	return M0_RC(rc);
}
M0_EXPORTED(m0_spiel_enclosure_v_add);

int m0_spiel_controller_v_add(struct m0_spiel_tx  *tx,
			      const struct m0_fid *fid,
			      const struct m0_fid *parent,
			      const struct m0_fid *real)
{
	int rc = 0;
	M0_ENTRY();

	return M0_RC(rc);
}
M0_EXPORTED(m0_spiel_controller_v_add);

int m0_spiel_pool_version_done(struct m0_spiel_tx  *tx,
			       const struct m0_fid *fid)
{
	int rc = 0;
	M0_ENTRY();

	return M0_RC(rc);
}
M0_EXPORTED(m0_spiel_pool_version_done);

int m0_spiel_element_del(struct m0_spiel_tx *tx, const struct m0_fid *fid)
{
	int rc = 0;
	M0_ENTRY();

	return M0_RC(rc);
}
M0_EXPORTED(m0_spiel_element_del);

int m0_spiel_service_init(struct m0_spiel *spl, const struct m0_fid *svc_fid)
{
	int rc = 0;
	M0_ENTRY();

	return M0_RC(rc);
}
M0_EXPORTED(m0_spiel_service_init);

int m0_spiel_service_start(struct m0_spiel *spl, const struct m0_fid *svc_fid)
{
	int rc = 0;
	M0_ENTRY();

	return M0_RC(rc);
}
M0_EXPORTED(m0_spiel_service_start);

int m0_spiel_service_stop(struct m0_spiel *spl, const struct m0_fid *svc_fid)
{
	int rc = 0;
	M0_ENTRY();

	return M0_RC(rc);
}
M0_EXPORTED(m0_spiel_service_stop);

int m0_spiel_service_health(struct m0_spiel *spl, const struct m0_fid *svc_fid)
{
	int rc = 0;
	M0_ENTRY();

	return M0_RC(rc);
}
M0_EXPORTED(m0_spiel_service_health);

int m0_spiel_service_quiesce(struct m0_spiel     *spl,
	       	             const struct m0_fid *svc_fid)
{
	int rc = 0;
	M0_ENTRY();

	return M0_RC(rc);
}
M0_EXPORTED(m0_spiel_service_quiesce);

int m0_spiel_device_attach(struct m0_spiel *spl, const struct m0_fid *dev_fid)
{
	int rc = 0;
	M0_ENTRY();

	return M0_RC(rc);
}
M0_EXPORTED(m0_spiel_device_attach);

int m0_spiel_device_detach(struct m0_spiel *spl, const struct m0_fid *dev_fid)
{
	int rc = 0;
	M0_ENTRY();

	return M0_RC(rc);
}
M0_EXPORTED(m0_spiel_device_detach);

int m0_spiel_device_format(struct m0_spiel *spl, const struct m0_fid *dev_fid)
{
	int rc = 0;
	M0_ENTRY();

	return M0_RC(rc);
}
M0_EXPORTED(m0_spiel_device_format);

int m0_spiel_process_stop(struct m0_spiel *spl, const struct m0_fid *proc_fid)
{
	int rc = 0;
	M0_ENTRY();

	return M0_RC(rc);
}
M0_EXPORTED(m0_spiel_process_stop);

int m0_spiel_process_reconfig(struct m0_spiel     *spl,
		              const struct m0_fid *proc_fid)
{
	int rc = 0;
	M0_ENTRY();

	return M0_RC(rc);
}
M0_EXPORTED(m0_spiel_process_reconfig);

int m0_spiel_process_health(struct m0_spiel     *spl,
		            const struct m0_fid *proc_fid)
{
	int rc = 0;
	M0_ENTRY();

	return M0_RC(rc);
}
M0_EXPORTED(m0_spiel_process_health);

int m0_spiel_process_quiesce(struct m0_spiel     *spl,
		             const struct m0_fid *proc_fid)
{
	int rc = 0;
	M0_ENTRY();

	return M0_RC(rc);
}
M0_EXPORTED(m0_spiel_process_quiesce);

int m0_spiel_process_list_services(struct m0_spiel      *spl,
		                   const struct m0_fid  *proc_fid,
		                   struct m0_fid        *services,
		                   int                   services_count)
{
	int rc = 0;
	M0_ENTRY();

	return M0_RC(rc);
}
M0_EXPORTED(m0_spiel_process_list_services);

int m0_spiel_pool_repair_start(struct m0_spiel     *spl,
		               const struct m0_fid *pool_fid)
{
	int rc = 0;
	M0_ENTRY();

	return M0_RC(rc);
}
M0_EXPORTED(m0_spiel_pool_repair_start);


int m0_spiel_pool_repair_quiesce(struct m0_spiel     *spl,
		                 const struct m0_fid *pool_fid)
{
	int rc = 0;
	M0_ENTRY();

	return M0_RC(rc);
}
M0_EXPORTED(m0_spiel_pool_repair_quiesce);

int m0_spiel_pool_rebalance_start(struct m0_spiel     *spl,
		                  const struct m0_fid *pool_fid)
{
	int rc = 0;
	M0_ENTRY();

	return M0_RC(rc);
}
M0_EXPORTED(m0_spiel_pool_rebalance_start);


int m0_spiel_pool_rebalance_quiesce(struct m0_spiel     *spl,
		                    const struct m0_fid *pool_fid)
{
	int rc = 0;
	M0_ENTRY();

	return M0_RC(rc);
}
M0_EXPORTED(m0_spiel_pool_rebalance_quiesce);

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
