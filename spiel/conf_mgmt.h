
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
 * Original author: Mikhail Antropov <mikhail.v.antropov@seagate.com>
 * Original creation date: 13-Feb-2015
 */

#pragma once

#ifndef __MERO_SPIEL_FOPS_H__
#define __MERO_SPIEL_FOPS_H__


#include "lib/types.h"
#include "xcode/xcode_attr.h"
#include "lib/buf.h"
#include "lib/buf_xc.h"
#include "fid/fid_xc.h"         /* m0_fid_xc */
#include "lib/types_xc.h"       /* m0_uint128_xc */
#include "fop/fop.h"            /* m0_fop */
#include "rpc/bulk.h"           /* m0_rpc_bulk */
#include "net/net_otw_types.h"  /* m0_net_buf_desc_data */


struct m0_spiel_tx;

extern struct m0_fop_type m0_fop_spiel_load_fopt;
extern struct m0_fop_type m0_fop_spiel_load_rep_fopt;

/**
   @section bulkclientDFSspielfop Generic io fop.
 */

/**
 * This data structure is used to associate an Spiel fop with its
 * rpc bulk data. It abstracts the m0_net_buffer and net layer APIs.
 * Client side implementations use this structure to represent
 * conf fops and the associated rpc bulk structures.
 * @see m0_rpc_bulk().
 */
struct m0_spiel_load_command {
	/** Inline fop for a generic Conf Load fop. */
	struct m0_fop         slc_load_fop;
	/** Inline fop for a generic Conf Flip fop. */
	struct m0_fop         slc_flip_fop;
	/** Rpc bulk structure containing zero vector for spiel fop. */
	struct m0_rpc_bulk    slc_rbulk;
	/** Connect */
	struct m0_rpc_conn    slc_connect;
	/** Session */
	struct m0_rpc_session slc_session;
	/* Error status */
	int                   slc_status;
	/** Current Version on Confd */
	int                   slc_version;
};

/* __MERO_SPIEL_FOPS_H__ */
#endif

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
