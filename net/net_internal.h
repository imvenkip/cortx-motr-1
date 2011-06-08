/* -*- C -*- */
/*
 * COPYRIGHT 2011 XYRATEX TECHNOLOGY LIMITED
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
 * Original author: Dave Cohrs, Carl Braganza
 * Original creation date: 04/04/2011
 */

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
  Validates the value of buffer queue type.
 */
bool c2_net__qtype_is_valid(enum c2_net_queue_type qt);

/*
  Validate transfer machine state
*/
bool c2_net__tm_state_is_valid(enum c2_net_tm_state ts);

/*
  TM event invariant
*/
bool c2_net__tm_event_invariant(const struct c2_net_tm_event *ev);

/*
  Validates the TM event type.
*/
bool c2_net__tm_ev_type_is_valid(enum c2_net_tm_ev_type et);

/*
  Buffer event invariant
*/
bool c2_net__buffer_event_invariant(const struct c2_net_buffer_event *ev);

/*
  Buffer checks for a registered buffer.
  Must be called within the domain or transfer machine mutex.
*/
bool c2_net__buffer_invariant(const struct c2_net_buffer *buf);

/*
  Invariant checks for an end point. No mutex necessary.
*/
bool c2_net__ep_invariant(struct c2_net_end_point *ep,
			  struct c2_net_domain    *dom,
			  bool                     under_dom_mutex);

/*
  Validates tm state.
  Must be called within the domain or transfer machine mutex.
*/
bool c2_net__tm_invariant(const struct c2_net_transfer_mc *tm);

/* this shouldn't really be here but it parallels the extern in net/net.h */
extern struct c2_net_xprt c2_net_usunrpc_minimal_xprt;

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
