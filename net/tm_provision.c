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
 * Original author: Carl Braganza <Carl_Braganza@us.xyratex.com>
 * 		    Madhavrao Vemuri<madhav_vemuri@xyratex.com>
 * Original creation date: 12/21/2011
 */

/**
   @page NetRQProvDLD Auto-Provisioning of Receive Message Queue Buffers DLD

   - @ref NetRQProvDLD-ovw
   - @ref NetRQProvDLD-def
   - @ref NetRQProvDLD-req
   - @ref NetRQProvDLD-depends
   - @ref NetRQProvDLD-highlights
   - @ref NetRQProvDLD-fspec "Functional Specification"
   - @ref NetRQProvDLD-lspec
      - @ref NetRQProvDLD-lspec-state
      - @ref NetRQProvDLD-lspec-thread
      - @ref NetRQProvDLD-lspec-numa
   - @ref NetRQProvDLD-conformance
   - @ref NetRQProvDLD-ut
   - @ref NetRQProvDLD-st
   - @ref NetRQProvDLD-O
   - @ref NetRQProvDLD-ref

   <hr>
   @section NetRQProvDLD-ovw Overview
   This document describes the design of the auto-provisioning of network
   buffers to the receive message queue of a transfer machine feature.

   <hr>
   @section NetRQProvDLD-def Definitions
   Refer to <a href="https://docs.google.com/a/xyratex.com/document/d/1TZG__XViil3ATbWICojZydvKzFNbL7-JJdjBbXTLgP4/edit?hl=en_US">HLD of Colibri LNet Transport</a>,
   @ref and @ref net_buffer_pool.

   <hr>
   @section NetRQProvDLD-req Requirements
   - @b r.c2.net.xprt.support-for-auto-provisioned-receive-queue
     from <a href="https://docs.google.com/a/xyratex.com/document/d/1TZG__XViil3ATbWICojZydvKzFNbL7-JJdjBbXTLgP4/edit?hl=en_US">HLD of Colibri LNet Transport</a>.

   <hr>
   @section NetRQProvDLD-depends Dependencies
   - Provisioning support primarily depends on the @ref net_buffer_pool.  The
     c2_net_buffer_pool_get() subroutine must be modified to set the value of
     the c2_net_buffer::nb_pool field.

   - Locality support during provisioning depends on the application assigning
     a color to each transfer machine.  In user space Colibri servers this is
     done by the @ref colibri_setup "Colibri Setup" module.

   - The act of provisioning is similar to invoking c2_net_buffer_add() on the
     ::C2_NET_QT_MSG_RECV queue of the transfer machine.  Since the transfer
     machine mutex is usually held in most provisioning calls, an internal
     version of this subroutine which requires the mutex to be held should be
     made available if necessary.

   <hr>
   @section NetRQProvDLD-highlights Design Highlights
   - Automatic provisioning takes place from a network buffer pool specified by
   the c2_net_tm_pool_attach() subroutine.
   - Automatic provisioning takes place at the following times:
     - When the transfer machine is started.
     - When a buffer is dequeued from the receive queue.
     - When an exhausted pool is replenished (requires application support)
   - Locality support available if the transfer machine is assigned a color
   with the c2_net_tm_colour_set() subroutine.

   <hr>
   @section NetRQProvDLD-fspec Functional Specification
   The following new APIs are introduced:
   @code
int      c2_net_tm_pool_attach(struct c2_net_transfer_mc *tm,
                               struct c2_net_buffer_pool *bufpool,
                               const struct c2_net_buffer_callbacks *callbacks);
int      c2_net_tm_pool_length_set(struct c2_net_transfer_mc *tm, uint32_t len);
void     c2_net_domain_buffer_pool_not_empty(struct c2_net_buffer_pool *pool);
void     c2_net_tm_colour_set(struct c2_net_transfer_mc *tm, uint32_t colour);
uint32_t c2_net_tm_colour_get(struct c2_net_transfer_mc *tm);
   @endcode

   The transfer machine data structure is extended as follows:
   @code
struct c2_net_transfer_mc {
      ...
      struct c2_net_buffer_pool            *ntm_recv_pool;
      const struct c2_net_buffer_callbacks *ntm_recv_pool_callbacks;
      uint32_t                              ntm_recv_queue_min_length;
      struct c2_atomic64                    ntm_recv_queue_deficit;
      uint32_t                              ntm_pool_colour;
};
   @endcode

   The network buffer data structure is extended as follows:
   @code
struct c2_net_buffer {
      ...
      struct c2_net_buffer_pool            *nb_pool;
};
   @endcode

   The following enumeration is defined:
   @code
enum {
     C2_NET_TM_RECV_QUEUE_DEF_LEN = 2
};
   @endcode

   <hr>
   @section NetRQProvDLD-lspec Logical Specification

   @subsection NetRQProvDLD-lspec-enable Enabling Automatic Provisioning
   Automatic provisioning of network buffers to the receive queue takes place
   only when a buffer pool is "attached" to a transfer machine using the
   c2_net_tm_pool_attach() subroutine.  The subroutine can only be called prior
   to starting the transfer machine.

   The subroutine validates that the specified pool is in the same network
   domain. It then saves the pool pointer in the
   c2_net_transfer_mc::ntm_recv_pool field, and the callback pointer in the
   c2_net_transfer_mc::ntm_recv_pool_callbacks field.

   @subsection NetRQProvDLD-lspec-initial Initial Provisioning

   The first attempt to provision the transfer machine is on start up, just
   after the transfer machine state change event is delivered.  This ensures
   that there is no race condition between the state change event and the
   buffer completion callback notifying receipt first incoming unsolicited
   message.

   The receive message queue is provisioned with ::C2_NET_TM_RECV_QUEUE_DEF_LEN
   (nominally 2) network buffers if possible.

   @subsection NetRQProvDLD-lspec-normal Normal Provisioning

   Whenever a network buffer is dequeued from the receive message queue, an
   attempt to re-provision the queue is made prior to delivering the buffer
   completion event.  This ensures that the queue is replenished as soon as
   possible. Note that not every receive message queue buffer completion event
   will trigger re-provisioning if multiple message delivery is enabled in the
   buffer.

   When re-provisioning, as many buffers are fetched from the pool as needed to
   bring its length to the minimum desired value.  Changing the minimum receive
   queue length with the c2_net_tm_pool_length_set() subroutine always triggers
   an attempt to re-provision.  No attempt is ever made, however, to return
   buffers to the pool if the length of the queue is greater than the minimum.

   New network buffers are obtained by invoking the c2_net_buffer_pool_get()
   subroutine.  The buffer obtained from this subroutine is expected to have
   its c2_net_buffer::nb_pool variable set to the pool pointer, to enable the
   application to easily return it to the pool it came from, without having to
   explicitly track the pool.  This requires a modification to the
   c2_net_buffer_pool_get() subroutine.

   Actual provisioning is done by invoking c2_net_buffer_add() or its internal
   equivalent, depending on the locking model used.

   It is possible that the buffer pool gets exhausted and re-provisioning
   fails, partially or entirely.  In such cases, the transfer machine maintains
   a count of the number of additional buffers it requires in the
   c2_net_transfer_mc::ntm_recv_queue_deficit @b atomic variable.  This is to
   facilitate later re-provisioning without unnecessary locking and loss of
   locality.

   @subsection NetRQProvDLD-lspec-abnormal Provisioning After Pool Exhaustion

   Re-provisioning a transfer machine after pool exhaustion requires a
   triggering event:
   - Another receive queue buffer gets de-queued.
   - The application changes the minimum receive queue length with the
   c2_net_tm_pool_length_set() subroutine.
   - The application invokes the c2_net_domain_buffer_pool_not_empty()
     subroutine from the pool's @i not-empty callback.

   The first two cases result in the same behavior as normal provisioning.

   The c2_net_domain_buffer_pool_not_empty() subroutine initiates the
   replenishment of @i all depleted transfer machines in the network domain
   that are provisioned from the specified buffer pool.  The order in which
   each transfer machine gets processed is arbitrary, but this poses no
   particular problem because such a situation is assumed to be very rare and
   the system is already in deep trouble were it to happen.

   The following pseudo-code illustrates the subroutine algorithm:
   @code
c2_net_domain_buffer_pool_not_empty(pool) {
   C2_ASSERT(c2_net_buffer_pool_is_locked(pool));
   dom = pool->nbp_ndom;
   c2_mutex_lock(&dom->nd_mutex);
   foreach tm in dom->nd_tms list {
       if (c2_atomic64_get(&tm->ntm_recv_queue_deficit) == 0)
          continue; // skip if no deficit
       c2_mutex_lock(&tm->ntm_mutex);
       if (tm->ntm_state == C2_NET_TM_STARTED && tm->ntm_recv_pool == pool &&
           c2_atomic64_get(&tm->ntm_recv_queue_deficit) > 0) {
           // attempt to provision the TM
       }
       c2_mutex_unlock(&tm->ntm_mutex);
   }
   c2_mutex_unlock(&dom->nd_mutex);
}
   @endcode
   Note the following:
   - The pool is assumed to be locked when the subroutine is invoked.
   - The domain lock is held while the list of transfer machines is traversed.
   - Provisioning is attempted only if the transfer machine is in a depleted
     condition. This check is made with an atomic variable without the need to
     obtain the transfer machine mutex.  This reduces the amount of locking
     required, and preserves the spatial locality of transfer machine locks as
     long as possible.  In particular, it does not interfere with transfer
     machines that do not need provisioning.
   - The transfer machine lock is required to determine the transfer machine
     state, associated pool and re-affirm the need for additional network
     buffers.

   Note that the network layer has no control over the pool operations, so it
   is up to the application to supply a @i not-empty pool callback subroutine
   and make the call to the c2_net_domain_buffer_pool_not_empty() subroutine
   from there.

   @subsection NetRQProvDLD-lspec-state State Specification
   Automatic provisioning only takes place in an active transfer
   machine (state is ::C2_NET_TM_STARTED).

   Automatic provisioning of a transfer machine exists in two (informal) states:
   - @b Provisioned
   - @b Depleted
   .
   In the @b Provisioned state there are sufficient network buffers enqueued on
   the receive message queue.  The algorithms do not care whether these buffers
   were obtained from the buffer pool or not, just that the count is right.

   When there are insufficient network buffers in the receive message queue,
   the provisioning state is said to be @b Depleted.  The provisioning
   algorithms work to change the state back to @b Provisioned by obtaining
   additional buffers from the buffer pool.  It is expected that this usually
   gets done before the application can sense the transition out of the @b
   Provisioned state (prior to the buffer completion event callback), but there
   is a possibility that the pool gets exhausted before this is accomplished.

   A non-zero value in c2_net_transfer_mc::ntm_recv_queue_deficit indicates
   that automatic provisioning is in the @b Depleted state. Otherwise, it is in
   the @b Provisioned state.

   Every time the minimum required network buffer count is modified it is
   possible that the transfer machine's automatic provisioning state transitions
   to @b Depleted, so an attempt is made to re-provision to restore the state.


   @subsection NetRQProvDLD-lspec-thread Threading and Concurrency Model

   There are more reentrancy issues involved with automatic provisioning than
   concurrency issues, which in some sense is more complicated.

   Applications return receive message buffers to the buffer pool on their own
   accord, possibly, but not always, in the buffer completion callback itself.
   The transfer machine lock is @b not held by the application at this time;
   instead, the application has to obtain the pool lock to return the buffer.
   It is possible that this operation triggers a domain wide re-provisioning if
   the pool was exhausted.  The re-provisioning operation, as explained above,
   will obtain the domain lock and internal transfer machine locks, but assumes
   that the buffer pool lock is held.

   Normal provisioning usually takes place in the context of normal transfer
   machine operations, protected by the transfer machine mutex.  The
   provisioning steps necessarily require that the pool lock be obtained which
   clearly is exactly in the opposite order of the application triggered
   re-provisioning, hence <b>can result in a deadlock</b>.  Since application
   behavior cannot be dictated, normal provisioning must be made to use the
   same locking order as the re-provisioning case.  This requires that the
   transfer machine lock be released, the pool lock obtained, and then the
   transfer machine lock re-obtained.

   This is not a new situation; the transfer machine is already handling cases
   where it has to temporarily give up and re-obtain its own mutex.  To avoid
   getting destroyed while operating out of its mutex, the transfer machine
   uses the c2_net_transfer_mc::ntm_callback_counter to indicate that it is
   operating in such a mode.  When it re-obtains the mutex and decrements the
   counter, it signals on the c2_net_transfer_mc::ntm_chan channel.  This is
   illustrated in the following pseudo-code:
@code
   ...
   C2_PRE(c2_in_mutex(&tm->ntm_mutex);
   C2_PRE(tm->ntm_recv_pool != NULL);
   ++tm->ntm_callback_counter;
   c2_mutex_unlock(&tm->ntm_mutex);

   c2_net_buffer_pool_lock(tm->ntm_recv_pool);
   c2_mutex_lock(&tm->ntm_mutex);
   if (tm->ntm_state == C2_NET_TM_STARTED && ...) {
              // provision if needed
   }
   c2_mutex_unlock(&tm->ntm_mutex);
   c2_net_buffer_pool_unlock(tm->ntm_recv_pool)

   c2_mutex_lock(&tm->ntm_mutex);
   --tm->ntm_callback_counter;
   ...
@endcode

   The callback counter logic is already used currently to synchronize buffer
   completion events with the concurrent finalization of the transfer machine.
   The new addition is to obtain the pool and transfer machine locks if
   provisioning is needed; this can be done on a demand basis only for the most
   frequent re-provisioning case, so the overhead can be held to a minimum.

   Transfer machine finalization must be slightly tweaked to continue waiting
   on the counter and channel as long as the transfer machine state is active.


   @subsection NetRQProvDLD-lspec-numa NUMA optimizations

   The @ref net_buffer_pool module provides support for "colored" operations to
   maximize the locality of reference between a network buffer and a transfer
   machine.  All c2_net_buffer_pool_get() calls will use the
   c2_net_transfer_mc::ntm_pool_colour field value as the color.  This value is
   initialized to @c ::C2_NET_BUFFER_POOL_ANY_COLOR, and it is up to the higher
   level application to assign a color to the transfer machine with the
   c2_net_tm_colour_set() subroutine.  The higher level application is also
   responsible for creating the buffer pool with sufficient colors in the first
   place.

   Special care is taken during domain wide re-provisioning after the buffer
   pool recovers from an exhausted state, to not lose the locality of reference
   of the various transfer machine locks with respect to their CPUs.  An atomic
   variable is used to track if a transfer machine needs re-provisioning, and
   only if this is the case is the lock obtained.

   <hr>
   @section NetRQProvDLD-conformance Conformance

   - @b i.c2.net.xprt.support-for-auto-provisioned-receive-queue The design
     provides a means to automatically provision network buffers to the receive
     queues of transfer machines, using a domain wide buffer pool that offers
     the potential of sharing network buffers across multiple transfer
     machines, yet in a manner that maximizes the spatial locality of each
     buffer with the transfer machine that last used it.

   <hr>
   @section NetRQProvDLD-ut Unit Tests
   All tests are done with a fake transport and a real buffer pool.
   - Test that the API calls are properly made.
   - Test initial provisioning of a transfer machine.
   - Test normal provisioning after buffer completion.
   - Test that the atomic counter is properly incremented if the buffer pool is
     exhausted.
   - Test that provisioned buffer callbacks match those specified during
     pool attach.
   - Test that provisioned buffers identify the buffer pool in the
     c2_net_buffer::nb_pool field.
   - Test provisioning with multiple transfer machines, including domain wide
     re-provisioning when the pool is replenished.

   <hr>
   @section NetRQProvDLD-st System Tests
   No system testing is planned, though the multiple transfer machine unit
   tests have some system testing flavor.

   <hr>
   @section NetRQProvDLD-O Analysis

   - The buffer pool c2_net_buffer_pool_get() subroutine call is used to search
     for appropriate buffers and represents a potentially non-constant time
     algorithm.

   - Counting the minimum number of receive buffers using the list length
     counting subroutine is proportional to the number of elements in the list.
     Since the algorithm is intended to keep the number of elements in the list
     to the minimum value, the cost is proportional to the minimum list length
     in the average case.

   - The locking model requires that the the transfer machine lock be released
     and re-acquired in the average case.  While this cannot be avoided, the
     impact can be minimized by re-obtaining the lock only when provisioning is
     known to be required.

   <hr>
   @section NetRQProvDLD-ref References
   - <a href="https://docs.google.com/a/xyratex.com/document/d/1TZG__XViil3ATbWICojZydvKzFNbL7-JJdjBbXTLgP4/edit?hl=en_US">HLD of Colibri LNet Transport</a>
   - <a href="https://docs.google.com/a/xyratex.com/document/d/1tm_IfkSsW6zfOxQlPMHeZ5gjF1Xd0FAUHeGOaNpUcHA/view">RPC Bulk Transfer Task Plan</a>
   - @ref net_buffer_pool

 */

#include "net/net_internal.h"
#include "net/buffer_pool.h"

/* @todo These dummy api's needs to be removed once transport provides values
	 for nb_min_receive_size and nb_max_receive_msgs.
 */
int c2_xprt_buffer_min_recv_size(struct c2_net_domain *ndom)
{
	return 1<<12;
}

int c2_xprt_buffer_max_recv_msgs(struct c2_net_domain *ndom)
{
	return 1;
}

/*
   Private provisioning routine that assumes all locking is obtained
   in the correct order prior to invocation.
   @post If provisioning is enabled:
         Length of receive queue >= tm->ntm_recv_queue_min_length &&
                tm->ntm_recv_queue_deficit == 0 ||
         Length of receive queue + tm->ntm_recv_queue_deficit ==
                tm->ntm_recv_queue_min_length
*/
static void tm_provision_recv_q(struct c2_net_transfer_mc *tm)
{
	struct c2_net_buffer_pool *pool;
	uint64_t		   need;
	struct c2_net_buffer	  *nb;
	int			   rc;

	C2_PRE(c2_mutex_is_locked(&tm->ntm_mutex));
	C2_PRE(c2_net__tm_invariant(tm));
	pool = tm->ntm_recv_pool;
	if (tm->ntm_state != C2_NET_TM_STARTED || pool == NULL)
		return; /* provisioning not required */
	C2_PRE(c2_net_buffer_pool_is_locked(pool));
	need = c2_atomic64_get(&tm->ntm_recv_queue_deficit);
	while (need > 0) {
		nb = c2_net_buffer_pool_get(tm->ntm_recv_pool,
					    tm->ntm_pool_colour);
		if (nb != NULL) {
			nb->nb_qtype		= C2_NET_QT_MSG_RECV;
			nb->nb_callbacks	= tm->ntm_recv_pool_callbacks;
			nb->nb_min_receive_size =
				c2_xprt_buffer_min_recv_size(tm->ntm_dom);
			nb->nb_max_receive_msgs =
				c2_xprt_buffer_max_recv_msgs(tm->ntm_dom);

			C2_POST(nb->nb_pool == tm->ntm_recv_pool);
			rc = c2_net__buffer_add(nb, tm);
			if (rc != 0)
				break;
			C2_ASSERT(nb->nb_callbacks ==
				  tm->ntm_recv_pool_callbacks);
			--need;
		} else
			break;

	}
	c2_atomic64_set(&tm->ntm_recv_queue_deficit, need);
	return;
}

void c2_net_domain_buffer_pool_not_empty(struct c2_net_buffer_pool *pool)
{
	struct c2_net_domain	  *dom;
	struct c2_net_transfer_mc *tm;
	C2_ASSERT(c2_net_buffer_pool_is_locked(pool));
	dom = pool->nbp_ndom;
	c2_mutex_lock(&dom->nd_mutex);
	c2_list_for_each_entry(&dom->nd_tms, tm,
			struct c2_net_transfer_mc, ntm_dom_linkage) {
		if (c2_atomic64_get(&tm->ntm_recv_queue_deficit) == 0)
          		continue; /* skip if no deficit */
		c2_mutex_lock(&tm->ntm_mutex);
		if (tm->ntm_state     == C2_NET_TM_STARTED &&
		    tm->ntm_recv_pool == pool &&
		    c2_atomic64_get(&tm->ntm_recv_queue_deficit) > 0)
			tm_provision_recv_q(tm);
		c2_mutex_unlock(&tm->ntm_mutex);
	}
	c2_mutex_unlock(&dom->nd_mutex);
	return;
}
C2_EXPORTED(c2_net_domain_buffer_pool_not_empty);

void c2_net__tm_provision_recv_q(struct c2_net_transfer_mc *tm)
{
	C2_PRE(c2_mutex_is_not_locked(&tm->ntm_mutex));
	C2_PRE(tm->ntm_callback_counter > 0);
	C2_PRE(tm->ntm_recv_pool != NULL);
	C2_PRE(!c2_net_buffer_pool_is_locked(tm->ntm_recv_pool));

	c2_mutex_lock(&tm->ntm_mutex);
	c2_net_buffer_pool_lock(tm->ntm_recv_pool);
	tm_provision_recv_q(tm);
	c2_net_buffer_pool_unlock(tm->ntm_recv_pool);
	c2_mutex_unlock(&tm->ntm_mutex);
	return;
}

void c2_net_tm_recv_pool_buffer_put(struct c2_net_buffer *nb)
{
	struct c2_net_transfer_mc *tm;
	C2_PRE(nb != NULL);
	tm = nb->nb_tm;
	C2_PRE(tm != NULL);
	C2_PRE(tm->ntm_recv_pool != NULL && nb->nb_pool !=NULL);
	C2_PRE(tm->ntm_recv_pool == nb->nb_pool);
	C2_PRE(!(nb->nb_flags & C2_NET_BUF_QUEUED));

	c2_net_buffer_pool_lock(tm->ntm_recv_pool);
	c2_net_buffer_pool_put(tm->ntm_recv_pool, nb,
			       tm->ntm_pool_colour);
	c2_net_buffer_pool_unlock(tm->ntm_recv_pool);
}

void c2_net_tm_recv_pool_buffers_put(struct c2_net_transfer_mc *tm)
{
	struct c2_net_domain *net_dom;
	struct c2_net_buffer *nb;
	struct c2_tl	     *ql;

	C2_PRE(tm != NULL);
	C2_PRE(tm->ntm_dom != NULL);
	C2_PRE(tm->ntm_recv_pool != NULL);

	net_dom = tm->ntm_dom;
	ql = &tm->ntm_q[C2_NET_QT_MSG_RECV];

	c2_tlist_for(&tm_tl, ql, nb) {
		c2_net_tm_recv_pool_buffer_put(nb);
	} c2_tlist_endfor;
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
