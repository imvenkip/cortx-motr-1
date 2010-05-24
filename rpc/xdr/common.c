#include "lib/cdefs.h"

#include <rpc/types.h>
#include <rpc/xdr.h>

#include "net/xdr.h"
#include "rpc/rpc_types.h"
#include "rpc/xdr/common.h"

bool c2_xdr_session_id(void *x, struct c2_session_id *objp)
{
	/** xdr_u_int64_t for mac */
	return xdr_uint64_t(x, &objp->id);
}

