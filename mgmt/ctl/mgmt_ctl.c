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
 * Original creation date: 12-Mar-2013
 */

/**
   @page MGMT-CTL-DLD Management CLI Design
   - @ref MGMT-CTL-DLD-ovw
   - @ref MGMT-CTL-DLD-req
   - @ref MGMT-CTL-DLD-depends
   - @ref MGMT-CTL-DLD-highlights
   - @ref MGMT-CTL-DLD-lspec
      - @ref MGMT-CTL-DLD-lspec-comps
      - @ref MGMT-CTL-DLD-lspec-mgmt-ctl
      - @ref MGMT-CTL-DLD-lspec-service
      - @ref MGMT-CTL-DLD-lspec-state
      - @ref MGMT-CTL-DLD-lspec-thread
      - @ref MGMT-CTL-DLD-lspec-numa
   - @ref MGMT-CTL-DLD-conformance
   - @ref MGMT-CTL-DLD-ut
   - @ref MGMT-CTL-DLD-st
   - @ref MGMT-CTL-DLD-O
   - @ref MGMT-CTL-DLD-impl-plan

   <hr>
   @section MGMT-CTL-DLD-ovw Overview
   This design describes CLI interfaces for managing mero, specifically
   for managing the mero kernel module and m0d process.  Mechanisms for
   starting, stopping and getting status of mero are provided.  A process
   for extending the management CLI is also presented.

   The design operates within the standard deployment pattern parameters
   outlined in @ref MGMT-DLD-lspec-osif "Extensions for the service command".

   <hr>
   @section MGMT-CTL-DLD-req Requirements
   The management service will address the following requirements described
   in the @ref MGMT-DLD-req "Management DLD".
   - @b R.cli.mgmt-api.services
   - @b R.cli.mgmt-api.query
   - @b R.cli.mgmt-api.start
   - @b R.cli.mgmt-api.shutdown

   <hr>
   @section MGMT-CTL-DLD-depends Dependencies
   - A management service as described in
   @ref MGMT-SVC-DLD "Management Service DLD"
   provides the internal APIs to implement query and service-specific
   management interfaces.
   - The @ref libgenders3 "genders" package from LLNL must be installed on all
   nodes.
     - The @c /usr/bin/nodeattr CLI is used to query the /etc/mero/genders file.
   - The m0d command must be enhanced to support bootstrap configuration from
   the /etc/mero/genders file.  This involves support for a new "-g" option
   to denote this mode of bootstrap configuration, and a corresponding
   "-f genderspath" option.
   - The ADDB subsystem must be extended to allow a mero CLI to run even
   before the mero kernel module is loaded.  This requires removing an assertion
   that the node UUID is set and providing an API to set the node UUID once
   it is known (e.g. by reading a genders file).

   <hr>
   @section MGMT-CTL-DLD-highlights Design Highlights
   - A Linux standard @c service script is provided to start, stop
   and query status of mero services.
   - A management CLI, @c m0ctl, is provided.
     - It implements the query status fuctionality of the @c service script.
     - It provides a pattern for providing future management functions.

   <hr>
   @section MGMT-CTL-DLD-lspec Logical Specification
   - @ref MGMT-CTL-DLD-lspec-comps
   - @ref MGMT-CTL-DLD-lspec-service
   - @ref MGMT-CTL-DLD-lspec-mgmt-ctl
   - @ref MGMT-CTL-DLD-lspec-state
   - @ref MGMT-CTL-DLD-lspec-thread
   - @ref MGMT-CTL-DLD-lspec-numa

   @subsection MGMT-CTL-DLD-lspec-comps Component Overview
   This design involves the following sub-components:
   - A script to be used by the Linux service CLI.
   - A management CLI.

   @subsection MGMT-CTL-DLD-lspec-service The Service CLI Interface
   The primary external interface for managing Mero services is the Linux
   @c service CLI.  The high-level behavior provided is described in
   @ref MGMT-DLD-lspec-osif.

   An /etc/rc.d/init.d/mero script implements the three supported directives:
   start, stop and status.

   The "start" directive performs the following steps:
   - It uses @c nodeattr to determine the configuration of the current node.
   If the node is not configured (m0_uuid, m0_var specified, etc), nothing
   is started.
   - It loads the Lustre lnet module if not already loaded.
   - It loads the m0mero module.
   - It changes to the directory specified in the m0_var attribute.
   - It starts the a m0d process, specifying that m0d should use the
   /etc/mero/genders file to bootstrap its configuration.
     - Functions in the /etc/rc.d/init.d/functions are used for this, with
     the side effect that the pid of the m0d is stored in a file in /var/run.

   The "stop" directive performs the following steps:
   - It sends a signal to the m0d process and waits for it to terminate.
     - Functions in the /etc/rc.d/init.d/functions are used for this.
   - It unloads the m0mero module.
   - The Lustre lnet module is not unloaded.

   The "status" directive performs the following steps:
   - It uses the m0ctl CLI to determine and print the status of the running
   m0d process, using the "query-all" operation.

   @subsection MGMT-CTL-DLD-lspec-mgmt-ctl The Management Client
   The management client is modular.  Its main program performs common
   functionality and the calls an operation-specific "main" function to
   execute a given operation.

   The common functionality includes:
   - Parsing common options and populating a m0_mgmt_ctl_ctx.
   - Determining which operation to perform.
   - Parsing the genders file.
   - Setting up ADDB in the process, including setting the node UUID.

   The external inteface of m0ctl is described in
   @ref MGMT-DLD-ref-svc-plan "Mero Service Interface Planning"
   and is not repeated here.

   In the specific case of the "query-all" operation:
   - The remaining argv are parsed (to determine optional repeat rate).
   - Send an m0_fop_mgmt_service_state_req FOP to the m0d mgmt service,
   requesting status of all services.
   - Wait for response.
   - Output status of all services, one line for each service, in the form
   service_name(UUID): State (equivlent output when YAML format is requested).
   - If a repeat rate was specified, sleep for the requested interval and
   loop back to sending the m0_fop_mgmt_service_state_req FOP.

   If an error occurs in the attempt to send the FOP or a timeout occurs
   waiting for the response, an error message is output instead.  If a repeat
   rate was specified, an error will not cause the "query-all" operation
   to terminate.  The "query-all" operation with a repeat rate will only
   terminate if an error occurs when it is generating output (e.g. if the
   output is being sent to a pipe and the other end of the pipe closes).
   Otherwise, the parent process can terminate the "query-all" operation by
   sending a signal the sub-process.

   @subsection MGMT-CTL-DLD-lspec-state State Specification
   These components have no formal state machines.

   @subsection MGMT-CTL-DLD-lspec-thread Threading and Concurrency Model
   At present, the m0ctl CLI is single-threaded (it may use functions in the
   Mero library that create additional, internal threads, but it does not
   depend on such multi-threaded behavior).

   @subsection MGMT-CTL-DLD-lspec-numa NUMA optimizations
   The m0ctl CLI requires and provides no NUMA optimizations.

   <hr>
   @section MGMT-CTL-DLD-conformance Conformance
   - @b I.cli.mgmt-api.services An /etc/rc.d/init.d script is provided for
   local services management.
   - @b I.cli.mgmt-api.query The m0ctl "query-all" operation is provided.
   - @b I.cli.mgmt-api.start An /etc/rc.d/init.d script is provided.
   - @b I.cli.mgmt-api.shutdown An /etc/rc.d/init.d script is provided.

   <hr>
   @section MGMT-CTL-DLD-ut Unit Tests
   Unit testing will be limited to tests of
   @ref MGMT-CONF-DLD "Management Configuration", as this
   is the only component that resides in the mero library.

   <hr>
   @section MGMT-CTL-DLD-st System Tests
   A system test script will be used to verify:
   - Correct CLI parsing
   - Operational (currently, query-all) behavior.

   <hr>
   @section MGMT-CTL-DLD-O Analysis
   Parsing of the genders file only occurs on startup and should have no
   significant impact on normal operation.  When m0ctl "query-all" is used
   with a repeat-rate, this is true of m0ctl as well.

   <hr>
   @section MGMT-CTL-DLD-impl-plan Implementation Plan
   - Initial implementation of m0ctl to support query-all and looping behavior.
   - The standard start, stop and status commands will be implemented in the
   /etc/rc.d/init.d script.

   Other features as required by the shipping product.

 */


/** An array of management operations */
static struct m0_mgmt_ctl_op ops[] = {
	{ .cto_op = "query-all", .ctl_main = query_all_main },
};

static struct m0_mgmt_ctx ctx;

int main(int argc, char *argv[])
{
}

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
