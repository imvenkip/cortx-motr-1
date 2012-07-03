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
 * Original author: Huang Hua <Hua_Huang@xyratex.com>
 * Original creation date: 18/06/2011
 */

/**
   @page io_calls_params_dld DLD of I/O calls Parameters Server-side

   - @ref io_calls_params_dld-ovw
   - @ref io_calls_params_dld-def
   - @ref io_calls_params_dld-req
   - @ref io_calls_params_dld-depends
   - @subpage io_calls_params_dld-fspec
   - @ref io_calls_params_dld-lspec
      - @ref io_calls_params_dld-lspec-comps
      - @ref io_calls_params_dld-lspec-ds
   - @ref io_calls_params_dld-conformance
   - @ref io_calls_params_dld-ut
   - @ref io_calls_params_dld-st
   - @ref io_calls_params_dld-O
   - @ref io_calls_params_dld-ref

   <hr>
   @section io_calls_params_dld-ovw Overview
   Read or write of a file stored according to a parity de-clustered layout
   involves operations on component objects stored on a potentially large
   number of devices attached to multiple network nodes (together comprising a
   storage pool). Devices and nodes can be unavailable for various reasons,
   including:
     - hardware failure,
     - network partitioning,
     - administrative decision,
     - software failure.

   To maintain desired levels of availability, IO must continue when some
   elements (devices or nodes) are unavailable. Parity de-clustering layout
   provides necessary redundancy to tolerate failures. This module provides
   data-structures and interfaces to report changes in pool availability
   characteristics to clients. Clients use this information to guide transitions
   between various IO regimes:
     - normal, when all devices and nodes in the pool are available,
     - degraded, when a client uses redundancy ("parity") to continue IO in the
       face of unavailability,
     - non-blocking availability (NBA), when a client redirects write operations
       to a different pool.
   The decision to switch to either degraded or NBA regime is made by a policy
   outside of this module.

   The key data-structure describing availability characteristics of a pool is
   its "failure vector", which is a collection of devices and nodes states,
   the states transitions history. Failure vector is maintained by pool manager
   and updated through distributed consensus algorithm, so that all non-faulty
   services in the pool have the same idea about the failure vector. The failure
   vector is shared and used by io services, SNS repair copy machines or other
   components.

   Clients receive failure vector together with the file layout from metadata
   services, and additionally as through unsolicited failure vector change
   notifications, sent by servers. A client uses failure vector to guide its
   IO requests. Failure vector has a version number, incremented on each vector
   update which leads to devices or nodes state transitions. Each client IO
   request is tagged with the version of failure vector used to guide this
   request. When ioservice receives an IO request with a stale failure vector
   version, the request is denied. Client has to pro-actively update its failure
   vector from ioservice.

   <hr>
   @section io_calls_params_dld-def Definitions
   - Failure vector. This is a data structures to keep track of device and node
     availability characteristics in a pool. Device/node join and leave will
     generate events to update the failure vector.

   - Failure vector version number. Failure vector is changing due to device
     and/or node join/leave. Failure vector is cached by clients, md services
     and other components. To keep the failure vector coherent in these
     components, a version number is introduced to failure vector.

   - Failure vector update event. This is a device or node event which will
     cause failure vector transits its internal state.

   <hr>
   @section io_calls_params_dld-req Requirements
   The following requirements should be meet:
   - @b R.DLD.FV Failure vector is stored in persistent storage. It can be
                 shared in multiple components.
   - @b R.DLD.FV_Update Failure vector can be updated by device/node join
                 or leave or other event.
   - @b R.DLD.FV_Query Failure vector can be queried, by whole, or for a
                 specified region marked by version number.
   - @b R.DLD.FV_Notification Failure vector update will generate un-solicited
                 notification to other services and components, like mdservice,
                 client, or SNS repair copy machine.
   - @b R.DLD.FV_version Failure vector version number is embedded to client
                 I/O request.
   - @b R.DLD.FV_Fetch Failure vector can be fectched to client or mdservice
                 or other services in reply message. This can be the whole
                 failure vector, or the recent changes between specific version.

   <hr>
   @section io_calls_params_dld-depends Dependencies
   - Layout.
   - SNS.

   <hr>
   @section io_calls_params_dld-lspec Logical Specification

   - @ref io_calls_params_dld-lspec-comps
   - @ref io_calls_params_dld-lspec-ds
   - @ref io_calls_params_dld-lspec-if

   @subsection io_calls_params_dld-lspec-comps Component Overview
   All I/O requests (including read, write, create, delete, ...) will to extended
   to embed its known failure vector version numbers.

   I/O replies are extended to  to embed failure vector updates (not the whole
   failure vector,  but the delta between the last known to current version) to
   client.

   A unsolicited notification fop is introduced. This fop will be sent to client
   by server. This fop contains the latest failure vector version number. When
   clients or other services get this notification, they should fetch the failure
   vector updates.

   @subsection io_calls_params_dld-lspec-ds Data Structures
   The data structures of failure vector, failure vector version number are in
   @ref poolmach module.

   I/O request fop will be extended to embed the its known failure vector
   version number. I/O replies will embed failure vector updates. Please refer
   to the io_fops.ff file for detailed design.

   Failure vector will be stored in reqh as a shared key. c2_reqh_key_init(),
   c2_reqh_key_find() and c2_reqh_key_fini() will be used. By this, latest
   failure vectors and its version number information can be shared between
   multiple modules in the same request handler.

   @subsection io_calls_params_dld-lspec-if Interfaces
   The failure vector and version number operations are designed and listed
   in @ref poolmach.

   <hr>
   @section io_calls_params_dld-conformance Conformance
   - @b I.DLD.FV Failure vector is stored in persistent storage by pool manager.
		 That will be done later and is out of the scope of this module.
                 Failure vector can be shared in multiple components by manage it
                 as a reqh key.

   - @b I.DLD.FV_Update Failure vector can be updated by events. These events
                 can be device or node join, leave, or others.

   - @b I.DLD.FV_Query Failure vector can be queried, by whole, or by delta.
		 Pool machine provide query interfaces.

   - @b I.DLD.FV_Notification Upon failure vector updates, notification will
                 be sent unsolicitedly to related services. These notification
                 carry the latest version number. Recipients will fetch failure
                 vector update based on its known version number.

   - @b I.DLD.FV_version Failure vector version number is embedded to client
                 I/O request.

   - @b I.DLD.FV_Fetch Failure vector can be fetched to client or mdservice
                 or other services in reply message. This can be the whole
                 failure vector, or the recent changes between specific version.

   <hr>
   @section io_calls_params_dld-ut Unit Tests
   Unit tests will cover the following cases:
   - client requests are tagged with valid failure vector version number.
   - client requests with valid failure version number are handles by ioservice
     correctly.
   - client requests are denied (returned special error code, that is
     C2_IOP_ERROR_FAILURE_VECTOR_VERSION_MISMATCH) if their failure
     vector version number are mismatch with the server known failure vector
     version number. Failure vector version number updates are serialized into
     replies.
   - failure vector changed on server. An un-solicited notification is sent to
     relative services, including clients.

   <hr>
   @section io_calls_params_dld-st System Tests
   Clients perform regular file system operations, meanwhile the I/O servers
   have some device failed, off-line, SNS repair, etc. operations. Clients
   should be able to continue operations without error.

   <hr>
   @section io_calls_params_dld-O Analysis
   If new location is replied to client, client will contact the new device.
   This is an extra request. But in a normal system load, the performance
   should be almost the same as normal.

   <hr>
   @section io_calls_params_dld-ref References
   - <a href="https://docs.google.com/a/xyratex.com/document/d/1Yz25F3GjgQVXzvM1sdlGQvVDSUu-v7FhdUvFhiY_vwM/edit#heading=h.650bad0e414a"> HLD of SNS repair </a>
   - @ref cm
   - @ref agents
   - @ref poolmach

 */

/**
   @addtogroup io_calls_params_dldDFS
   @{
 */

/** @} */ /* end of io_calls_params_dldDFS */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
