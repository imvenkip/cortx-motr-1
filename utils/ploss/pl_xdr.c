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
 */
/*
 * Packet loss testing tool.
 *
 * This tool is used to investigate the behavior of transport layer to simulate
 * the packet loss, reorder, duplication over the network.
 *
 * Written by Jay <jinshan.xiong@clusterstor.com>
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include "pl.h"

bool_t
xdr_C2_PL_CONFIG_TYPE (XDR *xdrs, C2_PL_CONFIG_TYPE *objp)
{
	 if (!xdr_enum (xdrs, (enum_t *) objp))
		 return FALSE;
	return TRUE;
}

bool_t
xdr_c2_pl_config_type (XDR *xdrs, c2_pl_config_type *objp)
{
	 if (!xdr_C2_PL_CONFIG_TYPE (xdrs, objp))
		 return FALSE;
	return TRUE;
}

bool_t
xdr_c2_pl_config (XDR *xdrs, c2_pl_config *objp)
{
	 if (!xdr_c2_pl_config_type (xdrs, &objp->op))
		 return FALSE;
	 if (!xdr_uint32_t(xdrs, &objp->value))
		 return FALSE;
	return TRUE;
}

bool_t
xdr_c2_pl_config_reply (XDR *xdrs, c2_pl_config_reply *objp)
{
	 if (!xdr_int (xdrs, &objp->res))
		 return FALSE;
	switch (objp->res) {
	case 0:
		 if (!xdr_uint32_t(xdrs, &objp->c2_pl_config_reply_u.config_value))
			 return FALSE;
		break;
	default:
		break;
	}
	return TRUE;
}

bool_t
xdr_c2_pl_config_res (XDR *xdrs, c2_pl_config_res *objp)
{
	 if (!xdr_c2_pl_config_type (xdrs, &objp->op))
		 return FALSE;
	 if (!xdr_c2_pl_config_reply (xdrs, &objp->body))
		 return FALSE;
	return TRUE;
}

bool_t
xdr_c2_pl_ping (XDR *xdrs, c2_pl_ping *objp)
{
	 if (!xdr_uint32_t(xdrs, &objp->seqno))
		 return FALSE;
	return TRUE;
}

bool_t
xdr_c2_pl_ping_res (XDR *xdrs, c2_pl_ping_res *objp)
{
	 if (!xdr_uint32_t(xdrs, &objp->seqno))
		 return FALSE;
	 if (!xdr_uint32_t(xdrs, &objp->time))
		 return FALSE;
	return TRUE;
}
