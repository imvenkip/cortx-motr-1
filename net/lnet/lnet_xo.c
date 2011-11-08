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
   @page LNetDLD LNet Transport DLD

   - @ref LNetDLD-ovw
   - @ref LNetDLD-def
   - @ref LNetDLD-req
   - @ref LNetDLD-depends
   - @ref LNetDLD-highlights
   - @subpage LNetDLD-fspec "Functional Specification" <!-- ext link -->
      - @ref LNetDFS "LNet Transport Interface"       <!-- ext link -->
      - @ref LNetIDFS "Internal Interface"            <!-- int link -->
   - @ref LNetDLD-lspec
      - @ref LNetDLD-lspec-comps
      - @ref LNetDLD-lspec-state
      - @ref LNetDLD-lspec-thread
      - @ref LNetDLD-lspec-numa
   - @ref LNetDLD-conformance
   - @ref LNetDLD-ut
   - @ref LNetDLD-st
   - @ref LNetDLD-O
   - @ref LNetDLD-ref

   <hr>
   @section LNetDLD-ovw Overview
   <i>All specifications must start with an Overview section that
   briefly describes the document and provides any additional
   instructions or hints on how to best read the specification.</i>

   This document describes the Colibri Network transport for LNet.

   <hr>
   @section LNetDLD-def Definitions
   <i>Mandatory.
   The DLD shall provide definitions of the terms and concepts
   introduced by the design, as well as the relevant terms used by the
   specification but described elsewhere.  References to the
   C2 Glossary are permitted and encouraged.  Agreed upon terminology
   should be incorporated in the glossary.</i>

   Previously defined terms:

   New terms:

   <hr>
   @section LNetDLD-req Requirements
   <i>Mandatory.
   The DLD shall state the requirements that it attempts to meet.</i>

   - <b>r.c2.net.xprt.lnet.transport-variable</b> The implementation
     shall name the transport variable as specified in the HLD.

   - <b>r.c2.net.lnet.buffer-registration</b> Provide support for
     hardware optimization through buffer pre-registration.

   - <b>r.c2.net.xprt.lnet.end-point-address</b> The implementation
     should support the mapping of end point address to LNet address
     as described in the Refinement section of the HLD.

   - <b>r.c2.net.xprt.lnet.multiple-messages-in-buffer</b> Provide
     support for this feature as described in the HLD.

   - <b>r.c2.net.xprt.lnet.dynamic-address-assignment</b> Provide
     support for dynamic address assignment as described in the HLD.

   - <b>r.c2.net.xprt.lnet.processor-affinity</b> The implementation
     must support processor affinity as described in the HLD.

   - <b>r.c2.net.xprt.lnet.user-space</b> The implementation must
     accommodate the needs of the user space LNet transport.

   <hr>
   @section LNetDLD-depends Dependencies
   <i>Mandatory. Identify other components on which this specification
   depends.</i>

   - @ref LNetCore "LNet Transport Core Interface"
   - @ref cqueueDLD "Circular Queue for Single Producer and Consumer DLD"
   - The processor API and control over thread affinity to processor.

   <hr>
   @section LNetDLD-highlights Design Highlights
   <i>Mandatory. This section briefly summarizes the key design
   decisions that are important for understanding the functional and
   logical specifications, and enumerates topics that need special
   attention.</i>

   - Common user and kernel space implementation over an underlying "Core" I/O
     layer.
   - Handles I/O flow control
   - Provides processor affinity


   <hr>
   @section LNetDLD-lspec Logical Specification
   <i>Mandatory.  This section describes the internal design of the component,
   explaining how the functional specification is met.  Sub-components and
   diagrams of their interaction should go into this section.  The section has
   mandatory subsections created using the Doxygen @@subsection command.  The
   designer should feel free to use additional sub-sectioning if needed, though
   if there is significant additional sub-sectioning, provide a table of
   contents here.</i>

   - @ref LNetDLD-lspec-comps
   - @ref LNetDLD-lspec-state
   - @ref LNetDLD-lspec-thread
   - @ref LNetDLD-lspec-numa

   @subsection LNetDLD-lspec-comps Component Overview
   <i>Mandatory.
   This section describes the internal logical decomposition.
   A diagram of the interaction between internal components and
   between external consumers and the internal components is useful.</i>

   The focus of the LNet transport is the implementation of the asynchronous
   semantics required by the Colibri Networking layer.  I/O is performed by an
   underlying "core" layer, which does the actual interaction with the Lustre
   LNet kernel module.  The core layer permits the LNet transport code to be
   written in an address space agnostic fashion, as it offers the same
   interface in both user and kernel space.


   @subsection LNetDLD-lspec-state State Specification
   <i>Mandatory.
   This section describes any formal state models used by the component,
   whether externally exposed or purely internal.</i>


   @subsection LNetDLD-lspec-thread Threading and Concurrency Model
   <i>Mandatory.
   This section describes the threading and concurrency model.
   It describes the various asynchronous threads of operation, identifies
   the critical sections and synchronization primitives used
   (such as semaphores, locks, mutexes and condition variables).</i>

   @subsection LNetDLD-lspec-numa NUMA optimizations
   <i>Mandatory for components with programmatic interfaces.
   This section describes if optimal behavior can be supported by
   associating the utilizing thread to a single processor.</i>

   <hr>
   @section LNetDLD-conformance Conformance
   <i>Mandatory.
   This section cites each requirement in the @ref LNetDLD-req section,
   and explains briefly how the LNet meets the requirement.</i>

   <hr>
   @section LNetDLD-ut Unit Tests
   <i>Mandatory. This section describes the unit tests that will be designed.
   </i>


   <hr>
   @section LNetDLD-st System Tests
   <i>Mandatory.
   This section describes the system testing done, if applicable.</i>

   <hr>
   @section LNetDLD-O Analysis
   <i>This section estimates the performance of the component, in terms of
   resource (memory, processor, locks, messages, etc.) consumption,
   ideally described in big-O notation.</i>

   <hr>
   @section LNetDLD-ref References
   <i>Mandatory. Provide references to other documents and components that
   are cited or used in the design.
   In particular a link to the HLD for the DLD should be provided.</i>

   - <a href="https://docs.google.com/a/xyratex.com/document/d/1TZG__XViil3ATbWICojZydvKzFNbL7-JJdjBbXTLgP4/edit?hl=en_US">HLD of Colibri LNet Transport</a>
   - @subpage KLNetCoreDLD "LNet Transport Kernel Core DLD"
   - @subpage ULNetCoreDLD "LNet Transport User Space Core DLD"
   - @ref cqueueDLD "Circular Queue for Single Producer and Consumer DLD"

 */

/*
 ******************************************************************************
 End of DLD
 ******************************************************************************
 */

#include "net/lnet/lnet_core.h"

/**
   @defgroup LNetIDFS LNet Transport Internal Interface
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

static const struct c2_net_xprt_ops lnet_xo_xprt_ops = {
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
   @} LNetIDFS
*/

struct c2_net_xprt c2_net_lnet_xprt = {
	.nx_name = "lnet",
	.nx_ops  = &lnet_xo_xprt_ops
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

