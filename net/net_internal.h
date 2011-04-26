/* -*- C -*- */
#ifndef __COLIBRI_NET_NET_INTERNAL_H__
#define __COLIBRI_NET_NET_INTERNAL_H__

#include "net/net.h"

/*
  Private symbols used within the Network module.
*/

extern const struct c2_addb_loc c2_net_addb_loc;
extern const struct c2_addb_ctx_type c2_net_addb_ctx;
extern struct c2_addb_ctx c2_net_addb;
extern const struct c2_addb_ctx_type c2_net_dom_addb_ctx;
extern struct c2_mutex c2_net_mutex;

/*
  Internal version of c2_net_domain_init() that is protected by the
  c2_net_mutex.  Can be used by transports for derived domain situations.
*/
int c2_net__domain_init(struct c2_net_domain *dom, struct c2_net_xprt *xprt);

/*
  Internal version of c2_net_domain_init() that is protected by the
  c2_net_mutex.  Can be used by transports for derived domain situations.
*/
void c2_net__domain_fini(struct c2_net_domain *dom);

/*
  Validates the value of queue type.
 */
bool c2_net__qtype_is_valid(enum c2_net_queue_type qt);

/*
  Buffer checks for a registered buffer.
  Must be called within the domain or transfer machine mutex.
*/
bool c2_net__buffer_invariant(struct c2_net_buffer *buf);

#endif /* __COLIBRI_NET_NET_INTERNAL_H__ */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
