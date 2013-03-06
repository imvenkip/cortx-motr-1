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
 * Original author: Carl Braganza <carl_braganza@xyratex.com>
 * Original creation date: 5-Mar-2013
 */

/**
   @page MGMT-DLD Mero Management Interfaces

   - @ref MGMT-DLD-ovw
   - @ref MGMT-DLD-def
   - @ref MGMT-DLD-req
   - @ref MGMT-DLD-depends
   - @ref MGMT-DLD-highlights
   - @subpage MGMT-DLD-fspec "Functional Specification" <!-- Note @subpage -->
   - @ref MGMT-DLD-lspec
      - @ref MGMT-DLD-lspec-comps
      - @ref MGMT-DLD-lspec-osif
      - @ref MGMT-DLD-lspec-genders
      - @ref MGMT-DLD-lspec-hosts
      - @ref MGMT-SVC-DLD "The management service"
      - @ref MGMT-M0MC-DLD "The management command (m0mc)"
      - @ref MGMT-DLD-lspec-state
      - @ref MGMT-DLD-lspec-thread
      - @ref MGMT-DLD-lspec-numa
   - @ref MGMT-DLD-conformance
   - @ref MGMT-DLD-ut
   - @ref MGMT-DLD-st
   - @ref MGMT-DLD-O
   - @ref MGMT-DLD-ref
   - @ref MGMT-DLD-impl-plan

   Additional design details are found in component DLDs:
   - @subpage MGMT-SVC-DLD "The Management Service Detailed Design"
   - @subpage MGMT-M0MC-DLD "The m0mc Command Detailed Design"

   <hr>
   @section MGMT-DLD-ovw Overview

   The Management module provides external interfaces with which to manage
   Mero.  The interfaces take the form of over-the-wire @ref fop "FOP"
   specifications that are exchanged with a management service, and
   command line utilities necessary for interaction with external
   subsystems such as the HA subsystem. @ref MGMT-DLD-ref-svc-plan "[0]".

   The Management module provides @i mechanisms by which Mero is managed;
   it does not provide @i policy.  This DLD and associated documents
   should aid in the development of middle-ware required to successfully
   deploy a Mero based product such as
   @ref MGMT-DLD-ref-mw-prod-plan "WOMO [1]".

   <hr>
   @section MGMT-DLD-def Definitions

   - @b LOMO Lustre Objects over Mero.
   - @b WOMO Web Objects over Mero.
   - @b Genders The @ref libgenders3 "libgenders(3)" database subsystem that
   is used in cluster administration.  The module is in the public domain, and
   originated from LLNL.

   <hr>
   @section MGMT-DLD-req Requirements

   These requirements originate from @ref MGMT-DLD-ref-svc-plan "[0]".
   - @b R.reqh.mgmt-api.service.start Provide an external interface to
   start a service through an active request handler.
   - @b R.reqh.startup.synchronous During start-up, services should not
   respond to FOP requests until all services are ready.
   - @b R.reqh.mgmt-api.service.stop Provide an external interface to stop
   a service running under a request handler.
   - @b R.reqh.mgmt-api.service.query Provide an external interface to query
   the status of services running under a request handler.
   - @b R.reqh.mgmt-api.service.query-failed Provide an external interface to
   query the list of services that have failed.
   - @b R.reqh.mgmt-api.shutdown Provide an external interface to gracefully
   shut down all services of a request handler.  It should be possible to force
   the shutdown of all services.  It should be possible to gracefully shutdown
   some of the services even if some subset has failed.
   - @b R.cli.mgmt-api.services Provide support to manage local and remote Mero
   services through a CLI.  Should provide a timeout (default/configurable).
   - @b R.cli.mgmt-api.shutdown Provide support to shutdown local and remote
   Mero services through a CLI.

   These extensibility requirements also originate from
   @ref MGMT-DLD-ref-svc-plan "[0]":
   - @b R.reqh.mgmt-api.control Provide an external interface to control
   miscellaneous run time behaviors, such as trace levels, conditional logic,
   etc.
   - @b R.cli.mgmt-api.control Provide support to control miscellaneous local
   and remote Mero services through a CLI.

   <hr>
   @section MGMT-DLD-depends Dependencies

   Component DLDs will document their individual dependencies.

   - The @ref libgenders3 "genders" database is crucial to the management of
   Mero.  The design relies on external agencies to set up this database and
   propagate it to all the hosts in the cluster.
   - The design relies on external agencies to set up the Lustre Network
   subsystem.
   - The design relies on external agencies to set up the cluster hosts
   database and assign names and addresses for both normal TCP/IP and LNet
   purposes.  The hosts database must be propagated to all hosts in the cluster.

   <hr>
   @section MGMT-DLD-highlights Design Highlights
   - Describe the run time environment consisting of TCP/IP and LNet host names,
     addresses and network interfaces, host UUIDs, service configuration
     choices, run time locations, common parameters.
   - Reflect static environment data in the Genders database.
   - Adopt a standard m0d deployment configuration.
      - Provide standard operating system "service" command support to start
        and stop m0d.
   - Automatically insert a Management service in every request handler.
      - Interact with this service through Management FOPs.
      - Provide the m0mc command line program.

   <hr>
   @section MGMT-DLD-lspec Logical Specification

   - @ref MGMT-DLD-lspec-comps
   - @ref MGMT-DLD-lspec-osif
   - @ref MGMT-DLD-lspec-genders
   - @ref MGMT-DLD-lspec-hosts
   - @ref MGMT-SVC-DLD "The management service"
   - @ref MGMT-M0MC-DLD "The management command (m0mc)"
   - @ref MGMT-DLD-lspec-state
   - @ref MGMT-DLD-lspec-thread
   - @ref MGMT-DLD-lspec-numa

   @subsection MGMT-DLD-lspec-comps Component Overview
   The Management module consists of the following components:
   - @ref MGMT-DLD-lspec-osif
   - @ref MGMT-DLD-lspec-svc "The Management Service"
   - @ref MGMT-M0MC-DLD "The management command (m0mc)"

   @subsection MGMT-DLD-lspec-osif Support for the service command
   Support will be provided to start and stop m0d with the "service" command.
   This is an operating system tool commonly used to manage subsystems.

   A standard deployment pattern is adopted for this purpose:
   - The kernel module will use a well known portal number.  Its request
     handler will use a TMID of 0.
   - There will be only one m0d per node, with a well known portal number.
   - There will be only one request handler per m0d. The request handler will
     have only one network end point, using a TMID of 0.
   - Standard location in the file system for run time data.
   - Use of the /etc/hosts and /etc/genders databases to capture static
     configuration data.  It is assumed that these files will be identical
     on all nodes of the Mero cluster.

   The following functionality will be offered:
@code
# service mero start
# service mero status
# service mero stop
@endcode
   See @ref service8 "service(8)" for more details.

   - The "start" option will load the Mero kernel module and then start the m0d
   process.
   - The "stop" option will stop the m0d process and unload the kernel
   module.
   - The "query" option will use the m0mc command to query m0d and then
   return a summary status.

   The functionality is provided by scripts in the /etc/init.d directory.
   The scripts will be driven by data from the /etc/hosts and /etc/genders file.

   @subsection MGMT-DLD-lspec-genders The /etc/genders file

   The @ref libgenders3 "Genders" database will be used to store static
   configuration information describing each node in the Mero cluster.
   It is expected that this file will be replicated on each participating
   host in the Mero cluster.

   The information will include:
   - specify the node UUID
   - specify the LNet information
     - the LNet hostname and interface to use for the node
     - the portal numbers to use in the kernel, m0d and client commands
   - specify which services must run on the node
   - specify m0d flag options for individual Mero services
   - specify where Mero run time data will be located

   Information in the genders file is supplemented with the hostname to IP
   mapping from the /etc/hosts file.

   A sample genders database may look like this:
@verbatim
# Hostname to IP assumed found in the hosts database for both TCP/IP hostnames
# and LNet hostnames
h[00-10]  all
h[00-10]  lnet_if=o2ib0,lnet_pid=12345   # LNet common defaults
h[00-10]  lnet_kernel_portal=34          # portal for the kernel
h[00-10]  lnet_m0d_portal=35             # portal for m0d
h[00-10]  lnet_client_portal=36          # portal for clients (dynamic TMID)
h[00-10]  lnet_host=l%n                  # Mapping of IP to LNet hostname
h[00-10]  ws=/var/mero                   # workspace directory
h[00-10]  max_rpc_msg=163840             # max rpc message size
h[00-10]  min_recv_q=2                   # minimum receive queue length
h00,h01   s_confd=-c:%ws/confdb.txt      # hosts running confd, db file
h00,h01   s_rm                           # hosts running the resource manager
h00,h01   s_mdservice                    # hosts running the meta-data service
h[00-10]  s_addb=-A:%ws/stobs            # hosts running the ADDB service
h[00-10]  s_ioservice=-T:AD:-S:%ws/stobs # hosts running the IO service
h[00-10]  s_sns                          # hosts running SNS
h00       HA-PROXY                       # hosts running HA proxies
h00       uuid=b47539c2-143e-44e8-9594-a8f6e09bfec0
h01       uuid=6d5ddc53-b1b6-43ae-9c7c-16c227b2ea5a
h02       uuid=26a17da7-d5f2-462d-960d-205334adb028
h03       uuid=68b617e1-097a-4e46-8d16-3e202628c568
@endverbatim
   Most of the attributes in the example above are self explanatory, but
   some need to be called out:
   - s_@em Name denotes a service (type) @em Name that needs to be started
   on a node.
   The value of this attribute, if any, are a list of colon separated m0d
   arguments.
   - lnet_host This attribute provides a mapping from a node name to the
   symbolic host name associated with the IP address to use for LNet on that
   node.
   - ws is the location of the "workspace" directory where run time Mero data is
   maintained.

   Note that while the "%n" token is automatically replaced by genders with the
   node name, the "%ws" token has to be explicitly replaced by the value of
   the work space directory.

   The following illustrates some queries on the genders database above:
@verbatim
#  nodeattr -f /tmp/genders -s all
h00 h01 h02 h03 h04 h05 h06 h07 h08 h09 h10

# nodeattr -l h03
all
lnet_if=o2ib0
lnet_pid=12345
lnet_kernel_portal=34
lnet_m0d_portal=35
lnet_host=lh03
ws=/var/mero
max_rpc_msg=163840
min_recv_q=2
s_addb=-A:%ws/stobs
s_ioservice=-T:AD:-S:%ws/stobs
s_sns
uuid=68b617e1-097a-4e46-8d16-3e202628c568

# nodeattr -c s_confd
h00,h01
@endverbatim
   See @ref nodeattr8 "nodeattr(8)" for more details.
   Common cluster support utilites like @ref pdsh1 "pdsh(1)" and
   @ref pdcp1 "pdcp(1)" can also be fed the output of this command.

   @subsection MGMT-DLD-lspec-hosts The /etc/hosts file
   The @ref hosts5 "hosts(5)" database provides hostname to IP address mapping
   for the host names used in TCP/IP and LNet communication.
   It is expected that this file will be replicated on each participating
   host in the Mero cluster.

   TCP/IP host naming in the cluster must follow a pattern supported by
   @ref libgenders3 "Genders", which refers to such a name as the "node name".

   LNet end points are named with IP addresses assigned to local network
   interfaces.  This assignment is done by external agencies, not by Mero,
   and can be seen in the /etc/modprobe.d/luster.conf file.
   The IP addresses used for LNet should also be assigned symbolic host names in
   the hosts database.  There should be a straight forward mapping from node
   name to LNet host name using just prefixes and suffixes.  See the "lnet_host"
   attribute in the sample genders database above for an example.

   To continue with the previous example, a hosts database for its Mero
   cluster would have records like the following:
@verbatim
h00      192.168.1.0
h01      192.168.1.1
...
lh00     10.76.50.40
lh01     10.76.50.41
...
@endverbatim

   @subsection MGMT-DLD-lspec-state State Specification
   <i>Mandatory.
   This section describes any formal state models used by the component,
   whether externally exposed or purely internal.</i>

   @subsection MGMT-DLD-lspec-thread Threading and Concurrency Model

   - Service management must operate within the locality model of a
   request handler, and must honor the existing request handler and
   service object locks.

   @subsection MGMT-DLD-lspec-numa NUMA optimizations

   - An atomic variable is used to collectively track the number of active FOPs
   per @ref reqhservice "request handler service" within the service object
   itself, rather than on a per-locality-per-service object basis.  This is
   because FOP creation and finalization events are relatively rare compared
   to FOP scheduling operations.

   <hr>
   @section MGMT-DLD-conformance Conformance
   <i>Mandatory.
   This section cites each requirement in the @ref MGMT-DLD-req section,
   and explains briefly how the DLD meets the requirement.</i>

   <hr>
   @section MGMT-DLD-ut Unit Tests
   <i>Mandatory. This section describes the unit tests that will be designed.
   </i>

   <hr>
   @section MGMT-DLD-st System Tests
   <i>Mandatory.
   This section describes the system testing done, if applicable.</i>

   <hr>
   @section MGMT-DLD-O Analysis
   <i>This section estimates the performance of the component, in terms of
   resource (memory, processor, locks, messages, etc.) consumption,
   ideally described in big-O notation.</i>

   <hr>
   @section MGMT-DLD-ref References

   - @anchor MGMT-DLD-ref-svc-plan [0] <a href="https://docs.google.com/a/
xyratex.com/document/d/10VtuJSH8gcMNjaS7wEvgDbn2v1vS7m2Czgi8nb857sQ/view">
Mero Service Interface Planning</a>
   - @anchor MGMT-DLD-ref-mw-prod-plan [1] <a href="https://docs.google.com/a/
xyratex.com/document/d/1OTmELk-rsABDONlsXCIFrR8q5NVlVlpQkiKEzgP0hl8/view">
Mero-WOMO Productization Planning</a>
   - Manual pages:
   @anchor hosts5 <a href="http://linux.die.net/man/5/hosts">hosts(5)</a>,
   @anchor libgenders3 <a href="http://linux.die.net/man/3/libgenders">
   libgenders(3)</a>,
   @anchor nodeattr1 <a href="http://linux.die.net/man/1/nodeattr">
   nodeattr(1)</a>,
   @anchor pdcp1 <a href="http://linux.die.net/man/1/pdcp">pdcp(1)</a>,
   @anchor pdsh1 <a href="http://linux.die.net/man/1/pdsh">pdsh(1)</a>,
   @anchor service8 <a href="http://linux.die.net/man/8/service">service(8)</a>

   <hr>
   @section MGMT-DLD-impl-plan Implementation Plan

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
