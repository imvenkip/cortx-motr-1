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
 * Original author: Nikita Danilov <nikita.danilov@seagate.com>
 * Original creation date: 26-Feb-2016
 */

#pragma once

#ifndef __MERO_CAS_CAS_H__
#define __MERO_CAS_CAS_H__

#include "xcode/xcode_attr.h"
#include "fid/fid.h"
#include "fid/fid_xc.h"
#include "lib/types.h"
#include "lib/buf.h"
#include "lib/buf_xc.h"
#include "lib/cookie.h"
#include "lib/cookie_xc.h"

/**
 * @defgroup cas Catalogue service
 *
 * @{
 */

/*
 * CAS fop formats.
 */

/**
 * CAS cookie.
 *
 * This is a hint to a location of a record in an index.
 */
struct m0_cas_hint {
	/** Cookie of the leaf node. */
	struct m0_cookie ch_cookie;
	/** Position of the record within the node. */
	uint64_t         ch_index;
} M0_XCA_RECORD;

/**
 * Identifier of a CAS index.
 */
struct m0_cas_id {
	/** Fid. This is always present. */
	struct m0_fid      ci_fid;
	/** Cookie, when present is used instead of fid. */
	struct m0_cas_hint ci_hint;
} M0_XCA_RECORD;

/**
 * CAS index record.
 */
struct m0_cas_rec {
	/**
	 * Record key.
	 *
	 * Can be empty, when record key is implied, e.g., in replies.
	 */
	struct m0_buf      cr_key;
	/**
	 * Record value.
	 *
	 * Can be empty, when record value is not known (GET) or not needed
	 * (DEL).
	 */
	struct m0_buf      cr_val;
	/**
	 * Optional hint to speed up access.
	 */
	struct m0_cas_hint cr_hint;
	/**
	 * In replies, return code for the operation on this record. In CUR
	 * operation, the number of consecutive records to return.
	 */
	uint64_t           cr_rc;
} M0_XCA_RECORD;

/**
 * Vector of records.
 */
struct m0_cas_recv {
	uint64_t           cr_nr;
	struct m0_cas_rec *cr_rec;
} M0_XCA_SEQUENCE;

/**
 * CAS-GET, CAS-PUT, CAS-DEL and CAS-CUR fops.
 *
 * Performs an operation on a number of records.
 *
 * @see m0_fop_cas_rep
 */
struct m0_cas_op {
	/** Index to make operation in. */
	struct m0_cas_id   cg_id;
	/**
	 * Array of input records.
	 *
	 * For CAS-GET, CAS-DEL and CAS-CUR this describes input keys.
	 *
	 * For CAS-PUT this describes input keys and values.
	 */
	struct m0_cas_recv cg_rec;
} M0_XCA_RECORD;

/**
 * CAS-GET, CAS-PUT, CAS-DEL and CAS-CUR reply fops.
 *
 * @see m0_fop_cas_op
 */
struct m0_cas_rep {
	/** Status code of operation. */
	int32_t            cgr_rc;
	/**
	 * Array of operation results for each input record.
	 *
	 * For CAS-GET this describes output values.
	 *
	 * For CAS-PUT and, CAS-DEL this describes return codes.
	 *
	 * For CAS-CUR this describes next records.
	 */
	struct m0_cas_recv cgr_rep;
} M0_XCA_RECORD;

M0_EXTERN struct m0_reqh_service_type m0_cas_service_type;
M0_EXTERN struct m0_fid               m0_cas_meta_fid;
M0_EXTERN struct m0_fid_type          m0_cas_index_fid_type;

M0_EXTERN struct m0_fop_type cas_get_fopt;
M0_EXTERN struct m0_fop_type cas_put_fopt;
M0_EXTERN struct m0_fop_type cas_del_fopt;
M0_EXTERN struct m0_fop_type cas_cur_fopt;
M0_EXTERN struct m0_fop_type cas_rep_fopt;


/** @} end of cas group */
#endif /* __MERO_CAS_CAS_H__ */

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
