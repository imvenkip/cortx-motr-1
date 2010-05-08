#include "lib/cdefs.h"
#include <rpc/types.h>
#include <rpc/xdr.h>

#include "net/net_types.h"
#include "net/xdr.h"

bool c2_xdr_node_id (void *x, void *data)
{
	XDR *xdrs = x;
	struct c2_node_id *objp = data;

	if (!xdr_vector (xdrs, (char *)objp->uuid, ARRAY_SIZE(objp->uuid),
		sizeof (char), (xdrproc_t) xdr_char))
		 return false;
	return true;
}
