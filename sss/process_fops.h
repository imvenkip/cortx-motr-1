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
 * Original author: Mikhail Antropov <mikhail.v.antropov@xyratex.com>
 * Original creation date: 20-Mar-2015
 */

#pragma once

#ifndef __MERO_SSS_PROCESS_FOPS_H__
#define __MERO_SSS_PROCESS_FOPS_H__

#include "lib/types_xc.h"
#include "lib/buf_xc.h"
#include "fid/fid.h"
#include "fid/fid_xc.h"

/**
 * @defgroup process_fop Driver Process FOP
 * @{
 */

struct m0_ref;
struct m0_rpc_machine;
extern struct m0_fop_type m0_fop_process_fopt;
extern struct m0_fop_type m0_fop_process_rep_fopt;
extern struct m0_fop_type m0_fop_process_svc_list_rep_fopt;

/**
 * Process command ID list except Reconfig command.
 * Reconfig command is send separate FOP
 */
enum m0_ss_process_req_cmd {
	M0_PROCESS_STOP,
	M0_PROCESS_RECONFIG,
	M0_PROCESS_HEALTH,
	M0_PROCESS_QUIESCE,
	M0_PROCESS_RUNNING_LIST,
	M0_PROCESS_NR
};

/** Request to command a process. */
struct m0_ss_process_req {
	/**
	 * Command to execute.
	 * @see enum m0_ss_process_req_cmd
	 */
	uint32_t      ssp_cmd;
	/**
	 * Identifier of the process being started.
	 * fid type should set to M0_CONF_PROCESS_TYPE.cot_ftype
	 */
	struct m0_fid ssp_id;
} M0_XCA_RECORD;

/** Response to m0_ss_process_req. */
struct m0_ss_process_rep {
	/**
	 * Result of process operation or Process health
	 * (-Exxx = failure, 0 = success).
	 */
	int32_t sspr_rc;
} M0_XCA_RECORD;

struct m0_ss_process_svc_item {
	struct m0_fid ssps_fid;
	/* real size set from ss_svc_list_running_fill */
	char          ssps_name[1];
};

struct m0_ss_process_svc_list_rep {
	/**
	 * Result of service start/stop operation
	 * (-Exxx = failure, 0 = success)
	 */
	int32_t        sspr_rc;
	struct m0_bufs sspr_services;
} M0_XCA_RECORD;

M0_INTERNAL struct m0_fop *m0_ss_process_fop_create(struct m0_rpc_machine *mach,
						    uint32_t               cmd,
						    const struct m0_fid   *fid);

M0_INTERNAL bool m0_ss_fop_is_process_req(const struct m0_fop *fop);
M0_INTERNAL struct m0_ss_process_req *m0_ss_fop_process_req(struct m0_fop *fop);

M0_INTERNAL struct m0_ss_process_rep* m0_ss_fop_process_rep(struct m0_fop *fop);

M0_INTERNAL struct  m0_ss_process_svc_list_rep *
			m0_ss_fop_process_svc_list_rep(struct m0_fop *fop);

M0_INTERNAL int m0_ss_process_fops_init(void);
M0_INTERNAL void m0_ss_process_fops_fini(void);

M0_INTERNAL void m0_ss_process_stop_fop_release(struct m0_ref *ref);

/** @} ss_fop */
#endif /* __MERO_SSS_PROCESS_FOPS_H__ */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
