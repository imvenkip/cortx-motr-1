#include <rpc/types.h>
#include <rpc/xdr.h>

#include "lib/cdefs.h"
#include "net/net.h"
#include "net/xdr.h"

bool c2_xdr_node_id(void *x, struct c2_service_id *objp)
{
	XDR *xdrs = x;

	return xdr_vector(xdrs, (char *)objp->si_uuid, 
			  ARRAY_SIZE(objp->si_uuid),
			  sizeof (char), (xdrproc_t) xdr_char);
}
