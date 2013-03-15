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
   @page MGMT-DLD Management Interface Design

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
      - @ref MGMT-SVC-DLD "Management Service Design"
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
   - @subpage MGMT-SVC-DLD "Management Service Design"
   - @subpage MGMT-M0MC-DLD "The m0mc Command Design"

   <hr>
   @section MGMT-DLD-ovw Overview

   The Management module provides external interfaces with which to manage
   Mero.  The interfaces take the form of over-the-wire @ref fop "FOP"
   specifications that are exchanged with a management service, and
   command line utilities necessary for interaction with external
   subsystems such as the HA subsystem. @ref MGMT-DLD-ref-svc-plan "[0]".

   The Management module provides @i mechanisms by which Mero is configured
   and managed externally; it does not define external @i policy.
   This DLD and associated documents should aid in the development of
   middle-ware required to successfully deploy a Mero based product such as
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
   - Describe the run time environment in terms of the host names, addresses and
     network interfaces, host UUIDs, service configuration choices, run time
     locations, and common parameters required to define the cluster.
   - Reflect static configuration data in the Genders database.
   - Adopt a standard m0d deployment configuration.
      - Provide standard operating system "service" command support to start
        and stop m0d.
   - Add formal states to a request handler to handle graceful startup and
   shutdown.
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
   - @ref MGMT-DLD-lspec-svc "The management service"
   - @ref MGMT-M0MC-DLD "The management command (m0mc)"

   @subsection MGMT-DLD-lspec-osif Extensions for the service command
   Support will be provided to start and stop m0d with the "service" command.
   This is a commonly used operating system tool that manages subsystems.
   The functionality is provided by an extensible set of subsystem specific
   scripts located in the /etc/rc.d/init.d directory.

   A standard Mero deployment pattern is defined for this purpose:
   - The kernel module will use a well known portal number.  Its request
     handler will use a TMID of 0.
   - There will be only one m0d per node, with a well known portal number.
     - There will be only one request handler per m0d. The request handler will
     have only one network end point and will use a TMID of 0.
     - All services relevant to a node will be configured to run within this
     m0d process.
   - Use of the /etc/hosts and /etc/genders databases to capture static
     configuration data.  It is assumed that these files will have the same
     cluster related content on all nodes of the Mero cluster.
   - A standard directory in the file system, /etc/sysconfig/mero, is specified
     for additional configuration, including information on the disks to be used
     on the node (STOB data).  Some of this information will eventually be
     obtained from the configuration database at run time, but this arena allows
     us to stage in such advanced functionality.
   - A standard directory in the file system, /var/mero, is specified for
     run time generated data.
     - This will be the run time directory of the m0d process.  Core files
       should end up in this directory.
     - A subdirectory named "db" will be temporarily created and passed as
       the argument to the '-D' m0d flag.
     - The confd database will be in the /var/mero/confd directory on hosts that
       run the configuration database service.  It should not be under /etc
       because the size is proportional to both the number of physical and the
       number of logical resources.
     - Trace files could be placed in this directory.

   It is up to an external agency to set up all the required configuration
   to enable use of the "service" command interface.

   The following functionality will be available through the "service" command
   with the provision of the /etc/rc.d/init.d/mero script:
@code
# service mero start
# service mero status
# service mero stop
@endcode
   - The "start" directive will load the Mero kernel module and then start
   the m0d process.
   - The "stop" directive will stop the m0d process and unload the kernel
   module.
   - The "query" directive will use the m0mc command to query m0d and then
   return a summary status.

   See @ref service8 "service(8)" for more details.

   The script will be driven by data from the /etc/hosts and /etc/genders file,
   and information in the /etc/sysconfig/mero directory.

   @todo Define STOB data required under /etc/sysconfig/mero
   @todo Investigate other m0d flags such as '-p'.  This generalizes to handling
   one-time initialization issues.
   @todo Investigate other service startup needs.

   @subsection MGMT-DLD-lspec-genders Use of the /etc/genders file
   The @ref libgenders3 "/etc/genders" file is a common database used in
   management of large clusters.
   It is expected that this file will be created by an external agency and
   replicated on each participating host in the Mero cluster.

   The Mero management module stores relatively static configuration information
   for each node of the Mero cluster in this file.
   The data in this file allows us to configure Mero sub-components on a
   node without run time access to the Mero configuration database.
   The only time the data in this file changes is when nodes are added or
   removed from the Mero cluster.

   The information will include specification of at least the following for
   each node:
   - The node UUID.
   - LNet information for the node:
     - The LNet hostname and interface name to use on the node.
     - The portal numbers to use in the kernel, m0d and client commands
       on the node.
   - The Mero services that must run on the node.
     - Some services, like the IO service, are configured on all nodes.
     - Other services, like confd and mdservice, are only configued on a
       subset of nodes.
   - m0d parameters for individual Mero services on the node.
   - The location of Mero run time data on the node.

   Information in the genders file is supplemented with related hostname to IP
   address mapping in the /etc/hosts file.

   A genders file could look like this:
@verbatim
h[00-10]  all
h[00-10]  lnet_if=o2ib0,lnet_pid=12345   # LNet common defaults
h[00-10]  lnet_kernel_portal=34          # portal for the kernel
h[00-10]  lnet_m0d_portal=35             # portal for m0d
h[00-10]  lnet_client_portal=36          # portal for clients (dynamic TMID)
h[00-10]  lnet_host=l%n                  # mapping of nodename to LNet hostname
h[00-10]  var=/var/mero                  # Mero variable data directory
h[00-10]  max_rpc_msg=163840             # max rpc message size
h[00-10]  min_recv_q=2                   # minimum receive queue length
h00,h01   s_confd=-c:/var/mero/confd/confdb.txt # confd hosts, db file
h00,h01   s_rm                           # hosts running the resource manager
h00,h01   s_mdservice                    # hosts running the meta-data service
h[00-10]  s_addb=-A:/etc/sysconfig/mero/addb-stobs # ADDB service hosts; stobs
h[00-10]  s_ioservice=-T:AD:-S:/etc/sysconfig/mero/stobs # IO service; stobs
h[00-10]  s_sns                          # hosts running SNS
h00       HA-PROXY                       # hosts running HA proxies
h00       uuid=b47539c2-143e-44e8-9594-a8f6e09bfec0
h00       u_confd=d2655b68-f578-45cb-bbb9-c1495e083074
h01       uuid=6d5ddc53-b1b6-43ae-9c7c-16c227b2ea5a
h02       uuid=26a17da7-d5f2-462d-960d-205334adb028
h03       uuid=68b617e1-097a-4e46-8d16-3e202628c568
h03       u_ioservice=f595564a-20ca-4b12-8f4b-0d2f82726d61
@endverbatim
   Most of the attributes in the example above are self explanatory, but
   some need additional explanation:
   - @a uuid is the Node UUID.  This value is passed as a parameter to the Mero
   kernel module to uniquely identify the node.
   - @a s_Name denotes a service (type) @em Name that needs to be started
   on a node.
   The value of this attribute, if any, are a list of colon separated m0d
   arguments.
   - @a u_Name specifies the UUID of the specified service (type).
   Every service instance in the cluster must have a UUID - the example above
   only illustrates a couple of service uuids.  Communication with
   @ref MGMT-SVC-DLD-lspec-mgmt-foms "Management FOPs" uses service UUIDs.
   - @a lnet_host This attribute provides a mapping from a node name to the
   symbolic host name associated with the IP address to use for LNet on that
   node.
   - @a var is the location of the variable data directory where run time Mero
   data is stored.  This directory will be used as the "current" directory of
   the m0d process

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
var=/var/mero
max_rpc_msg=163840
min_recv_q=2
s_addb=-A:/etc/sysconfig/mero/addb-stobs
s_ioservice=-T:AD:-S:/etc/sysconfig/mero/stobs
s_sns
uuid=68b617e1-097a-4e46-8d16-3e202628c568
u_ioservice=f595564a-20ca-4b12-8f4b-0d2f82726d61

# nodeattr -c s_confd
h00,h01
@endverbatim
   Note that the "%n" token is automatically replaced by genders with the
   node name.
   See @ref nodeattr1 "nodeattr(1)" for more details.
   Common cluster support utilites like @ref pdsh1 "pdsh(1)" and
   @ref pdcp1 "pdcp(1)" can also be fed the output of this command.

   @subsection MGMT-DLD-lspec-hosts Use of the /etc/hosts file
   The @ref hosts5 "hosts(5)" file is a standard system database that
   provides host name to IP address mapping.
   It is expected that this file will be created by an external agency and
   replicated on each participating host in the Mero cluster so that they all
   have the same set of mappings.

   The host names for Mero cluster servers must follow the pattern
   defined for @ref libgenders3 "/etc/genders".  Genders refers to a
   cluster host name as its "node name".

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
   See individual component DLDs.

   @subsection MGMT-DLD-lspec-thread Threading and Concurrency Model
   See individual component DLDs.

   @subsection MGMT-DLD-lspec-numa NUMA optimizations
   See individual component DLDs.

   <hr>
   @section MGMT-DLD-conformance Conformance

   - @b I.reqh.mgmt-api.service.start
   - @b I.reqh.startup.synchronous
   - @b I.reqh.mgmt-api.service.stop
   - @b I.reqh.mgmt-api.service.query
   - @b I.reqh.mgmt-api.shutdown
   - @b I.reqh.mgmt-api.control
        See @ref MGMT-SVC-DLD "Management Service Design"

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


#undef M0_ADDB_RT_CREATE_DEFINITION
#undef M0_ADDB_CT_CREATE_DEFINITION
#define M0_ADDB_CT_CREATE_DEFINITION
#define M0_ADDB_RT_CREATE_DEFINITION
#include "mgmt/mgmt_addb.h"

#include "fop/fop.h"
#include "fop/fom.h"
#include "lib/errno.h"
#include "lib/memory.h"
#include "lib/misc.h"
#define M0_TRACE_SUBSYSTEM M0_TRACE_SUBSYS_MGMT
#include "lib/trace.h"  /* M0_LOG() */
#include "reqh/reqh.h"
#include "reqh/reqh_service.h"
#include "rpc/rpc_opcodes.h"
#include "mero/magic.h"

#include "mgmt/mgmt.h"
#include "mgmt/mgmt_pvt.h"
#include "mgmt/mgmt_fops_xc.h"
#include "mgmt/mgmt_fops.h"

/**
   @addtogroup mgmt_pvt
   @{
 */
#ifndef __KERNEL__
/**
   Define this macro to create the management service.
   @todo Should a management service be in the kernel?  It requires reqh.
 */
#define M0_MGMT_SERVICE_PRESENT
#endif /* !__KERNEL__ */

/** @} end group mgmt_pvt */

struct m0_addb_ctx m0_mgmt_addb_ctx;

/* Include C files to minimize symbol exposure */
#ifdef M0_MGMT_SERVICE_PRESENT
#include "mgmt/svc/mgmt_svc.c"
#endif
#include "mgmt/svc/fop_ssr.c"
#include "mgmt/svc/fop_ss.c"

/**
   @addtogroup mgmt
   @{
 */

M0_INTERNAL int m0_mgmt_init(void)
{
	int rc = 0;

	m0_xc_mgmt_fops_init();
	m0_addb_ctx_type_register(&m0_addb_ct_mgmt_mod);
	m0_addb_ctx_type_register(&m0_addb_ct_mgmt_service);
	M0_ADDB_CTX_INIT(&m0_addb_gmc, &m0_mgmt_addb_ctx, &m0_addb_ct_mgmt_mod,
			 &m0_addb_proc_ctx);

#ifdef M0_MGMT_SERVICE_PRESENT
	rc = mgmt_svc_init();
#endif
	rc = rc ?:
		mgmt_fop_ssr_init() ?:
		mgmt_fop_ss_init();
	return rc;
}

M0_INTERNAL void m0_mgmt_fini(void)
{
	mgmt_fop_ss_fini();
	mgmt_fop_ssr_fini();
#ifdef M0_MGMT_SERVICE_PRESENT
	mgmt_svc_fini();
#endif
	m0_addb_ctx_fini(&m0_mgmt_addb_ctx);
	m0_xc_mgmt_fops_fini();
}

M0_INTERNAL int m0_mgmt_service_allocate(struct m0_reqh_service **service)
{
#ifdef M0_MGMT_SERVICE_PRESENT
	return m0_reqh_service_allocate(service, &m0_mgmt_svc_type, NULL);
#else
	return -ENOSYS;
#endif
}

/** @} end of mgmt group */

#undef M0_TRACE_SUBSYSTEM

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
