#ifndef __COLIBRI_RPC_PACKET_H__
#define __COLIBRI_RPC_PACKET_H__

#include "lib/vec.h"
#include "lib/tlist.h"

struct c2_rpc_item;
struct c2_rpc_frm;

enum {
	/** RPC version (4 bytes) + number of items in the packet (4 bytes) */
	C2_RPC_PACKET_OW_HEADER_SIZE = 16
};

/**
   RPC Packet (aka RPC) is a collection of RPC items that are sent together
   in same network buffer.
 */
struct c2_rpc_packet {
	/** Number of RPC items in packet */
	uint32_t           rp_nr_items;

	/** Onwire size of this packet, including header */
	c2_bcount_t        rp_size;

	/**
	   List of c2_rpc_item objects placed using ri_plink.
	   List descriptor: packet_item
	 */
	struct c2_tl       rp_items;

	/**
	   Successfully sent (== 0) or was there any error while sending (!= 0)
	 */
	int                rp_status;
	struct c2_rpc_frm *rp_frm;
};

bool c2_rpc_packet_invariant(const struct c2_rpc_packet *packet);
void c2_rpc_packet_init(struct c2_rpc_packet *packet);
void c2_rpc_packet_fini(struct c2_rpc_packet *packet);

/**
   @pre  !c2_rpc_packet_is_carrying_item(packet, item)
   @post c2_rpc_packet_is_carrying_item(packet, item)
 */
void c2_rpc_packet_add_item(struct c2_rpc_packet *packet,
			    struct c2_rpc_item   *item);

/**
   @pre  c2_rpc_packet_is_carrying_item(packet, item)
   @post !c2_rpc_packet_is_carrying_item(packet, item)
 */
void c2_rpc_packet_remove_item(struct c2_rpc_packet *packet,
			       struct c2_rpc_item   *item);

/**
   @pre !c2_rpc_packet_is_empty(packet)
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
int c2_rpc_packet_encode_in_buf(struct c2_rpc_packet *packet,
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
int c2_rpc_packet_decode_from_buf(struct c2_rpc_packet *packet,
				  struct c2_bufvec     *bufvec);


/**
   Decodes packet from location pointed by bufvec.
 */
int c2_rpc_packet_decode_using_cursor(struct c2_rpc_packet    *packet,
				      struct c2_bufvec_cursor *cursor);

typedef void item_visit_fn(struct c2_rpc_item *item, unsigned long data);

/**
   Iterates through all the items in the packet p and calls visit function
   for each item. Passes opaque_data as it is to visit function.
 */
void c2_rpc_packet_traverse_items(struct c2_rpc_packet *p,
				  item_visit_fn        *visit,
				  unsigned long         opaque_data);

#endif
