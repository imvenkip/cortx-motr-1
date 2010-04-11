#include "lib/cdefs.h"
#include "net/net.h"

bool xdr_node_id (XDR *xdrs, node_id *objp)
{
	if (!xdr_vector (xdrs, (char *)objp->uuid, ARRAY_SIZE(objp->uuid),
		sizeof (char), (xdrproc_t) xdr_char))
		 return FALSE;
	return TRUE;
}
