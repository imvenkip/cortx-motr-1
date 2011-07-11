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
#include "rpc/session_int.h"
#include "net/net.h"
#include "net/bulk_mem.h"
#include "net/bulk_sunrpc.h"
#include "lib/vec.h"
#include "rpc/rpc_onwire.h"
#include "rpc/rpc_bufvec.h"

/** Encode an on-wire RPC object into a net buffer  ( not zero copy )
    Buffers containing multiple vectors are supported
	@param buf - char buf to be copied into a net buffer
	@param nb  - The network buffer
	@retval    - 0 if success, errno if failure.

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

 Decode a multivectored netbuf into a char buffer which
    can be deserialized using XDR
	@param buf - The buffer where the data would be copied to
	@param nb  - The network buffer from where the data would be
	copied from.
	@retval    - 0 if success, errno if failure.

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
*/
size_t c2_rpc_item_default_size(struct c2_rpc_item *item)
{
	size_t		len = 0;
	struct c2_fop	*fop;

	C2_PRE(item != NULL);

	fop = c2_rpc_item_to_fop(item);
	if(fop != NULL){
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
static int rpc_header_encode( struct c2_bufvec_cursor *cur,
			      struct c2_rpc *rpc_obj)
{
	uint32_t	len;
	uint32_t	ver;
	int		rc ;

	C2_PRE(cur != NULL);
	C2_PRE(rpc_obj != NULL);

	/*XXX: Currently returns only 1. TBD */
	ver = rpc_ver_get();
	rc = c2_bufvec_uint32(cur, &ver, BUFVEC_ENCODE);
	if(rc != 0)
		return -EFAULT;
	len = c2_list_length(&rpc_obj->r_items);
	if(len == 0)
		return -EFAULT;
	rc = c2_bufvec_uint32(cur, &len, BUFVEC_ENCODE);
	if(rc != 0)
		return -EFAULT;
	return 0;
}

/** Deserialize the rpc object header which consists of a rpc version no and
    count of rpc items present from the XDR stream for further processing.
    @param cur the bufvec cursor from which the header is to be decoded;
    @param  item_count pointer to get no of items in an rpc object
    @param ver pointer to deserialized value of rpc ver
    @retval 1 on success, errno on failure.
*/
static int rpc_header_decode(struct c2_bufvec_cursor *cur, uint32_t *item_count,			     uint32_t *ver)
{
	int 	rc;

	C2_PRE(item_count != NULL);
	C2_PRE(cur != NULL);
	C2_PRE(ver != NULL);

	rc = c2_bufvec_uint32(cur, ver, BUFVEC_DECODE);
	if(rc != 0)
		return rc;

	rc =  c2_bufvec_uint32(cur, item_count, BUFVEC_DECODE);
	  if(rc != 0)
		  return rc;

	return 0;
}

/** Helper functions to serialize uuid and slot references in rpc item header
    see rpc/rpccore.h */

static int sender_uuid_encdec(struct c2_bufvec_cursor *cur,
			      struct c2_rpc_sender_uuid *uuid,
			      enum bufvec_what what)
{
	int 	rc;

	rc = c2_bufvec_uint64(cur, &uuid->su_uuid, what);

	if(what == BUFVEC_ENCODE)
		printf("\nSender uuid (encode): %lu", uuid->su_uuid);
	 else
		printf("\nSender uuid (decode): %lu", uuid->su_uuid);

	return rc;
}

static int slot_ref_encdec(struct c2_bufvec_cursor *cur,
			  struct c2_rpc_slot_ref *slot_ref,
			  enum bufvec_what what)
{
	struct c2_rpc_slot_ref    *sref;
	int			  rc = 1;
	char			  *todo;

	C2_PRE(slot_ref != NULL);
	C2_PRE(cur != NULL);

	sref = &slot_ref[0];
	rc = c2_bufvec_uint64(cur, &sref->sr_verno.vn_lsn, what) ||
	c2_bufvec_uint64(cur, &sref->sr_verno.vn_vc, what) ||
	c2_bufvec_uint64(cur, &sref->sr_last_persistent_verno.vn_lsn, what) ||
	c2_bufvec_uint64(cur, &sref->sr_last_persistent_verno.vn_vc, what) ||
	c2_bufvec_uint64(cur, &sref->sr_last_seen_verno.vn_lsn, what) ||
	c2_bufvec_uint64(cur, &sref->sr_last_seen_verno.vn_vc, what) ||
	c2_bufvec_uint32(cur, &sref->sr_slot_id, what) ||
	c2_bufvec_uint64(cur, &sref->sr_xid, what) ||
	c2_bufvec_uint64(cur, &sref->sr_slot_gen, what);

	if(what == BUFVEC_ENCODE)
		todo = "encode";
	else
		todo = "decode";

	printf("\nVer No lsn (%s): %lu",todo, sref->sr_verno.vn_lsn);
	printf("\nVer No vc (%s): %lu", todo, sref->sr_verno.vn_vc);
	printf("\nLast persistent ver no lsn (%s): %lu", todo, sref->sr_last_persistent_verno.vn_lsn);
	printf("\nLast persistent ver no vc (%s): %lu", todo, sref->sr_last_persistent_verno.vn_vc);
	printf("\nLast seen ver no lsn (%s): %lu", todo, sref->sr_last_seen_verno.vn_lsn);
	printf("\nLast seen ver no vc (%s) : %lu", todo, sref->sr_last_seen_verno.vn_vc);
	printf("\nSlot Id (%s): %u", todo, sref->sr_slot_id);
	printf("\nXid (%s): %lu", todo, sref->sr_xid);
	printf("\nSlot gen (%s): %lu",todo, sref->sr_slot_gen);
	if(rc != 0)
	   rc = -EFAULT;

	return rc;
}

/** Generic encode/decode function for rpc item header
    @param cur bufvec cursor for encode decode
    @param item the item for which the header is to be encoded
    into the XDR stream
    @retval 0 if success, errno on failure
*/
static int item_header_encdec(struct c2_bufvec_cursor *cur,
 			      struct c2_rpc_item *item,
			      enum bufvec_what what)
{
	uint64_t		 len;
	char 			*todo;
	int			 rc;
	struct c2_rpc_item_type *item_type;

	C2_PRE(cur != NULL);
	C2_PRE(item != NULL);

	item_type = item->ri_type;
	C2_ASSERT(item_type->rit_ops != NULL);
	C2_ASSERT(item_type->rit_ops->rio_item_size != NULL);
	len = item_type->rit_ops->rio_item_size(item);
	len +=  sizeof (struct c2_rpc_item_header);

	if(what == BUFVEC_ENCODE)
		todo = "encode";
	else
		todo = "decode";
	printf("\nSender id (%s) :  %ld", todo, item->ri_sender_id);
	printf("\nSession id (%s) :  %ld", todo, item->ri_session_id);


	rc = c2_bufvec_uint64(cur, &len, what) ||
	c2_bufvec_uint64(cur, &item->ri_sender_id, what) ||
	c2_bufvec_uint64(cur, &item->ri_session_id, what) ||
	sender_uuid_encdec(cur, &item->ri_uuid, what) ||
	slot_ref_encdec(cur, item->ri_slot_refs, what);

	if(rc != 0)
		return -EFAULT;

	return rc;
}

/**
   Helper function used by item_encode and item_decode
   for encoding and decoding an rpc item into/from an XDR
   stream
*/
static int item_encdec(struct c2_bufvec_cursor *cur, struct c2_rpc_item *item,
		       enum bufvec_what what)
{
	int		rc;
	struct		c2_fop *fop;

	C2_PRE(item != NULL);
	C2_PRE(cur != NULL);

	fop = c2_rpc_item_to_fop(item);
	C2_ASSERT(fop != NULL);

	rc = item_header_encdec(cur, item, what);
	if(rc != 0)
		return rc;
	rc = c2_bufvec_fop(cur, fop, what);
	return rc;
}

int c2_rpc_fop_default_encode(struct c2_rpc_item_type *item_type,
			      struct c2_rpc_item *item,
			      struct c2_bufvec_cursor *cur)
{
	int			rc;
	uint32_t		opcode;

	C2_PRE(item != NULL);
	C2_PRE(cur != NULL);

	item_type = item->ri_type;
	opcode = item_type->rit_opcode;
	rc = c2_bufvec_uint32(cur, &opcode, BUFVEC_ENCODE);
	if(rc != 0)
		return -EFAULT;
	printf("\nOpcode (encode): %d", opcode);

	rc = item_encdec(cur, item, BUFVEC_ENCODE);
	return rc;
}

int c2_rpc_fop_default_decode(struct c2_rpc_item_type *item_type,
			      struct c2_rpc_item **item,
			      struct c2_bufvec_cursor *cur)
{
	struct c2_fop 		*fop;
	struct c2_fop_type	*ftype;
	C2_PRE(item != NULL);
	C2_PRE(cur != NULL);

	ftype = c2_item_type_to_fop_type(item_type);
	C2_ASSERT(ftype != NULL);

	fop = c2_fop_alloc(ftype, NULL);
	if (fop == NULL)
		return -ENOMEM;
	*item = c2_fop_to_rpc_item(fop);
	C2_ASSERT(*item != NULL);
	return(item_encdec(cur, *item, BUFVEC_DECODE));
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
	printf("\nDecoded FOP Data :\n");
	for (i = 0; i < len; ++i) {
		printf (" %x ", *buf);
		buf++;
	}
	printf("\n");
}

int c2_rpc_encode(struct c2_rpc *rpc_obj, struct c2_net_buffer *nb )
{
	struct c2_rpc_item		*item;
	struct c2_bufvec_cursor 	 cur;
	size_t				 len;
	size_t				 offset=0;
	c2_bcount_t			 bufvec_size;
	int				 rc;
	int				 count=0;
	struct c2_rpc_item_type		*item_type;

	C2_PRE(rpc_obj != NULL);
	C2_PRE(nb != NULL);

	bufvec_size = c2_vec_count(&nb->nb_buffer.ov_vec);
	printf("\nNetwork buf size : %lu", bufvec_size);
	c2_bufvec_cursor_init(&cur, &nb->nb_buffer);

	printf("\n**********ENCODING STARTS************");
	/*Serialize RPC object header into the buffer */
	rc = rpc_header_encode(&cur ,rpc_obj);
	if(rc != 0)
		goto end;

        len = sizeof(struct c2_rpc_header);
	/*Iterate through the RPC items list in the RPC object
           and for each object serialize item and the fop data*/
        c2_list_for_each_entry(&rpc_obj->r_items, item,
				struct c2_rpc_item, ri_rpcobject_linkage) {
		offset = len + c2_rpc_item_default_size(item);
		if(offset > bufvec_size) {
			rc = -EMSGSIZE;
			goto end;
		}
		len = offset;
		printf("\n\n----ENCODING ITEM NO:%d\n", ++count);
		item_type = item->ri_type;
		C2_ASSERT(item_type->rit_ops != NULL);
		C2_ASSERT(item_type->rit_ops->rito_encode != NULL);
		/* Call the associated encode function for the that item type */
		rc = item_type->rit_ops->rito_encode(item_type, item, &cur);
		if(rc != 0)
			goto end;
	}
	/* Copy the buffer into nb */
	nb->nb_length = len;
	printf("\nEncoded data length : %lu", len);
	printf("\n===========ENCODING ENDS===========\n");
	if(rc != 0)
		goto end;
end :
	/* Free the buffer */
	return rc;
}

int c2_rpc_decode(struct c2_rpc *rpc_obj, struct c2_net_buffer *nb)
{
	int			  i,rc = 0;
	struct c2_rpc_item	 *item;
	struct c2_rpc_item_type  *item_type;
	size_t			  offset;
	size_t			  len;
	size_t			  bufvec_size;
	uint32_t		  item_count, opcode, ver;
	struct c2_bufvec_cursor   cur;


	C2_PRE(nb != NULL);
	C2_PRE(rpc_obj != NULL);

	printf("\n**********DECODING STARTS************");
	len = nb->nb_length;
	printf("\nLength of decode buffer = %d", (int)len);
	C2_ASSERT(len != 0);

	bufvec_size = c2_vec_count(&nb->nb_buffer.ov_vec);
	printf("\nNetwork buf size(decode) : %lu", bufvec_size);
	/* XXX : Add check here for 8 byte aligned vectors and buffer size */
	C2_ASSERT(len <= bufvec_size);
	c2_bufvec_cursor_init(&cur, &nb->nb_buffer);

	rc = rpc_header_decode(&cur, &item_count, &ver);
	if (rc != 0)
		return -EFAULT;
	offset = sizeof (struct c2_rpc_header);
	/* Iterate through the each rpc item and for each rpc item
	   -deserialize the opcode and get corrosponding item_type by
            iteratingthrough the item_types_list to find it.
	   -Call the corrosponding item decode function for that item type.
         */
	for (i = 0; i < item_count; ++i) {
		if (offset + sizeof(opcode) > len)
			return -EMSGSIZE;
		rc = c2_bufvec_uint32(&cur, &opcode, BUFVEC_DECODE);
		if(rc != 0)
			return -EFAULT;
		printf("Finding item type for opcode %u", opcode);
		item_type = c2_rpc_item_type_lookup(opcode);
		C2_ASSERT(item_type != NULL);
		C2_ASSERT(item_type->rit_ops != NULL);
		C2_ASSERT(item_type->rit_ops->rito_decode != NULL);
		rc = item_type->rit_ops->rito_decode(item_type, &item, &cur);
		if (rc != 0)
			return rc;
		item_verify(item);
		c2_list_add(&rpc_obj->r_items, &item->ri_rpcobject_linkage);
	}
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
