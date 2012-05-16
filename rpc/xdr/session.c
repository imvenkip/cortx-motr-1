/* -*- C -*- */
/*
 * COPYRIGHT 2011 XYRATEX TECHNOLOGY LIMITED
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
 * Original author: Alexey Lyashkov <Alexey_Lyashkov@xyratex.com>
 * Original creation date: 05/18/2010
 */


#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <lib/cdefs.h>

#include <rpc/types.h>
#include <rpc/xdr.h>

#include <net/net.h>
#include <net/xdr.h>
#include <rpc/rpc_types.h>
#include <rpc/session_types.h>
#include <rpc/xdr/common.h>
#include <rpc/xdr/session.h>


bool c2_xdr_session_create_arg(void *x, struct c2_session_create_arg *objp)
{
	 if (!c2_xdr_service_id(x, &objp->sca_client))
		 return false;
	 if (!c2_xdr_service_id(x, &objp->sca_server))
		 return false;
	 if (!xdr_uint32_t(x, &objp->sca_high_slot_id))
		 return false;
	 if (!xdr_uint32_t(x, &objp->sca_max_rpc_size))
		 return false;
	return true;
}

bool c2_xdr_session_create_ret(void *x, struct c2_session_create_ret *objp)
{
	 if (!xdr_int(x, &objp->error))
		return false;

	/* request failed */
	if (objp->error)
		return true;

	 if (!c2_xdr_session_id(x, &objp->sco_session_id))
		return false;
	 if (!xdr_uint32_t(x, &objp->sco_high_slot_id))
		return false;
	 if (!xdr_uint32_t(x, &objp->sco_max_rpc_size))
		return false;
	return true;
}

bool c2_xdr_session_destroy_arg(void *xdrs, struct c2_session_destroy_arg *objp)
{
	if (!c2_xdr_service_id(xdrs, &objp->da_service))
		return false;

	if (!c2_xdr_session_id(xdrs, &objp->da_session))
		return false;

	return true;
}

bool c2_xdr_session_destroy_ret(void *xdrs, struct c2_session_destroy_ret *objp)
{
	return xdr_int32_t(xdrs, &objp->sda_errno);
}
