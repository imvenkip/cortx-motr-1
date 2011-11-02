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
 * Original author: Carl Braganza <Carl_Braganza@us.xyratex.com>
 *                  Dave Cohrs <Dave_Cohrs@us.xyratex.com>
 * Original creation date: 11/01/2011
 */

/**
   @page KLNet LNet Kernel Transport Detailed Level Design

   - @ref KLNet-ovw
   - @ref KLNet-def
   - @ref KLNet-req
   - @ref KLNet-depends
   - @ref KLNet-highlights
   - @subpage KLNet-fspec "Functional Specification" <!-- ext link -->
      - @ref LNetDFS "Consumer Interfaces"           <!-- ext link -->
      - @ref KLNetCore "Core Interfaces"             <!-- ext link -->
      - @ref KLNetIDFS "Internal Interfaces"         <!-- int link -->
   - @ref KLNet-lspec
      - @ref KLNet-lspec-comps
      - @ref KLNet-lspec-state
      - @ref KLNet-lspec-thread
      - @ref KLNet-lspec-numa
   - @ref KLNet-conformance
   - @ref KLNet-ut
   - @ref KLNet-st
   - @ref KLNet-O
   - @ref KLNet-ref

   <hr>
   @section KLNet-ovw Overview
   <i>All specifications must start with an Overview section that
   briefly describes the document and provides any additional
   instructions or hints on how to best read the specification.</i>

   <hr>
   @section KLNet-def Definitions
   <i>Mandatory.
   The DLD shall provide definitions of the terms and concepts
   introduced by the design, as well as the relevant terms used by the
   specification but described elsewhere.  References to the
   C2 Glossary are permitted and encouraged.  Agreed upon terminology
   should be incorporated in the glossary.</i>

   Previously defined terms:

   New terms:

   <hr>
   @section KLNet-req Requirements
   <i>Mandatory.
   The DLD shall state the requirements that it attempts to meet.</i>

   - <b>r.c2.net.xprt.lnet.transport-variable</b> The implementation
     shall name the transport variable as specified in this document.

   - <b>r.c2.net.xprt.lnet.end-point-address</b> The implementation
     should support the mapping of end point address to LNet address
     as described in Mapping of Endpoint Address to LNet Address,
     including the reservation of a portion of the match bit space in
     which to encode the transfer machine identifier.

   - <b>r.c2.net.xprt.support-for-auto-provisioned-receive-queue</b>
     The implementation should follow the strategy outlined in Automatic
     provisioning of receive buffers. It should also follow the
     serialization model outlined in Concurrency control.

   - <b>r.c2.net.xprt.lnet.multiple-messages-in-buffer</b>
      - Add a nb_min_receive_size field to struct c2_net_buffer.
      - Document the behavioral change of the receive message callback.
      - Provide a mechanism for the transport to indicate that the
        C2_NET_BUF_QUEUED flag should not be cleared by the
        c2_net_buffer_event_post subroutine.
      - Modify all existing usage to set the nb_min_receive_size field
        to the buffer length.

   - <b>r.c2.net.xprt.lnet.efficient-user-to-kernel-comm</b> The
     implementation should follow the strategies recommended in
     Efficient communication between user and kernel spaces, including
     the creation of a private device driver to facilitate such
     communication.

   - <b>r.c2.net.xprt.lnet.cleanup-on-process-termination</b> The
     implementation should release all kernel resources held by a
     process using the LNet transport when that process terminates.

   - <b>r.c2.net.xprt.lnet.dynamic-address-assignment</b> The
     implementation may support dynamic assignment of transfer machine
     identifier using the strategy outlined in Mapping of Endpoint
     Address to LNet Address. We recommend that the implementation
     dynamically assign transfer machine identifiers from higher
     numbers downward to reduce the chance of conflicting with
     well-known transfer machine identifiers.

   - <b>r.c2.net.xprt.lnet.processor-affinity</b> The implementation
     must provide support for this feature, as outlined in Processor
     affinity for transfer machines.  The implementation will need to
     define an additional transport operation to convey this request
     to the transport. Availability may vary by kernel or user space.

   <hr>
   @section KLNet-depends Dependencies
   <i>Mandatory. Identify other components on which this specification
   depends.</i>

   <hr>
   @section KLNet-highlights Design Highlights
   <i>Mandatory. This section briefly summarizes the key design
   decisions that are important for understanding the functional and
   logical specifications, and enumerates topics that need special
   attention.</i>


   <hr>
   @section KLNet-lspec Logical Specification
   <i>Mandatory.  This section describes the internal design of the component,
   explaining how the functional specification is met.  Sub-components and
   diagrams of their interaction should go into this section.  The section has
   mandatory subsections created using the Doxygen @@subsection command.  The
   designer should feel free to use additional sub-sectioning if needed, though
   if there is significant additional sub-sectioning, provide a table of
   contents here.</i>

   - @ref KLNet-lspec-comps
   - @ref KLNet-lspec-state
   - @ref KLNet-lspec-thread
   - @ref KLNet-lspec-numa

   @subsection KLNet-lspec-comps Component Overview
   <i>Mandatory.
   This section describes the internal logical decomposition.
   A diagram of the interaction between internal components and
   between external consumers and the internal components is useful.</i>


   @subsection KLNet-lspec-state State Specification
   <i>Mandatory.
   This section describes any formal state models used by the component,
   whether externally exposed or purely internal.</i>


   @subsection KLNet-lspec-thread Threading and Concurrency Model
   <i>Mandatory.
   This section describes the threading and concurrency model.
   It describes the various asynchronous threads of operation, identifies
   the critical sections and synchronization primitives used
   (such as semaphores, locks, mutexes and condition variables).</i>

   @subsection KLNet-lspec-numa NUMA optimizations
   <i>Mandatory for components with programmatic interfaces.
   This section describes if optimal behavior can be supported by
   associating the utilizing thread to a single processor.</i>

   <hr>
   @section KLNet-conformance Conformance
   <i>Mandatory.
   This section cites each requirement in the @ref KLNet-req section,
   and explains briefly how the KLNet meets the requirement.</i>

   <hr>
   @section KLNet-ut Unit Tests
   <i>Mandatory. This section describes the unit tests that will be designed.
   </i>


   <hr>
   @section KLNet-st System Tests
   <i>Mandatory.
   This section describes the system testing done, if applicable.</i>

   <hr>
   @section KLNet-O Analysis
   <i>This section estimates the performance of the component, in terms of
   resource (memory, processor, locks, messages, etc.) consumption,
   ideally described in big-O notation.</i>

   <hr>
   @section KLNet-ref References
   <i>Mandatory. Provide references to other documents and components that
   are cited or used in the design.
   In particular a link to the HLD for the DLD should be provided.</i>

   - <a href="https://docs.google.com/a/xyratex.com/document/d/1TZG__XViil3ATbWICojZydvKzFNbL7-JJdjBbXTLgP4/edit?hl=en_US">HLD of Colibri LNet Transport</a>

 */

/*
 ******************************************************************************
 End of DLD
 ******************************************************************************
 */

#include "net/lnet/linux_kernel/klnet_core.h"

/**
   @defgroup KLNetIDFS LNet Kernel Transport Internal Interfaces
   @ingroup LNetDFS
   @{
*/

static int lnet_xo_dom_init(struct c2_net_xprt *xprt, struct c2_net_domain *dom)
{
}

static void lnet_xo_dom_fini(struct c2_net_domain *dom)
{
}

static c2_bcount_t lnet_xo_get_max_buffer_size(const struct c2_net_domain *dom)
{
}

static c2_bcount_t lnet_xo_get_max_buffer_segment_size(const struct
						       c2_net_domain *dom)
{
}

static int32_t lnet_xo_get_max_buffer_segments(const struct c2_net_domain *dom)
{
}

static int lnet_xo_end_point_create(struct c2_net_end_point **epp,
				    struct c2_net_transfer_mc *tm,
				    const char *addr)
{
}

static int lnet_xo_buf_register(struct c2_net_buffer *nb)
{
}

static void lnet_xo_buf_deregister(struct c2_net_buffer *nb)
{
}

static int lnet_xo_buf_add(struct c2_net_buffer *nb)
{
}

static void lnet_xo_buf_del(struct c2_net_buffer *nb)
{
}

static int lnet_xo_tm_init(struct c2_net_transfer_mc *tm)
{
}

static void lnet_xo_tm_fini(struct c2_net_transfer_mc *tm)
{
}

static int lnet_xo_tm_start(struct c2_net_transfer_mc *tm, const char *addr)
{
}

static int lnet_xo_tm_stop(struct c2_net_transfer_mc *tm, bool cancel)
{
}

static const struct c2_net_xprt_ops klnet_xo_xprt_ops = {
	.xo_dom_init                    = lnet_xo_dom_init,
	.xo_dom_fini                    = lnet_xo_dom_fini,
	.xo_get_max_buffer_size         = lnet_xo_get_max_buffer_size,
	.xo_get_max_buffer_segment_size = lnet_xo_get_max_buffer_segment_size,
	.xo_get_max_buffer_segments     = lnet_xo_get_max_buffer_segments,
	.xo_end_point_create            = lnet_xo_end_point_create,
	.xo_buf_register                = lnet_xo_buf_register,
	.xo_buf_deregister              = lnet_xo_buf_deregister,
	.xo_buf_add                     = lnet_xo_buf_add,
	.xo_buf_del                     = lnet_xo_buf_del,
	.xo_tm_init                     = lnet_xo_tm_init,
	.xo_tm_fini                     = lnet_xo_tm_fini,
	.xo_tm_start                    = lnet_xo_tm_start,
	.xo_tm_stop                     = lnet_xo_tm_stop,
};

/**
   @} KLNetIDFS
*/

struct c2_net_xprt c2_net_lnet_xprt = {
	.nx_name = "lnet",
	.nx_ops  = &klnet_xo_xprt_ops
};
C2_EXPORTED(c2_net_lnet_xprt);

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 79
 *  scroll-step: 1
 *  End:
 */

