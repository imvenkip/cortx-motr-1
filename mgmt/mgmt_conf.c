/* -*- C -*- */
/*
 * COPYRIGHT 2013 XYRATEX TECHNOLOGY LIMITED
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
 * Original author: Dave Cohrs <dave_cohrs@xyratex.com>
 * Original creation date: 13-Mar-2013
 */

/**
   @page MGMT-CONF-DLD Management Configuration Internals

   This is a component DLD and hence not all sections are present.
   Refer to the @ref MGMT-DLD "Management Interface Design"
   for the design requirements.

   - @ref MGMT-CONF-DLD-ovw
   - @ref MGMT-CONF-DLD-fspec
     - @ref MGMT-CONF-DLD-fspec-ds
     - @ref MGMT-CONF-DLD-fspec-sub
   - @ref MGMT-CONF-DLD-lspec

   <hr>
   @section MGMT-CONF-DLD-ovw Overview
   - An API and data structures to query for the configuration
   of a Mero service node.
   - Used by both m0ctl and m0d.

   <hr>
   @section MGMT-CONF-DLD-fspec Functional Specification
   - @ref MGMT-CONF-DLD-fspec-ds
   - @ref MGMT-CONF-DLD-fspec-sub

   @subsection MGMT-CONF-DLD-fspec-ds Data Structures

   The following data structures are provided:
   - m0_mgmt_node_conf
   - m0_mgmt_svc_conf

   @todo Use @ref conf "m0_conf" data structures once they are extended to
   support mero service nodes.

   @subsection MGMT-CONF-DLD-fspec-sub Subroutines and Macros

   The following functions are provided to access node configuration:
   - m0_mgmt_node_conf_init()
   - m0_mgmt_node_conf_fini()

   <hr>
   @section MGMT-CONF-DLD-lspec Logical Specification
   - @ref MGMT-CONF-DLD-lspec-comps
   - @ref MGMT-CONF-DLD-lspec-thread

   @subsection MGMT-CONF-DLD-lspec-comps Component Overview
   The management configuration functions provide a mechanism to determine
   the configuration parameters applicable to managing a mero node.

   At present, only server nodes are addressed.

   A genders file, as described in @ref MGMT-DLD-lspec-genders
   is the basis of management configuration.

   The m0_mgmt_node_conf_init() function parses such a file and populates the
   provided m0_mgmt_node_conf object.  Additional memory is allocated to store
   various configuration information, including the information for each
   service to be started on the node.  The raw network parameters are used
   to build up the values returned in the m0_mgmt_node_conf::mnc_m0d_ep and
   m0_mgmt_node_conf::mnc_client_ep members.

   Note that because @ref libgenders3 "libgenders3" is GPL, the nodeattr
   CLI is used to query the genders file, and its output is parsed.

   The m0_mgmt_node_conf_fini() function frees memory allocated by
   m0_mgmt_node_conf_init() and returns the m0_mgmt_node_conf object to
   a pre-initilized state.

   @subsection MGMT-CONF-DLD-lspec-thread Threading and Concurrency Model
   No threads are created.  No internal synchronization is provided.

   <hr>
   @section MGMT-DLD-ut
   - Tests for m0_mgmt_node_conf_init() to test parsing various genders files.

 */

#include "lib/errno.h"
#include "mgmt/mgmt.h"

/**
   @addtogroup mgmt
   @{
 */

M0_INTERNAL int m0_mgmt_node_conf_init(struct m0_mgmt_node_conf *conf,
				       const char *genders)
{
	return -ENOSYS;
}

M0_INTERNAL void m0_mgmt_node_conf_fini(struct m0_mgmt_node_conf *conf)
{
}

/** @} end of mgmt group */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
