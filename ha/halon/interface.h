/* -*- C -*- */
/*
 * COPYRIGHT 2016 XYRATEX TECHNOLOGY LIMITED
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
 * Original author: Maxim Medved <max.medved@seagate.com>
 * Original creation date: 5-May-2016
 */

#pragma once

#ifndef __MERO_HA_HALON_INTERFACE_H__
#define __MERO_HA_HALON_INTERFACE_H__

/**
 * @defgroup ha
 *
 * - @ref halon-interface-highlights
 *   - @ref halon-interface-msg
 *   - @ref halon-interface-tag
 *   - @ref halon-interface-lifetime
 * - @ref halon-interface-spec
 * - @ref halon-interface-sm
 *   - @ref halon-interface-sm-interface
 * - @ref halon-interface-threads
 *
 * @section halon-interface-highlights Design Highlights
 *
 * - only one m0_halon_interface in a single process address space is supported
 *   at the moment;
 * - user of m0_halon_interface shouldn't care about rpc, reqh or other stuff
 *   that is not documented here. Threading constraints should be taken into
 *   account though.
 *
 * @section halon-interface-lspec Logical Specification
 *
 * @subsection halon-interface-tag Message Handling
 *
 * There are 2 kinds of messages: received and sent.
 * Each message is received or sent in context of m0_ha_link.
 *
 * When a message arrives the msg_received_cb() is executed. The user should
 * call m0_halon_interface_delivered() for each received message. The call means
 * that the message shoulnd't be sent again if the current process restarts.
 *
 * To send a message user calls m0_halon_interface_send(). For each message sent
 * in this way either msg_is_delivered_cb() or msg_is_not_delivered_cb() is
 * called. Exactly one of this functions is called exactly once for each message
 * sent.
 *
 * msg_is_delivered_cb() means that the message has been successfully delivered
 * to the destination and it is not going to be resent if the destination
 * restarts. msg_is_not_delivered_cb() is called if the message can't be
 * delivered. It may be delivered already but there is no confirmation that it
 * has been delivered.
 *
 * @subsection halon-interface-tag Message Tag
 *
 * Each message has a tag. The tag has uint64_t type and it's assigned
 * internally when user tries to send a message (m0_halon_interface_send(),
 * for example).  Tag value is unique for all the messages sent or received
 * over a single m0_ha_link. No other assumption about tag value should be used.
 *
 * Tag is used for message identification when struct m0_ha_msg is not available.
 *
 * @subsection halon-interface-lifetime Lifetime and Ownership
 *
 * - m0_halon_inteface shouldn't be used before m0_halon_interface_init() and
 *   shouldn't be used after m0_halon_interface_fini();
 * - m0_halon_interface_start()
 *   - local_rpc_endpoint is not used after the function returns;
 *   - callbacks can be executed at any point after the function is called but
 *     before m0_halon_interface_stop() returns;
 * - m0_halon_interface_entrypoint_reply()
 *   - all parameters (except hi) are not used after the function returns;
 * - m0_halon_interface_send()
 *   - m0_ha_msg is not used after the function returns;
 * - entrypoint_request_cb()
 *   - req is owned by the user until m0_halon_interface_entrypoint_reply() is
 *     called for the req. User shouldn't interpret req in any way. It should be
 *     used just as an opaque pointer;
 *   - remote_rpc_endpoint can be finalised at any moment after the callback
 *     returns;
 * - msg_received_cb()
 *   - msg is owned by the user only before the callback returns.
 * - TODO define m0_ha_link lifetime.
 *
 * @section halon-interface-sm State machines
 *
 * @subsection halon-interface-sm-interface m0_halon_interface
 *
 * @verbatim
 *
 *      UNINITIALISED
 *          |   ^
 *   init() |   | fini()
 *          v   |
 *       INITIALISED
 *          |    ^
 *  start() |    | stop()
 *          v    |
 *         WORKING
 *
 * @endverbatim
 *
 * @section halon-interface-threads Threading and Concurrency Model
 *
 * - thread which calls m0_halon_interface_init() is considered as main thread
 *   for m0 instance inside m0_halon_interface;
 * - m0_halon_interface_start(), m0_halon_interface_stop(),
 *   m0_halon_interface_fini() should be called from the exactly the same thread
 *   m0_halon_interface_init() is called (main thread);
 * - if m0_halon_interface is in WORKING state then Mero locality threads are
 *   created;
 * - each callback from m0_halon_interface_start() is executed in the locality
 *   thread. The callback function:
 *   - can be called from any locality thread;
 *   - shouldn't wait for network or disk I/O;
 *   - shoudln't wait for another message to come or for a message to be
 *     delivered;
 *   - can call m0_halon_interface_entrypoint_reply(),
 *     m0_halon_interface_send() or m0_halon_interface_delivered().
 * - entrypoint_request_cb() can be called from any locality thread at any time
 *   regardless of other entrypoint_request_cb() executing;
 * - msg_received_cb()
 *   - for the same m0_ha_link is called sequentially, one by one;
 *   - may be called from different threads for the same m0_ha_link;
 *   - if there are links L1 and L2, then callbacks for the messages received
 *     for the links are not synchronised in any way;
 * - m0_halon_interface_init(), m0_halon_interface_fini(),
 *   m0_halon_interface_start(), m0_halon_interface_stop() are blocking calls.
 *   After the function returns m0_halon_interface is already moved to the
 *   appropriate state;
 * - m0_halon_interface_entrypoint_reply(), m0_halon_interface_send(),
 *   m0_halon_interface_delivered() are non-blocking calls. They can be called
 *   from:
 *   - main thread;
 *   - locality thread;
 *   - any callback.
 *
 * @{
 */

#include "lib/types.h"          /* bool */

struct m0_halon_interface_internal;
struct m0_ha_entrypoint_req;
struct m0_ha_link;
struct m0_ha_msg;
struct m0_fid;

struct m0_halon_interface {
	struct m0_halon_interface_internal *hif_internal;
};

/**
 * Mero is ready to work after the call.
 *
 * This function compares given version against current library version.
 * It detects if the given version is compatible with the current version.
 *
 * @param hi                   this structure should be zeroed.
 * @param build_git_rev_id     @see m0_build_info::bi_git_rev_id
 * @param build_configure_opts @see m0_build_info::bi_configure_opts
 * @param disable_compat_check disables compatibility check entirely if set
 */
int m0_halon_interface_init(struct m0_halon_interface *hi,
                            const char                *build_git_rev_id,
                            const char                *build_configure_opts,
                            bool                       disable_compat_check);

/**
 * Finalises everything has been initialised during the init() call.
 * Mero functions shouldn't be used after this call.
 *
 * @note This function should be called from the exactly the same thread init()
 * has been called.
 *
 * @see m0_halon_interface_init()
 */
void m0_halon_interface_fini(struct m0_halon_interface *hi);

/**
 * Starts everything needed to handle entrypoint requests and
 * m0_ha_msg send/recv.
 *
 * @param local_rpc_endpoint    the function creates local rpc machine with this
 *                              endpoint in the current process. All network
 *                              communications using this m0_halon_interface
 *                              uses this rpc machine.
 * @param entrypoint_request_cb this callback is executed when
 *                              entrypoint request arrives.
 * @param msg_received_cb       this callback is executed when
 *                              a new message arrives.
 *
 * - entrypoint_request_cb()
 *   - for each callback the m0_halon_interface_entrypoint_reply() should
 *     be called with the same req parameter;
 *   - remote_rpc_endpoint parameter contains rpc endpoint from which the
 *     entrypoint request has been received;
 * - msg_received_cb()
 *   - it's called when the message is received by the local rpc machine;
 *   - m0_halon_interface_delivered() should be called for each message
 *     received.
 * - msg_is_delivered_cb()
 *   - it's called when the message is delivered to the destination;
 * - msg_is_not_delivered_cb()
 *   - it's called when the message is not guaranteed to be delivered to the
 *     destination.
 */
int m0_halon_interface_start(struct m0_halon_interface *hi,
                             const char                *local_rpc_endpoint,
                             void                     (*entrypoint_request_cb)
				(struct m0_halon_interface         *hi,
				 const struct m0_ha_entrypoint_req *req,
				 const char             *remote_rpc_endpoint),
			     void                     (*msg_received_cb)
				(struct m0_halon_interface *hi,
				 struct m0_ha_link         *hl,
				 struct m0_ha_msg          *msg,
				 uint64_t                   tag),
			     void                     (*msg_is_delivered_cb)
				(struct m0_halon_interface *hi,
				 struct m0_ha_link         *hl,
				 uint64_t                   tag),
			     void                     (*msg_is_not_delivered_cb)
				(struct m0_halon_interface *hi,
				 struct m0_ha_link         *hl,
				 uint64_t                   tag));

/**
 * Stops sending/receiving messages and entrypoint requests.
 */
void m0_halon_interface_stop(struct m0_halon_interface *hi);

/**
 * Sends entrypoint reply.
 *
 * @param req the pointer received in the entrypoint_request_cb()
 * @param rc  return code for the entrypoint. It's delivered to the user
 * @param confd_fid_size size of confd_fid_data array
 * @param confd_fid_data array of confd fids
 * @param confd_eps_size size of confd_eps_data array (XXX Why this is needed?)
 * @param confd_eps_data array of confd endpoints
 * @param rm_fid         Active RM fid
 * @param rp_eps         Active RM endpoint
 * @param hl_ptr         m0_ha_link for communications with the other side is
 *                       returned here.
 *
 * - 'm0_ha_link *' is used in msg_received_cb(), m0_halon_interface_send(),
 *   m0_halon_interface_delivered();
 * - this function can be called from entrypoint_request_cb().
 */
void m0_halon_interface_entrypoint_reply(
                struct m0_halon_interface          *hi,
                const struct m0_ha_entrypoint_req  *req,
                int                                 rc,
                int                                 confd_fid_size,
                const struct m0_fid                *confd_fid_data,
                int                                 confd_eps_size,
                const char                        **confd_eps_data,
                const struct m0_fid                *rm_fid,
                const char                         *rm_eps,
                struct m0_ha_link                 **hl_ptr);

/**
 * Send m0_ha_msg using m0_ha_link.
 *
 * @param hl  m0_ha_link to send
 * @param msg msg to send
 * @param tag message tag is returned here.
 */
void m0_halon_interface_send(struct m0_halon_interface *hi,
                             struct m0_ha_link         *hl,
                             struct m0_ha_msg          *msg,
                             uint64_t                  *tag);

/**
 * Notifies remote side that the message is delivered. The remote side will not
 * resend the message if Halon crashes and then m0d reconnects again after this
 * call.
 *
 * - this function should be called for all messages received in
 *   msg_received_cb();
 * - this function can be called from the msg_received_cb() callback.
 */
void m0_halon_interface_delivered(struct m0_halon_interface *hi,
                                  struct m0_ha_link         *hl,
                                  struct m0_ha_msg          *msg);


/** @} end of ha group */
#endif /* __MERO_HA_HALON_INTERFACE_H__ */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
/*
 * vim: tabstop=8 shiftwidth=8 noexpandtab textwidth=80 nowrap
 */
