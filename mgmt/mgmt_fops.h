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
 * Original author: Carl Braganza <carl_braganza@xyratex.com>
 * Original creation date: 5-Mar-2013
 */
#pragma once
#ifndef __MERO_MGMT_MGMT_FOPS_H__
#define __MERO_MGMT_MGMT_FOPS_H__

#include "addb/addb_wire.h"
#include "addb/addb_wire_xc.h"
#include "lib/buf.h"
#include "lib/buf_xc.h"
#include "lib/types.h"
#include "lib/types_xc.h"
#include "xcode/xcode_attr.h"

/**
   @defgroup mgmt_fops Management FOPs and serializable data structures
   @ingroup mgmt

   @{
 */

/**
   Serializable sequence of service uuids.
 */
struct m0_mgmt_service_uuid_seq {
	uint32_t          msus_nr;
	struct m0_uint128 msus_uuids;
} M0_XCA_SEQUENCE;

/**
   Serializable data structure representing the state of a service.
 */
struct m0_mgmt_service_state {
	/**
	   Service id.
	 */
	struct m0_uint128 mss_uuid;
	/**
	   State - see enum ::m0_reqh_service_state.
	 */
	uint32_t          mss_state;
} M0_XCA_RECORD;

/**
   Serializable sequence of struct m0_mgmt_service_state.
 */
struct m0_mgmt_service_state_seq {
	uint32_t                      msss_nr;
	struct m0_mgmt_service_state *msss_state;
} M0_XCA_SEQUENCE;

/**
   Response FOP with state of services.  Typically sent in response to service
   management requests.
 */
struct m0_fop_mgmt_service_state_res {
	/**
	   Request handler state. See enum m0_reqh_states.
	 */
	uint32_t                         msr_reqh_state;
	/**
	   Individual service states.
	 */
	struct m0_mgmt_service_state_seq msr_ss;
} M0_XCA_RECORD;

/**
   Request FOP to ask for the state of a service or of all services.
   The response is a ::m0_fop_mgmt_service_state_res FOP.
 */
struct m0_fop_mgmt_service_state_req {
	/**
	   ADDB context id exported by the requestor.
	 */
	struct m0_addb_uint64_seq       mssrq_addb_ctx_id;
	/**
	   The (type) names of the services whose state is to be returned.
	   Specify an empty sequence if state of all configured
	   services are to be returned.
	   @todo Support a non-empty sequence.
	 */
	struct m0_mgmt_service_uuid_seq mssrq_services;
} M0_XCA_RECORD;

/**
   Request to stop a service.
   The response is a ::m0_fop_mgmt_service_state_res FOP.
   @todo Not supported yet.
 */
struct m0_fop_mgmt_service_terminate_req {
	/**
	   ADDB context id exported by the requestor.
	 */
	struct m0_addb_uint64_seq       mstrq_addb_ctx_id;
	/**
	   The (type) names of the services to be terminated.
	   Specify an empty sequence if state of all configured
	   services are to be terminated.
	 */
	struct m0_mgmt_service_uuid_seq mstrq_services;
} M0_XCA_RECORD;

/**
   Request to start a service.
   The response is a ::m0_fop_mgmt_service_state_res FOP.
   @todo Not supported yet.
 */
struct m0_fop_mgmt_service_run_req {
	/**
	   ADDB context id exported by the requestor.
	 */
	struct m0_addb_uint64_seq       msrrq_addb_ctx_id;
	/**
	   The (type) names of the services to be started.
	   Specify an empty sequence if state of all configured
	   services are to be started.
	 */
	struct m0_mgmt_service_uuid_seq msrrq_services;
} M0_XCA_RECORD;

/** @} end mgmt group */

#endif /* __MERO_MGMT_MGMT_FOPS_H__ */
/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
