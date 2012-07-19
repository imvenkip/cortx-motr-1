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
 * Original author: Amit Jambure <amit_jambure@xyratex.com>
 * Original creation date: 05/25/2012
 */

#pragma once

#ifndef __COLIBRI_RPC_PACKET_INT_H__
#define __COLIBRI_RPC_PACKET_INT_H__

#include "lib/vec.h"
#include "lib/tlist.h"
#include "rpc/rpc_onwire.h"

/**
 * @addtogroup rpc
 * @{
 */

/* Imports */
struct c2_rpc_item;
struct c2_rpc_frm;

/**
   RPC Packet (aka RPC) is a collection of RPC items that are sent together
   in same network buffer.
 */
struct c2_rpc_packet {

	struct c2_rpc_packet_onwire_header rp_ow;

	/** Onwire size of this packet, including header */
	c2_bcount_t                        rp_size;

	/**
	   List of c2_rpc_item objects placed using ri_plink.
	   List descriptor: packet_item
	 */
	struct c2_tl                       rp_items;

	/**
	   Successfully sent (== 0) or was there any error while sending (!= 0)
	 */
	int                                rp_status;

	struct c2_rpc_frm                 *rp_frm;
};

c2_bcount_t c2_rpc_packet_onwire_header_size(void);

C2_TL_DESCR_DECLARE(packet_item, extern);
C2_TL_DECLARE(packet_item, extern, struct c2_rpc_item);

#define for_each_item_in_packet(item, packet) \
	c2_tl_for(packet_item, &packet->rp_items, item)

#define end_for_each_item_in_packet c2_tl_endfor

bool c2_rpc_packet_invariant(const struct c2_rpc_packet *packet);
void c2_rpc_packet_init(struct c2_rpc_packet *packet);
void c2_rpc_packet_fini(struct c2_rpc_packet *packet);

/**
   @pre  !packet_item_tlink_is_in(item)
   @post c2_rpc_packet_is_carrying_item(packet, item)
 */
void c2_rpc_packet_add_item(struct c2_rpc_packet *packet,
			    struct c2_rpc_item   *item);

/**
   @pre  c2_rpc_packet_is_carrying_item(packet, item)
   @post !packet_item_tlink_is_in(item)
 */
void c2_rpc_packet_remove_item(struct c2_rpc_packet *packet,
			       struct c2_rpc_item   *item);

/**
   @post c2_rpc_packet_is_empty(packet)
 */
void c2_rpc_packet_remove_all_items(struct c2_rpc_packet *packet);

bool c2_rpc_packet_is_empty(const struct c2_rpc_packet *packet);

/**
   Returns true iff item is included in packet.
 */
bool c2_rpc_packet_is_carrying_item(const struct c2_rpc_packet *packet,
				    const struct c2_rpc_item   *item);

/**
   Serialises packet in buffer pointed by bufvec.

   @pre !c2_rpc_packet_is_empty(packet)
 */
int c2_rpc_packet_encode(struct c2_rpc_packet *packet,
			 struct c2_bufvec     *bufvec);

/**
   Serialises packet in location pointed by cursor.

   @pre !c2_rpc_packet_is_empty(packet)
 */
int c2_rpc_packet_encode_using_cursor(struct c2_rpc_packet    *packet,
				      struct c2_bufvec_cursor *cursor);

/**
   Decodes packet from bufvec.
 */
int c2_rpc_packet_decode(struct c2_rpc_packet *packet,
			 struct c2_bufvec     *bufvec,
			 c2_bindex_t           off,
			 c2_bcount_t           len);

/**
   Decodes packet from location pointed by bufvec cursor.
 */
int c2_rpc_packet_decode_using_cursor(struct c2_rpc_packet    *packet,
				      struct c2_bufvec_cursor *cursor,
				      c2_bcount_t              len);

typedef void item_visit_fn(struct c2_rpc_item *item, unsigned long data);

/**
   Iterates through all the items in the packet p and calls visit function
   for each item. Passes opaque_data as it is to visit function.
 */
void c2_rpc_packet_traverse_items(struct c2_rpc_packet *p,
				  item_visit_fn        *visit,
				  unsigned long         opaque_data);

/** @} */

#endif /* __COLIBRI_RPC_PACKET_INT_H__ */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
