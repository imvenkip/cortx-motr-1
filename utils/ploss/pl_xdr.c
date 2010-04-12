/*
 * Packet loss testing tool.
 *
 * This tool is used to investigate the behavior of transport layer to simulate
 * the packet loss, reorder, duplication over the network.
 *
 * Written by Jay <jinshan.xiong@clusterstor.com>
 */

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
	 if (!xdr_u_long (xdrs, &objp->value))
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
		 if (!xdr_u_long (xdrs, &objp->c2_pl_config_reply_u.config_value))
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
	 if (!xdr_u_long (xdrs, &objp->seqno))
		 return FALSE;
	return TRUE;
}

bool_t
xdr_c2_pl_ping_res (XDR *xdrs, c2_pl_ping_res *objp)
{
	 if (!xdr_u_long (xdrs, &objp->seqno))
		 return FALSE;
	 if (!xdr_u_long (xdrs, &objp->time))
		 return FALSE;
	return TRUE;
}
