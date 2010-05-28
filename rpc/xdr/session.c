/* -*- C -*- */
#ifdef HAVE_CONFIG_H
#  include <config.h>
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
#ifdef LINUX
	 if (!xdr_uint32_t(x, &objp->sca_high_slot_id))
		 return false;
	 if (!xdr_uint32_t(x, &objp->sca_max_rpc_size))
		 return false;
#elif DARWIN
	 if (!xdr_u_int32_t(x, &objp->sca_high_slot_id))
		 return false;
	 if (!xdr_u_int32_t(x, &objp->sca_max_rpc_size))
		 return false;
#else
#error "Not supported platform!"
#endif
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
#ifdef LINUX
	 if (!xdr_uint32_t(x, &objp->sco_high_slot_id))
		return false;
	 if (!xdr_uint32_t(x, &objp->sco_max_rpc_size))
		return false;
#elif DARWIN
	 if (!xdr_u_int32_t(x, &objp->sco_high_slot_id))
		return false;
	 if (!xdr_u_int32_t(x, &objp->sco_max_rpc_size))
		return false;
#else
#error "Not supported platform!"
#endif
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
