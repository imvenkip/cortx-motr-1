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

/*
  Domain version of xo_get_param().
 */
int c2_net_domain_get_param(struct c2_net_domain *dom, int param, ...);

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
