/*
 * Packet loss testing tool.
 *
 * This tool is used to investigate the behavior of transport layer to simulate
 * the packet loss, reorder, duplication over the network.
 *
 * Written by Jay <jinshan.xiong@clusterstor.com>
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
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
#ifdef LINUX
	 if (!xdr_uint32_t(xdrs, &objp->value))
		 return FALSE;
#elif DARWIN
	 if (!xdr_u_int32_t(xdrs, &objp->value))
		 return FALSE;
#else
#error "Not supported platform!"
#endif
	return TRUE;
}

bool_t
xdr_c2_pl_config_reply (XDR *xdrs, c2_pl_config_reply *objp)
{
	 if (!xdr_int (xdrs, &objp->res))
		 return FALSE;
	switch (objp->res) {
	case 0:
#ifdef LINUX
		 if (!xdr_uint32_t(xdrs, &objp->c2_pl_config_reply_u.config_value))
			 return FALSE;
#elif DARWIN
		 if (!xdr_u_int32_t(xdrs, &objp->c2_pl_config_reply_u.config_value))
			 return FALSE;
#else
#error "Not supported platform!"
#endif
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
#ifdef LINUX
	 if (!xdr_uint32_t(xdrs, &objp->seqno))
		 return FALSE;
#elif DARWIN
	 if (!xdr_u_int32_t(xdrs, &objp->seqno))
		 return FALSE;
#else
#error "Not supported platform!"
#endif
	return TRUE;
}

bool_t
xdr_c2_pl_ping_res (XDR *xdrs, c2_pl_ping_res *objp)
{
#ifdef LINUX
	 if (!xdr_uint32_t(xdrs, &objp->seqno))
		 return FALSE;
	 if (!xdr_uint32_t(xdrs, &objp->time))
		 return FALSE;
#elif DARWIN
	 if (!xdr_u_int32_t(xdrs, &objp->seqno))
		 return FALSE;
	 if (!xdr_u_int32_t(xdrs, &objp->time))
		 return FALSE;
#else
#error "Not supported platform!"
#endif
	return TRUE;
}
