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
   - @ref io_calls_params_dld-lspec
      - @ref io_calls_params_dld-lspec-comps
      - @ref io_calls_params_dld-lspec-state
      - @ref io_calls_params_dld-lspec-thread
      - @ref io_calls_params_dld-lspec-numa
   - @ref io_calls_params_dld-conformance
   - @ref io_calls_params_dld-ut
   - @ref io_calls_params_dld-st
   - @ref io_calls_params_dld-O
   - @ref io_calls_params_dld-ref
   - @ref io_calls_params_dld-impl-plan

   <hr>
   @section io_calls_params_dld-ovw Overview
   Normal I/O requests from clients are handled by I/O services, and replies are
   sent back to clients. The replies contain the results of I/O operations. For
   a read request, the reply contains the result data and its length, or error
   code. For a write request, the reply contains the written data length, or
   error code.

   If a device is damaged, i/o requests on that device should be handled in a
   special way:
   - reply an error code. When client gets the reply, it may read from parity
     and other data units to re-construct the data.
   - reply a new location. If the data has been re-covered by SNS repair to
     its spare space, this new location is replied. Client will contact with
     that device.

   I/O calls parameters handle the latter case.

   <hr>
   @section io_calls_params_dld-def Definitions
   N/A

   <hr>
   @section io_calls_params_dld-req Requirements
   The followsing requirements should be meet:
   - @b R.DLD.Device_Failure A specific error code should be replied to client
     when a device is damaged and no data can be read from it. Upon recieving
     this, the client will not try to read data from or write data to this
     device.
   - @b R.DLD.Device_New_Location A specific error code should be replied to
     client when a device is damaged and data has been recovered by SNS repair.
     New location will be replied in the same reply. Client will use this new
     location to fetch data or store data.

   <hr>
   @section io_calls_params_dld-depends Dependencies
   - Layout.
   - SNS.

   <hr>
   @section io_calls_params_dld-lspec Logical Specification

   - @ref io_calls_params_dld-lspec-comps
   - @ref io_calls_params_dld-lspec-state
   - @ref io_calls_params_dld-lspec-thread
   - @ref io_calls_params_dld-lspec-numa

   @subsection io_calls_params_dld-lspec-comps Component Overview
   Pool machine maintains the node and device status. Nodes/devices have four
   states: ONLINE, OFFLINE, FAILED, RECOVERING. These status of nodes and
   devices are replicated in a pool.
   When a i/o request target device is not in ONLINE status, a special error
   code will be returned along with the reply. More information may be carried
   along with the reply.
   - If the device is OFFLINE, a special error code will be returned to client.
     If this is a read request, client will read data and parity unit to
     re-construct the data. This is de-graded read. If this is write or create
     or delete request, client will fail.
   - If the device is FAILED, a special error node will be returned to client.
     Client will take similar actions to OFFLINE status, but SNS repair will be
     triggered.
   - If the device is RECOVERING, the result depends on SNS repair progress and
     the SNS policy. Let's assume SNS will repair data sequentially from lower
     objects numbers to higher objects numbers and, no out-of-order repair.
     If this is a read request,
	- if data is not recovered, an error code to instruct client to do
          degraded read is returned.
        - if data is already recovered, request will be handled in its normal
          way.
     If this is a write request,
        - if it is write to recovered area, or to a new object, request will
          be handled normally.
        - if it is write to a not-recovered area, a special error node will
          be returned to client (EAGAIN) and client will try later again.
     If this is create request,
        - handle this normally.
     If this is delete request,
        - if the target object exists (recovered), handle this normally.
        - if the target object does not exist (to-be-recovered), a special
          error code will be returned (EAGAIN) and client will try later
          again.

   @subsection io_calls_params_dld-lspec-state State Specification
   N/A

   @subsection io_calls_params_dld-lspec-thread Threading and Concurrency Model
   N/A

   @subsection io_calls_params_dld-lspec-numa NUMA optimizations
   N/A

   <hr>
   @section io_calls_params_dld-conformance Conformance
   - @b I.DLD.Device_FAILURE As we described in the component overview (@ref
        io_calls_params_dld-lspec-comps), error code will be returned to client,
        either because device is failed, or is offline, or in recovering and
        data is not ready. Client will parse the error code and take proper
        actions.
   - @b I.DLD.Device_New_Location If new spare space is already set up by
     SNS repair, error code along with new device location will be returned.
     Client will parse the reply and contact the new device.

   <hr>
   @section io_calls_params_dld-ut Unit Tests
   For read/write/create/delete request, unit tests will cover the following cases:
   - Device is OFFLINE
   - Device is FAILED
   - Device is RECOVERING
	- target object is recovered.
	- target object is not recovered.

   <hr>
   @section io_calls_params_dld-st System Tests
   Clients perform regular file system operations, meanwhile the I/O servers
   have some device failed, offline, SNS repair, etc. operations. Clients
   should be able to continue operations without error.

   <hr>
   @section io_calls_params_dld-O Analysis
   If new location is replied to client, client will contact the new device.
   This is an extra request. But in a normal system load, the performance
   should be almost the same as normal.

   <hr>
   @section io_calls_params_dld-ref References
   - <a href="https://docs.google.com/a/xyratex.com/document/d/1Yz25F3GjgQVXzvM1sdlGQvVDSUu-v7FhdUvFhiY_vwM/edit#heading=h.650bad0e414a"> HLD of SNS repair <a/>
   - @ref cm
   - @ref agents

 */

/**
   @addtogroup io_calls_params_dld
   @{
 */

/** @} */ /* end-of-io_calls_params_dld */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
