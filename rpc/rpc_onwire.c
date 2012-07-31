/* -*- C -*- */
/*
 * COPYRIGHT 2012 XYRATEX TECHNOLOGY LIMITED
 *
 * THIS DRAWING/DOCUMENT, ITS SPECIFICATIONS, AND THE DATA CONTAINED
 * HEREIN, ARE THE EXCLUSIVE PROPERTY OF XYRATEX TECHNOLOGY
 * LIMITED, ISSUED IN STRICT CONFIDENCE AND SHALL NOT, WITHOUT
 * THE PRIOR WRITTEN PERMISSION OF XYRATEX TECHNOLOGY LIMITED,
 * BE REPRODUCED, COPIED, OR DISCLOSED TO A THIRD PARTY, OR
 * USED FOR ANY PURPOSE WHATSOEVER, OR STORED IN A RETRIEVAL SYSTEM
 * EXCEPT AS ALLOWED BY THE TERMS OF XYRATEX LICENSES AND AGREEMENTS.
 *
 * YOU SHOULD HAVE RECEIVED A COPY OF XYRATEX'S LICENSE ALONG WITH
 * THIS RELEASE. IF NOT PLEASE CONTACT A XYRATEX REPRESENTATIVE
 * http://www.xyratex.com/contact
 *
 * Original author: Subhash Arya <subhash_arya@xyratex.com>
 * Original creation date: 06/25/2011
 */

#include "lib/errno.h"
#include "lib/memory.h"
#include "lib/arith.h"
#include "lib/trace.h"
#include "fop/fop.h"
#include "fop/fop_format.h"
#include "rpc/session_internal.h"
#include "rpc/rpc_onwire.h"
#include "xcode/bufvec_xcode.h"
#include "rpc/packet.h"                  /* c2_rpc */

/* XXX : Return correct RPC version. */
static uint32_t rpc_ver_get(void)
{
	return C2_RPC_VERSION_1;
}

/**
    Serialize the rpc object header into a network buffer. The header consists
    of a rpc version no and count of rpc items present in the rpc object
    @param cur Current cursor position in the buffer vector.
    @param rpc_obj Object to be serialized.
    @retval 0 (success).
    @retvak -errno (failure).
*/
static int rpc_header_encode(struct c2_bufvec_cursor *cur,
			     struct c2_rpc *rpc_obj)
{
	uint32_t	item_count;
	uint32_t	ver;
	int		rc ;

	C2_PRE(cur != NULL);
	C2_PRE(rpc_obj != NULL);

	ver = rpc_ver_get();
	rc = c2_bufvec_uint32(cur, &ver, C2_BUFVEC_ENCODE);
	if (rc != 0)
		return rc;
	item_count = c2_list_length(&rpc_obj->r_items);
	C2_ASSERT(item_count != 0);
	rc = c2_bufvec_uint32(cur, &item_count, C2_BUFVEC_ENCODE);
	return rc;
}

/**
    Deserialize the rpc object header from a network buffer. The header consists
    of a rpc version no and count of rpc items present.
    @param cur Current cursor position in the buffer.
    @param  item_count Pointer to no of items in an rpc object.
    @param ver Pointer to deserialized value of rpc ver.
    @retval 0 On success.
    @retval -errno On failure.
*/
static int rpc_header_decode(struct c2_bufvec_cursor *cur, uint32_t *item_count,
			    uint32_t *ver)
{
	int	rc;

	C2_PRE(item_count != NULL);
	C2_PRE(cur != NULL);
	C2_PRE(ver != NULL);

	rc = c2_bufvec_uint32(cur, ver, C2_BUFVEC_DECODE);
	if (rc != 0)
		return rc;
	rc =  c2_bufvec_uint32(cur, item_count, C2_BUFVEC_DECODE);
	return rc;
}

/** Helper functions to serialize uuid and slot references in rpc item header
    see rpc/rpc2.h */

static int sender_uuid_encdec(struct c2_bufvec_cursor *cur,
			      struct c2_rpc_sender_uuid *uuid,
			      enum c2_bufvec_what what)
{
	return c2_bufvec_uint64(cur, &uuid->su_uuid, what);
}

static int slot_ref_encdec(struct c2_bufvec_cursor *cur,
			   struct c2_rpc_slot_ref *slot_ref,
			   enum c2_bufvec_what what)
{
	struct c2_rpc_slot_ref    *sref;
	int			   rc;
	int			   slot_ref_cnt;
	int			   i;

	C2_PRE(slot_ref != NULL);
	C2_PRE(cur != NULL);

	/* Currently MAX slot references in sessions is 1. */
	slot_ref_cnt = 1;
	for (i = 0; i < slot_ref_cnt; ++i) {
		sref = &slot_ref[i];
		rc = c2_bufvec_uint64(cur, &sref->sr_verno.vn_lsn, what) ?:
		c2_bufvec_uint64(cur, &sref->sr_sender_id, what) ?:
		c2_bufvec_uint64(cur, &sref->sr_session_id, what) ?:
		c2_bufvec_uint64(cur, &sref->sr_verno.vn_vc, what) ?:
		sender_uuid_encdec(cur, &sref->sr_uuid, what) ?:
		c2_bufvec_uint64(cur, &sref->sr_last_persistent_verno.vn_lsn,
				 what) ?:
		c2_bufvec_uint64(cur,&sref->sr_last_persistent_verno.vn_vc,
				 what) ?:
		c2_bufvec_uint64(cur, &sref->sr_last_seen_verno.vn_lsn, what) ?:
		c2_bufvec_uint64(cur, &sref->sr_last_seen_verno.vn_vc, what) ?:
		c2_bufvec_uint32(cur, &sref->sr_slot_id, what) ?:
		c2_bufvec_uint64(cur, &sref->sr_xid, what) ?:
		c2_bufvec_uint64(cur, &sref->sr_slot_gen, what);
		if (rc != 0)
			return -EFAULT;
	}
	return rc;
}

/**
    Encodes/decodes the rpc item header into a bufvec
    @param cur Current bufvec cursor position
    @param item RPC item for which the header is to be encoded/decoded
    @param what Denotes type of operation (Encode or Decode)
    @retval 0 (success)
    @retval -errno  (failure)
*/
static int item_header_encdec(struct c2_bufvec_cursor *cur,
			     struct c2_rpc_item *item,
			     enum c2_bufvec_what what)
{
	uint64_t		 len;
	int			 rc;
	struct c2_rpc_item_type *item_type;

	C2_PRE(cur != NULL);
	C2_PRE(item != NULL);

	item_type = item->ri_type;
	if (what == C2_BUFVEC_ENCODE) {
		C2_ASSERT(item_type->rit_ops != NULL);
		C2_ASSERT(item_type->rit_ops->rito_item_size != NULL);
		len = item_type->rit_ops->rito_item_size(item);
	}
	rc = c2_bufvec_uint64(cur, &len, what) ?:
	slot_ref_encdec(cur, item->ri_slot_refs, what);
	return rc;
}

/**
   Helper function used by encode/decode ops of each item type (rito_encode,
   rito_decode) for decoding an rpc item into/from a bufvec
*/
int item_encdec(struct c2_bufvec_cursor *cur, struct c2_rpc_item *item,
		       enum c2_bufvec_what what)
{
	int		 rc;
	struct	c2_fop	*fop;

	C2_PRE(item != NULL);
	C2_PRE(cur != NULL);

	fop = c2_rpc_item_to_fop(item);
	C2_ASSERT(fop != NULL);

	rc = item_header_encdec(cur, item, what);
	if(rc != 0)
		return rc;
	rc = c2_xcode_bufvec_fop(cur, fop, what);
	return rc;
}


/**
  Checks if the supplied bufvec has buffers with sizes multiple of 8 bytes.
  @param buf bufvec for which we want to check the size alignment.
  @retval true if the size of the bufvec is multiple of align_val.
  @retval false if the size of the bufvec is not a multiple of align_val.
*/
static bool each_bufsize_is_8aligned(struct c2_bufvec *buf)
{
	int		i;

	C2_PRE(buf != NULL);
	/* Check if each buffer segment has sizes multiple of 8 bytes*/
	for (i = 0; i < buf->ov_vec.v_nr; ++i) {
		if (!C2_IS_8ALIGNED(buf->ov_vec.v_count[i]))
			return false;
	}
	return true;
}

int c2_rpc_encode(struct c2_rpc *rpc_obj, struct c2_net_buffer *nb )
{
	struct c2_rpc_item		*item;
	struct c2_bufvec_cursor		 cur;
	size_t				 len;
	size_t				 offset = 0;
	c2_bcount_t			 bufvec_size;
	int				 rc;
	struct c2_rpc_item_type		*item_type;
	void				*cur_addr;

	C2_PRE(rpc_obj != NULL);
	C2_PRE(nb != NULL);

	bufvec_size = c2_vec_count(&nb->nb_buffer.ov_vec);
	/*
	  XXX : Alignment Checks
	  Check if bufvecs are 8-byte aligned buffers with sizes multiple of
	  8 bytes.
	*/
	C2_ASSERT(C2_IS_8ALIGNED(bufvec_size));
	C2_ASSERT(each_bufsize_is_8aligned(&nb->nb_buffer));
	c2_bufvec_cursor_init(&cur, &nb->nb_buffer);
	cur_addr = c2_bufvec_cursor_addr(&cur);
	C2_ASSERT(C2_IS_8ALIGNED(cur_addr));

	/* Serialize RPC object header into the buffer */
	rc = rpc_header_encode(&cur, rpc_obj);
	if (rc != 0)
		goto end;

        len = RPC_ONWIRE_HEADER_SIZE;
	/* Iterate through the RPC items list in the RPC object
           and for each object serialize item and the payload */
        c2_list_for_each_entry(&rpc_obj->r_items, item,
				struct c2_rpc_item, ri_rpcobject_linkage) {
		item_type = item->ri_type;
		C2_ASSERT(item_type->rit_ops != NULL);
		C2_ASSERT(item_type->rit_ops->rito_encode != NULL);
		C2_ASSERT(item_type->rit_ops->rito_item_size != NULL);
		offset = len + item_type->rit_ops->rito_item_size(item);

		C2_ASSERT(offset <= bufvec_size);
		len = offset;
		/* Call the associated encode function for the that item type */
		rc = item_type->rit_ops->rito_encode(item_type, item, &cur);
		if (rc != 0)
			goto end;
	}
	C2_ASSERT(bufvec_size >= len);
	nb->nb_length = len;
end:
	return rc;
}

int c2_rpc_decode(struct c2_rpc *rpc_obj, struct c2_net_buffer *nb,
		  c2_bcount_t len, c2_bcount_t offset)
{
	int			  i;
	int			  rc;
	struct c2_rpc_item	 *item;
	struct c2_rpc_item_type  *item_type;
	size_t			  bufvec_size;
	uint32_t		  item_count;
	uint32_t		  opcode;
	uint32_t		  ver;
	struct c2_bufvec_cursor   cur;
	void			 *cur_addr;

	C2_PRE(nb != NULL);
	C2_PRE(rpc_obj != NULL);
	C2_PRE(len != 0);

	bufvec_size = c2_vec_count(&nb->nb_buffer.ov_vec);
	C2_ASSERT(len <= bufvec_size);
	/*
	  Check if bufvecs are 8-byte aligned buffers with sizes multiple
	  of 8 bytes.
	*/
	C2_ASSERT(C2_IS_8ALIGNED(bufvec_size));
	C2_ASSERT(each_bufsize_is_8aligned(&nb->nb_buffer));
	c2_bufvec_cursor_init(&cur, &nb->nb_buffer);
	if (offset > 0)
		c2_bufvec_cursor_move(&cur, offset);
        cur_addr = c2_bufvec_cursor_addr(&cur);
	C2_ASSERT(C2_IS_8ALIGNED(cur_addr));

	/* Decode the rpc object header and get the count of items & rpc ver */
	rc = rpc_header_decode(&cur, &item_count, &ver);
	if (rc != 0)
		return -EFAULT;
	offset = RPC_ONWIRE_HEADER_SIZE;
	C2_ASSERT(offset < len);
	/*
	   - Iterate through the each rpc item and for each rpc item.
	   - Deserialize the opcode and get corresponding item_type by
             iterating through the item_types_list to find it.
	   - Call the corresponding item decode function for that item type.
         */
	for (i = 0; i < item_count; ++i) {
		rc = c2_bufvec_uint32(&cur, &opcode, C2_BUFVEC_DECODE);
		if (rc != 0)
			return -EFAULT;
		item_type = c2_rpc_item_type_lookup(opcode);
		C2_ASSERT(item_type != NULL);
		C2_ASSERT(item_type->rit_ops != NULL);
		C2_ASSERT(item_type->rit_ops->rito_decode != NULL);
		C2_ASSERT(item_type->rit_ops->rito_item_size != NULL);
		rc = item_type->rit_ops->rito_decode(item_type, &item, &cur);
		if (rc != 0)
			return rc;
		offset += item_type->rit_ops->rito_item_size(item);
		if (offset > len)
			return -EMSGSIZE;
		c2_list_add(&rpc_obj->r_items, &item->ri_rpcobject_linkage);
	}
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
