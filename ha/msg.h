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
 * Original creation date: 26-Apr-2016
 */

#pragma once

#ifndef __MERO_HA_MSG_H__
#define __MERO_HA_MSG_H__

/**
 * @defgroup ha
 *
 * @{
 */

#include "lib/types.h"          /* UINT64_MAX */
#include "lib/time.h"           /* m0_time_t */

#include "fid/fid.h"            /* m0_fid */
#include "stob/ioq_error.h"     /* m0_stob_ioq_error */

/*
 * XXX next two are workarounds because *_xc.h file generator can't
 * handle dependencies, so we need to specify them manually in .h
 */
#include "stob/ioq_error_xc.h"  /* workaround */
#include "lib/types_xc.h"       /* m0_uint128_xc */

enum {
	M0_HA_MSG_TAG_UNKNOWN = 0,
	M0_HA_MSG_TAG_INVALID = UINT64_MAX,
};

enum m0_ha_msg_type {
	M0_HA_MSG_INVALID,
	M0_HA_MSG_STOB_IOQ,
	M0_HA_MSG_NR,
};

struct m0_ha_msg_data {
	uint64_t hed_type;           /**< m0_ha_msg_type */
	union {
		struct m0_stob_ioq_error hed_stob_ioq
					M0_XCA_TAG("M0_HA_MSG_STOB_IOQ");
	} u;
} M0_XCA_UNION;

struct m0_ha_msg {
/* public fields */
	struct m0_fid          hm_fid;            /**< conf object fid */
	struct m0_fid          hm_source_process; /**< source process fid */
	struct m0_fid          hm_source_service; /**< source service fid */
	m0_time_t              hm_time;           /**< event timestamp */
	struct m0_ha_msg_data  hm_data;           /**< user data */
/* private fields */
	/** unique across a pair of m0_ha_link tag */
	uint64_t               hm_tag;
	/** m0_ha_link_cfd::hlc_link_id of the target link */
	struct m0_uint128      hm_link_id;
	/**
	 * true:  msg is in m0_ha_link::hln_q_in,
	 * false: msg is in m0_ha_link::hln_q_out.
	 */
	bool                   hm_incoming;
} M0_XCA_RECORD;


/** Compares 2 m0_ha_msg. It's useful in tests. */
M0_INTERNAL bool m0_ha_msg_eq(const struct m0_ha_msg *msg1,
                              const struct m0_ha_msg *msg2);

/** @} end of ha group */
#endif /* __MERO_HA_MSG_H__ */

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
