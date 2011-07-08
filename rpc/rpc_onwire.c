/* -*- C -*- */

#include <errno.h>
#include "lib/bitstring.h"
#include <stdlib.h>
#include <rpc/xdr.h>
#include "lib/memory.h"
#include "lib/misc.h"
#include "fop/fop.h"
#include "fop/fop_format_def.h"
#include "fop/fop_format.h"
#include "net/usunrpc/usunrpc.h"
#include "rpc/rpccore.h"
#include "rpc/session_internal.h"
#include "net/net.h"
#include "net/bulk_mem.h"
#include "net/bulk_sunrpc.h"
#include "lib/vec.h"
#include "rpc/rpc_onwire.h"

/** Header information present in an RPC object */
struct c2_rpc_header {
	/** RPC version, currenly 1 */
	uint32_t rh_ver;
	/** No of items present in the RPC object */
	uint32_t item_count;
};

/** Header information per rpc item in an rpc object. The detailed
    description of the various fields is present in struct c2_rpc_item
    /rpc/rpccore.h */
struct c2_rpc_item_header {
	uint64_t			rih_length;
	uint64_t			rih_sender_id;
	uint64_t			rih_session_id;
	uint32_t			slot_id;
	struct c2_rpc_sender_uuid	rih_uuid;
	struct c2_verno			rih_verno;
	struct c2_verno			rih_last_persistent_ver_no;
	struct c2_verno			rih_last_seen_ver_no;
	uint64_t			rih_xid;
	uint64_t			rih_slot_gen;
};

/** Encode an on-wire RPC object into a net buffer  ( not zero copy )
    Buffers containing multiple vectors are supported
	@param buf - char buf to be copied into a net buffer
	@param nb  - The network buffer
	@retval    - 0 if success, errno if failure.
*/
static int netbuf_encode(char *buf, struct c2_net_buffer *nb)
{
        struct c2_bufvec_cursor cur;
        char                    *bp;
        c2_bcount_t             step;
	size_t			len;

	C2_PRE(nb != NULL);
	C2_PRE(buf != NULL);

	len = nb->nb_length;
	c2_bufvec_cursor_init(&cur, &nb->nb_buffer);
        bp = c2_bufvec_cursor_addr(&cur);
	if(bp == NULL)
		return -EFAULT;
        while (len > 0) {
		bp = c2_bufvec_cursor_addr(&cur);
		step = c2_bufvec_cursor_step(&cur);
		if (len > step) {
			memcpy(bp, buf, step);
			buf += step;
			len -= step;
			C2_ASSERT(!c2_bufvec_cursor_move(&cur, step));
			C2_ASSERT(cur.bc_vc.vc_offset == 0);
		} else {
			memcpy(bp, buf, len);
			len = 0;
		}
	}
	return 0;
}

/** Decode a multivectored netbuf into a char buffer which
    can be deserialized using XDR
	@param buf - The buffer where the data would be copied to
	@param nb  - The network buffer from where the data would be
	copied from.
	@retval    - 0 if success, errno if failure.
*/
static int netbuf_decode(char *buf, struct c2_net_buffer *nb)
{
        struct c2_bufvec_cursor cur;
        char            *bp;
	c2_bcount_t	step;

	C2_PRE(buf != NULL);
	C2_PRE(nb != NULL);

        size_t len = nb->nb_length;
        c2_bufvec_cursor_init(&cur, &nb->nb_buffer);
        while(len > 0) {
		bp = c2_bufvec_cursor_addr(&cur);
		if(bp == NULL)
			return -EFAULT;
		step = c2_bufvec_cursor_step(&cur);
		if( len > step) {
			memcpy(buf, bp, step);
			buf += step;
			len -= step;
			C2_ASSERT(!c2_bufvec_cursor_move(&cur, step));
			C2_ASSERT(cur.bc_vc.vc_offset == 0);
		} else {
		memcpy(buf, bp, len);
			len = 0;
		}
	}
	return 0;
}

/** Calculate the size of the buffer that would be needed to encode this
rpc object */
static size_t rpc_buf_size_get(struct c2_rpc *rpc_obj)
{
	size_t			size = 0;
	struct c2_rpc_item	*item;

	size = sizeof (struct c2_rpc_header);
	c2_list_for_each_entry(&rpc_obj->r_items, item,
			struct c2_rpc_item, ri_rpcobject_linkage) {
		size  = size + c2_rpc_item_default_size(item);
	}
	return size;
}

size_t c2_rpc_item_default_size(struct c2_rpc_item *item)
{
	size_t		len = 0;
	struct c2_fop	*fop;

	C2_PRE(item != NULL);

	fop = c2_rpc_item_to_fop(item);
	if(fop != NULL){
		if((fop->f_type->ft_ops) && (fop->f_type->ft_ops->fto_getsize)) {
			len = fop->f_type->ft_ops->fto_getsize(fop);
			} else
			      len = fop->f_type->ft_fmt->ftf_layout->fm_sizeof;
	
		len += sizeof(fop->f_type->ft_code);
		len += sizeof(struct c2_rpc_item_header);
	}
	return len;
}

/* XXX : Return correct RPC version. TBD */
static int32_t rpc_ver_get()
{
	return 1;
}

/** Serialize the rpc object header which consists of a rpc version no and
    count of rpc items present in the rpc object
    @param - xdrs a valid xdr stream;
    @param - rpc_obj rpc object to be serialized
    @retval - 1 on success, errno on failure.
*/
static int rpc_header_encode( XDR *xdrs,  struct c2_rpc *rpc_obj)
{
	uint32_t	len;
	uint32_t	ver;
	int		rc ;

	C2_PRE(xdrs != NULL);
	C2_PRE(rpc_obj != NULL);

	/*XXX: Currently returns only 1. TBD */
	ver = rpc_ver_get();
	rc = xdr_uint32_t(xdrs, &ver);
	if(rc == 0)
		return -EFAULT;
	len = c2_list_length(&rpc_obj->r_items);
	if(len == 0)
		return -EFAULT;
	rc = xdr_uint32_t(xdrs, &len);
	if(rc == 0)
		return -EFAULT;
	return 0;
}

/** Deserialize the rpc object header which consists of a rpc version no and
    count of rpc items present from the XDR stream for further processing.
    @param - xdrs a valid xdr stream;
    @param - item_count pointer to get no of items in an rpc object
    @param - ver pointer to deserialized value of rpc ver
    @retval - 1 on success, errno on failure.
*/
static int rpc_header_decode(XDR *xdrs, uint32_t *item_count, uint32_t *ver)
{

	C2_PRE(item_count != NULL);
	C2_PRE(xdrs != NULL);
	C2_PRE(ver != NULL);

	if((!xdr_uint32_t(xdrs, ver)) ||
	   (!xdr_uint32_t(xdrs, item_count)))
	  return -EFAULT;

	return 0;
}

/** Helper functions to serialize uuid and slot references in rpc item header
    see rpc/rpccore.h */

static int sender_uuid_encdec(XDR *xdrs, struct c2_rpc_sender_uuid *uuid)
{
	int rc = 1;

	if(xdrs->x_op == XDR_ENCODE) 
		printf("\nSender uuid (encode): %lu", uuid->su_uuid);
	 else 
		printf("\nSender uuid (decode): %lu", uuid->su_uuid);
	
	if(!xdr_uint64_t(xdrs, &uuid->su_uuid))
		return -EFAULT;

	return rc;
}

static int slot_ref_encdec(XDR *xdrs, struct c2_rpc_slot_ref *slot_ref)
{
	struct c2_rpc_slot_ref    *sref;
	int			  rc = 1;
	char			  *what;

	C2_PRE(slot_ref != NULL);


	sref = &slot_ref[0];
	if((!xdr_uint64_t(xdrs, &sref->sr_verno.vn_lsn))||
	(!xdr_uint64_t(xdrs, &sref->sr_verno.vn_vc)) ||
	(!xdr_uint64_t(xdrs, &sref->sr_last_persistent_verno.vn_lsn)) ||
	(!xdr_uint64_t(xdrs, &sref->sr_last_persistent_verno.vn_vc)) ||
	(!xdr_uint64_t(xdrs, &sref->sr_last_seen_verno.vn_lsn)) ||
	(!xdr_uint64_t(xdrs, &sref->sr_last_seen_verno.vn_vc)) ||
	(!xdr_uint32_t(xdrs, &sref->sr_slot_id)) ||
	(!xdr_uint64_t(xdrs, &sref->sr_xid)) ||
	(!xdr_uint64_t(xdrs, &sref->sr_slot_gen)))
		rc = -EFAULT;
	
	if(xdrs->x_op == XDR_ENCODE)
		what = "encode";
	else
		what = "decode";

	printf("\nVer No lsn (%s): %lu",what, sref->sr_verno.vn_lsn);
	printf("\nVer No vc (%s): %lu", what, sref->sr_verno.vn_vc);
	printf("\nLast persistent ver no lsn (%s): %lu", what, sref->sr_last_persistent_verno.vn_lsn);
	printf("\nLast persistent ver no vc (%s): %lu", what, sref->sr_last_persistent_verno.vn_vc);
	printf("\nLast seen ver no lsn (%s): %lu", what, sref->sr_last_seen_verno.vn_lsn);
	printf("\nLast seen ver no vc (%s) : %lu", what, sref->sr_last_seen_verno.vn_vc);
	printf("\nSlot Id (%s): %u", what, sref->sr_slot_id);
	printf("\nXid (%s): %lu", what, sref->sr_xid);
	printf("\nSlot gen (%s): %lu",what, sref->sr_slot_gen);
	return rc;
}

/** Generic encode/decode function for rpc item header
    @param xdrs - XDR stream for encoding/decoding
    @param item - This RPC item's header would be encoded/decoded
    into the XDR stream
    @retval 0 if success, errno on failure
*/
static int item_header_encdec(XDR *xdrs, struct c2_rpc_item *item)
{
	uint64_t		len;
	struct c2_fop		*fop;
	char			*what;

	C2_PRE(xdrs != NULL);
	C2_PRE(item != NULL);

	fop = c2_rpc_item_to_fop(item);
	C2_ASSERT(fop != NULL);
	len = fop->f_type->ft_fmt->ftf_layout->fm_sizeof;
	len = len + sizeof(struct c2_rpc_item_header);

	if((!xdr_uint64_t(xdrs, &len)) ||
	(!xdr_uint64_t(xdrs, &item->ri_slot_refs[0].sr_sender_id)) ||
	(!xdr_uint64_t(xdrs, &item->ri_slot_refs[0].sr_session_id)) ||
	(!sender_uuid_encdec(xdrs, &item->ri_slot_refs[0].sr_uuid)) ||
	(!slot_ref_encdec(xdrs, item->ri_slot_refs)))
		return -EFAULT;
	
	if(xdrs->x_op == XDR_ENCODE)
		what = "encode";
	else
		what = "decode";
	printf("\nSender id (%s) :  %lu", what,
			item->ri_slot_refs[0].sr_sender_id);
	printf("\nSession id (%s) :  %lu", what,
			item->ri_slot_refs[0].sr_session_id);
	return 0;
}

/**
   Helper function used by item_encode and item_decode
   for encoding and decoding an rpc item into/from an XDR
   stream
*/
static int item_encdec(XDR *xdrs, struct c2_rpc_item *item)
{
	int		rc;
	struct		c2_fop *fop;

	C2_PRE(item != NULL);
	C2_PRE(xdrs != NULL);

	fop = c2_rpc_item_to_fop(item);
	C2_ASSERT(fop != NULL);

	rc = item_header_encdec(xdrs, item);
	if(rc != 0)
		return rc;
	if(!c2_fop_uxdrproc(xdrs, fop))
		return -EFAULT;
	return 0;
}

/* XXX : Debug function. Added here for UT and testing.*/
static void item_verify(struct c2_rpc_item *item)
{
	struct c2_fop		*fop;
	struct c2_fop_type	*fopt;
	struct c2_fop_data	*fdata;
	int			i;
	size_t			len;
	unsigned char		*buf;

	fop = c2_rpc_item_to_fop(item);
	fopt = fop->f_type;
	len = fop->f_type->ft_fmt->ftf_layout->fm_sizeof;
	fdata = c2_fop_data(fop);
	buf = (unsigned char *)fdata;
	printf("\n");
	for (i = 0; i < len; ++i) {
		printf (" %x ", *buf);
		buf++;
	}
	printf("\n");
}

/* XXX : Debug function. Added here for UT and testing.*/
int c2_rpc_fop_default_encode(struct c2_rpc_item *item, XDR *xdrs)
{
	struct c2_fop		*fop;
	int			rc;
	struct c2_fop_type	*fopt;
	uint32_t		opcode;

	C2_PRE(item != NULL);
	C2_PRE(xdrs != NULL);

	fop = c2_rpc_item_to_fop(item);
	C2_ASSERT(fop != NULL);
	fopt = fop->f_type;
	opcode = (uint32_t)fopt->ft_code;
	
	rc = xdr_uint32_t(xdrs, &opcode);
	if(rc != 1)
		return -EFAULT;
	printf("\nOpcode (encode): %d", opcode);
	
	rc = item_encdec(xdrs, item);
	printf("\nEncoded FOP data : ");
	item_verify(item);

	return rc;
}

int c2_rpc_fop_default_decode(struct c2_rpc_item *item, XDR *xdrs)
{
	C2_PRE(item != NULL);
	C2_PRE(xdrs != NULL);

	return(item_encdec(xdrs, item));
}

int c2_rpc_encode(struct c2_rpc *rpc_obj, struct c2_net_buffer *nb )
{
	char			*buf;
	struct c2_rpc_item	*item;
	XDR			xdrs;
	size_t			len, offset=0, buf_size;
	int			rc, count=0;

	C2_PRE(rpc_obj != NULL);
	C2_PRE(nb != NULL);

	buf_size = rpc_buf_size_get(rpc_obj);
	/* Allocate a buffer and create an XDR stream */
	buf = c2_alloc(buf_size);
	if(buf == NULL)
		return -ENOMEM;
	xdrmem_create(&xdrs, buf, buf_size, XDR_ENCODE);
	printf("\n**********ENCODING STARTS************");
	/*Serialize RPC object header into the buffer */
	rc = rpc_header_encode(&xdrs,rpc_obj);
        len = sizeof(struct c2_rpc_header);
	if(rc != 0)
		goto end;

	/*Iterate through the RPC items list in the RPC object
           and for each object serialize item and the fop data*/
        c2_list_for_each_entry(&rpc_obj->r_items, item,
				struct c2_rpc_item, ri_rpcobject_linkage) {
		offset = len + c2_rpc_item_default_size(item);
		if(offset > buf_size) {
			rc = -EMSGSIZE;
			goto end;
		}
		len = offset;
		printf("\n\n----ENCODING ITEM NO:%d\n", ++count);
		rc = c2_rpc_fop_default_encode(item, &xdrs);
		if(rc != 0)
			goto end;
	}
	/* Copy the buffer into nb */
	nb->nb_length = buf_size;
	printf("\nEncoded data length : %d", (int)buf_size);
	rc = netbuf_encode( buf, nb);
	printf("\n===========ENCODING ENDS===========\n");
	if(rc != 0)
		goto end;
end :
	/* Free the buffer */
	c2_free(buf);
	return rc;
}

int c2_rpc_decode(struct c2_rpc *rpc_obj, struct c2_net_buffer *nb)
{
        XDR			xdrs;
	char			*buf;
	int			i,rc = 0;
	struct c2_rpc_item	*item;
	struct c2_fop		**fop_arr;
	struct c2_fop_type	*ftype;
	size_t			offset, len;
	uint32_t		item_count, opcode, ver;

	C2_PRE(nb != NULL);
	
	printf("\n**********DECODING STARTS************");
	len = nb->nb_length;
	printf("\nLength of decode buffer = %d", (int)len);
	C2_ASSERT(len != 0);

	/* Allocate a buffer and create an XDR stream */
        buf = c2_alloc(len);
	if(buf == NULL)
		return -ENOMEM;
	
	/* Decode/Copy the nb into the allocated buffer */
	rc = netbuf_decode(buf, nb);
        if ( rc != 0 )
		return rc;
	/* Init XDR stream, deserialize the header
	   and find the no of items present in the rpc object */
	xdrmem_create(&xdrs, buf, len, XDR_DECODE);

	rc = rpc_header_decode(&xdrs, &item_count, &ver);
	if(rc != 0)
		return -EFAULT;
	offset = sizeof(struct c2_rpc_header);
	/* Iterate through the each rpc item and for each rpc item
	   -deserialize the opcode and get corrosponding c2_fop_type.
            iterate through the fop_types_list to find it.
           -Allocate new fop based on the c2_fop_type found.
           -Deserialize all the requisite item header information into
            the rpc_item.
           -Finally deserialize the payload into the fop data.
         */
	C2_ALLOC_ARR(fop_arr, item_count);
	for(i = 0; i < item_count; ++i) {
		if(offset + sizeof(opcode) > len)
			return -EMSGSIZE;
		rc = xdr_uint32_t(&xdrs, &opcode);
		if(rc == 0)
			return -EFAULT;
		ftype = c2_fop_type_search(opcode);
		if(ftype == NULL) {
			return -ENOSYS;
		}
		*fop_arr = c2_fop_alloc(ftype, NULL);
		if(*fop_arr == NULL)
			return -ENOMEM;
		item = c2_fop_to_rpc_item(*fop_arr);
		if(item == NULL) {
			rc = -EFAULT;
			goto end_decode;
		}
		offset = offset + c2_rpc_item_default_size(item);
		if(offset > len)
			return -EMSGSIZE;
		printf("\n\n----DECODING ITEM NO : %d\n", i+1);
		printf("\nOpcode (decode): %d", opcode);
		rc = c2_rpc_fop_default_decode(item, &xdrs);
		if (rc != 0)
			goto end_decode;
		item->ri_src_ep = nb->nb_ep;
		printf("\nDecoded FOP data : ");
		item_verify(item);
		fop_arr++;
		c2_list_add(&rpc_obj->r_items, &item->ri_rpcobject_linkage);
	}
end_decode:
	if(rc != 0)
		c2_fop_free(*fop_arr);
	
	printf("\n===========DECODING ENDS===========\n");
	return rc;
}

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 79
 *  scroll-step: 1
 *  End:
 */
