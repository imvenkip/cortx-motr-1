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
 * Original author: Carl Braganza <carl_braganza@xyratex.com>
 *                  Dave Cohrs <dave_cohrs@xyratex.com>
 * Original creation: 08/17/2012
 */

/**
   @page ADDB-DLD-RSINK RPC Sink Detailed Design

   This is a component DLD and hence not all sections are present.
   Refer to @ref ADDB-DLD "ADDB Detailed Design"
   for the design requirements.

   - @ref ADDB-DLD-RPCSINK-depends
   - @ref ADDB-DLD-RPCSINK-lspec
      - @ref ADDB-DLD-RPCSINK-lspec-state
      - @ref ADDB-DLD-RPCSINK-lspec-thread
      - @ref ADDB-DLD-RPCSINK-lspec-numa
   - @ref ADDB-DLD-RPCSINK-ut
   - @ref ADDB-DLD-RPCSINK-O

   <hr>
   @section ADDB-DLD-RPCSINK-depends RPC Sink Dependencies

   The RPC sink component depends on the RPC subsystem to provide the
   mechanisms needed to send ADDB records to an ADDB service:

   -# ADDB records will be sent in a ::m0_rpc_item, either reusing the existing
   @ref fop "fop" data structure, or, alternatively, using a new non-fop data
   structure with corresponding changes to the request handler service delivery
   interface.  On the client side, instead of using m0_rpc_post() to transmit a
   pre-assembled item, a "pull" model will be used to create an item on demand
   to fill in the remaining space in an RPC packet during formation.  This is
   represented by a proposed m0_rpc_item_source object, which has to be
   registered by the higher level application after establishing a connection to
   a service with the m0_rpc_conn_create() subroutine:
@code
   struct m0_rpc_item_source_ops {
	bool             (*riso_has_item)(const struct m0_rpc_item_source *ris);
	struct m0_rpc_item *(*riso_get_item)(struct m0_rpc_item_source *ris,
                                             struct m0_rpc_session     *sess,//?
                                             size_t           available_space);
   };
   struct m0_rpc_item_source {
        uint64_t                             ris_magic;
        const char                          *ris_name;
	const struct m0_rpc_item_source_ops *ris_ops;
        struct m0_rpc_machine               *ris_machine;
        struct m0_tlink                      ris_linkage;
   };
   struct m0_rpc_machine {
        ...
        struct m0_tl rm_item_sources;
   };
   int m0_rpc_item_source_init(struct m0_rpc_item_source *ris,
                               const char *name,
			       const struct m0_rpc_item_source_ops *ops);
   int m0_rpc_item_source_fini(struct m0_rpc_item_source *ris);
   int m0_rpc_item_source_register(struct m0_rpc_conn *conn,
                                   struct m0_rpc_item_source *ris);
   int m0_rpc_item_source_deregister(struct m0_rpc_item_source *ris);
@endcode
   The callback subroutines are invoked within the scope of the RPC machine
   lock so should not make re-entrant calls to the RPC subsystem other than
   the following:
        - m0_rpc_item_init()
	- m0_rpc_item_fini()
	- m0_rpc_item_size()
   -# The ADDB subsystem will define a ::m0_rpc_item_source object for the
   higher level application (typically the Mero file system) to register on
   its behalf.  The application has access to configuration information and will
   use this to ensure that the ADDB provided source is only registered for
   connections to Mero servers that define an ADDB service.
   -# The RPC machine must make periodic sweeps over item sources to
   drain pending items that could not be packed into RPC packets.
   Presumably the RPC machine could do this off its existing timer threads.
   The following pseudo-code illustrates this:
@code
   size_t max_size = rpcmach->rm_min_rec_size - ITEM_ONWIRE_HEADER_SIZE;
   m0_tl_for(..., &rpcmach->rm_item_sources, itsrc) {
       while (itsrc->ris_ops->riso_has_item()) {
             // loop over sessions?
             struct m0_rpc_item *ri = itsrc->ris_ops->riso_get_item(...,
	                                                            max_size);
             if (ri != NULL)
	          m0_rpc__post_locked(ri);
       }
   } m0_tl_endfor;
@endcode
   The source creates a single ::m0_rpc_item that gets immediately posted; there
   may still be items left, so the inner loop will continue until each
   individual source is drained.  It would be overly complicated to try and
   optimize further by filling in any remaining space in an RPC packet with an
   item from a subsequent source.
   -# The RPC machine must provide one-way, best-effort support to transmit
     the ADDB rpc item.  No indication of remote reception is required.

   <hr>
   @section ADDB-DLD-RPCSINK-lspec RPC Sink Logical Specification
      - @ref ADDB-DLD-RPCSINK-lspec-state
      - @ref ADDB-DLD-RPCSINK-lspec-thread
      - @ref ADDB-DLD-RPCSINK-lspec-numa

   @subsection ADDB-DLD-RPCSINK-lspec-state RPC Sink State Specification

   @subsection ADDB-DLD-RPCSINK-lspec-thread RPC Sink Threading and
   Concurrency Model

   @subsection ADDB-DLD-RPCSINK-lspec-numa RPC Sink NUMA optimizations

   <hr>
   @section ADDB-DLD-RPCSINK-ut RPC Sink Unit Tests

   <hr>
   @section ADDB-DLD-RPCSINK-O RPC Sink Analysis

 */


/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
