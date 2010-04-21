#include "lib/cdefs.h"
#include <rpc/types.h>
#include <rpc/xdr.h>

#include "net/net.h"
#include "net/xdr.h"


bool_t c2_xdr_node_id (XDR *xdrs, struct node_id *objp)
{
	if (!xdr_vector (xdrs, (char *)objp->uuid, ARRAY_SIZE(objp->uuid),
		sizeof (char), (xdrproc_t) xdr_char))
		 return false;
	return true;
}
