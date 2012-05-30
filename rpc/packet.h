#ifndef __COLIBRI_RPC_PACKET_H__
#define __COLIBRI_RPC_PACKET_H__

#include "lib/vec.h"
#include "lib/tlist.h"

struct c2_rpc_item;

struct c2_rpc_packet {
	uint64_t       rp_nr_items;
	c2_bcount_t    rp_size;
	struct c2_tl   rp_items;
	struct c2_chan rp_chan;
	int            rp_status;
};

void c2_rpc_packet_init(struct c2_rpc_packet *packet);
void c2_rpc_packet_fini(struct c2_rpc_packet *packet);

void c2_rpc_packet_add_item(struct c2_rpc_packet *packet,
			    struct c2_rpc_item   *item);
void c2_rpc_packet_remove_item(struct c2_rpc_packet *packet,
			       struct c2_rpc_item   *item);
void c2_rpc_packet_remove_all_items(struct c2_rpc_packet *packet);
bool c2_rpc_packet_is_empty(const struct c2_rpc_packet *packet);
bool c2_rpc_packet_is_carrying_item(const struct c2_rpc_packet *packet,
				    const struct c2_rpc_item   *item);
int c2_rpc_packet_encode_in_buf(const struct c2_rpc_packet *packet,
				struct c2_bufvec           *bufvec);
int c2_rpc_packet_encode_using_cursor(const struct c2_rpc_packet *packet,
				      struct c2_bufvec_cursor    *cursor);
int c2_rpc_packet_decode_from_buf(struct c2_rpc_packet   *packet,
				  const struct c2_bufvec *bufvec);
int c2_rpc_packet_decode_using_cursor(struct c2_rpc_packet    *packet,
				      struct c2_bufvec_cursor *cursor);

#endif
