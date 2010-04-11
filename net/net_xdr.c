bool_t
xdr_client_id (XDR *xdrs, client_id *objp)
{
	register int32_t *buf;

	int i;
	 if (!xdr_vector (xdrs, (char *)objp->uuid, ARRAY_SIZE(objp->uuid),
		sizeof (char), (xdrproc_t) xdr_char))
		 return FALSE;
	return TRUE;
}
