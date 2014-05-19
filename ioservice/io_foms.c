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
 * Original author: Anand Vidwansa <Anand_Vidwansa@xyratex.com>
 *                  Anup Barve <Anup_Barve@xyratex.com>
 * Original creation date: 03/21/2011
 * Revision       : Rajanikant Chirmade <Rajanikant_Chirmade@xyratex.com>
 * Revision date  : 09/14/2011
 */

#define M0_TRACE_SUBSYSTEM M0_TRACE_SUBSYS_IOSERVICE
#include "lib/trace.h"
#include "lib/errno.h"
#include "lib/memory.h"
#include "lib/tlist.h"
#include "lib/assert.h"
#include "lib/misc.h"    /* M0_BITS */
#include "lib/finject.h"
#include "addb/addb.h"
#include "net/net_internal.h"
#include "net/buffer_pool.h"
#include "fop/fop.h"
#include "fop/fom_generic.h"
#include "stob/stob.h"
#include "stob/type.h"		/* m0_stob_type_id_by_name */
#include "stob/domain.h"	/* m0_stob_domain__dom_id */
#include "fid/fid.h"
#include "reqh/reqh_service.h"
#include "ioservice/io_foms.h"
#include "ioservice/io_service.h"
#include "ioservice/io_device.h"
#include "mero/magic.h"
#include "mero/setup.h"
#include "pool/pool.h"
#include "ioservice/io_service_addb.h"
#include "sns/cm/cm.h" /* m0_sns_cm_fid_repair_done() */
#include "module/instance.h"	/* m0_get() */

/**
   @page DLD-bulk-server DLD of Bulk Server

   - @ref DLD-bulk-server-ovw
   - @ref DLD-bulk-server-def
   - @ref DLD-bulk-server-req
   - @ref DLD-bulk-server-design-ovw
   - @subpage DLD-bulk-server-fspec  <!-- Note @subpage -->
   - @ref DLD-bulk-server-lspec
      - @ref DLD-bulk-server-lspec-seq
      - @ref DLD-bulk-server-lspec-state
      - @ref DLD-bulk-server-lspec-buffers-mgnt
      - @ref DLD-bulk-server-lspec-service-registration
      - @ref DLD-bulk-server-lspec-thread
      - @ref DLD-bulk-server-lspec-numa
      - @ref DLD-bulk-server-lspec-depends
      - @ref DLD-bulk-server-lspec-conformance
   - @ref DLD-bulk-server-ut
   - @ref DLD-bulk-server-it
   - @ref DLD-bulk-server-st
   - @ref DLD-bulk-server-O
   - @ref DLD-bulk-server-ref

   <hr>
   @section DLD-bulk-server-ovw Overview
   This document contains the detailed level design of the Bulk I/O Service.

   <b>Purpose</b><br>
   The purpose of this document is to:
   - Refine higher level designs
   - To be verified by inspectors and architects
   - To guide the coding phase

   <hr>
   @section DLD-bulk-server-def Definitions
   Terms used in this document defined as below :

   - <b>Bulk I/O Service</b> Mero ioservice which process read/write FOPs.
   - <b>FOP</b> File operation packet, a description of file operation
     suitable for sending over network or storing on a storage device.
     File operation packet (FOP) identifies file operation type and operation
     parameters.
   - <b>FOM</b> FOP state machine (FOM) is a state machine that represents
     current state of the FOP's execution on a node. FOM is associated with
     the particular FOP and implicitly includes this FOP as part of its state.
   - <b>zero-copy</b> Copy between a source and destination takes place without
     any intermediate copies to staging areas.
   - <b>STOB</b> Storage object (STOB) is a basic M0 data structure containing
     raw data.
   - <b>COB</b> Component object (COB) is a component (stripe) of a file,
     referencing a single storage object and containing metadata describing the
     object.
   - <b>rpc_bulk</b> Generic interface for zero-copy.
   - <b>buffer_pool</b> Pre-allocated & pre-registered pool of buffers. Buffer
     pool also provides interfaces to get/put buffers. Every Bulk I/O Service
     initiates its buffer_pool.
   - <b>Configuration cache</b> Configuration data being stored in node’s
     memory.

   <hr>
   @section DLD-bulk-server-req Requirements
   - <b>r.bulkserver.async</b> Bulk I/O server runs asynchronously.
   - <b>r.non-blocking.few-threads</b> Mero service should use a relatively
     small number of threads: a few per processor.
   - <b>r.non-blocking.easy</b> Non-blocking infrastructure should be easy to
     use and non-intrusive.
   - <b>r.non-blocking.extensibility</b> Addition of new "cross-cut"
     functionality (e.g., logging, reporting) potentially including blocking
     points and affecting multiple fop types should not require extensive
     changes to the data-structures for each fop type involved.
   - <b>r.non-blocking.network</b> Network communication must not block handler
     threads.
   - <b>r.non-blocking.storage</b> Storage transfers must not block handler
     threads.
   - <b>r.non-blocking.resources</b> Resource acquisition and release must not
     block handler threads.
   - <b>r.non-blocking.other-block</b> Other potentially blocking conditions
     (page faults, memory allocations, writing trace records, etc.) must never
     block all service threads.

   <hr>
   @subsection DLD-bulk-server-design-ovw Design Overview

   Bulk I/O Service will be available in the form of state machine to process
   bulk I/O request. It uses generic rpc_bulk interface to use zero-copy RDMA
   mechanism from transport layer to copy data from source to destination.
   It also use STOB I/O interface to complete the I/O operation.

   Bulk I/O Service implements I/O FOMs to process I/O FOPs @ref io_foms.

   - Bulk read FOM process FOP of type m0_fop_cob_readv
   - Bulk write FOM process FOP of type m0_fop_cob_writev

   The Bulk I/O Service interface m0_ioservice_fop_init() registers and
   initiates I/O FOPs with it. Following are the Bulk I/O Service FOP type.

   - m0_fop_cob_readv,
   - m0_fop_cob_writev,
   - m0_fop_cob_readv_rep,
   - m0_fop_cob_writev_rep

   The Bulk I/O Service initiates buffer_pool during its initialization.
   Bulk I/O Service gets buffers required from buffer_pool and pass it to
   rpc_bulk for zero-copy. Buffers then returns back to buffer pool after
   data written on STOB.

   The Bulk I/O Service initialization done by request handler during its
   startup.

   <hr>
   @section DLD-bulk-server-lspec Logical Specification

   @subsection DLD-bulk-server-lspec-seq Sequence diagram
   This section describes how client and server communications happens while
   processing read/write FOPs. This also shows usage of zero-copy in I/O
   FOP processing.

   <b> Write operation with zero-copy data transfer </b>
   @msc
   wordwraparcs="1", hscale="1.5";
   a [ label = Bulk_Client ],
   at[ label = Bulk_Client_Transport ],
   b [ label = Bulk_Server_Transport ],
   c [ label = Bulk_IO_Service, linecolor="#0000ff", textcolor="#0000ff"],
   d [ label = STOB_IO ];

   a->c  [ label = "Write FOP (m0_net_bufs_desc list, indexvecs list)"];
   c=>b  [ label = "Get network buffer from buffer_pool"];
   b>>c  [ label = "Got network buffer (m0_net_bufs)"];
   c=>b  [ label = "Initiates zero-copy (m0_net_bufs, m0_net_bufs_desc)" ];
   at->b [ label = "Data transfer using RDMA" ];
   at->b [ label = "Data transfer using RDMA" ];
   ...;
   at->b [ label = "Data transfer using RDMA" ];
   b=>>c [ label = "Zero-copy finish"];
   c=>d  [ label = "Initiates STOB I/O" ];
   d=>>c [ label = "STOB I/O completes"];
   c=>b  [ label = "Return back buffer to buffer_pool (m0_net_bufs)"];
   b>>c  [ label = "Network buffers released"];
   c->a  [ label = "Reply FOP (status)" ];

   @endmsc

   - Client sends write FOP to server. Write FOP contains the network buffer
     descriptor list and indexvecs list instead of actual data.

   - To process write FOP, request handler creates & initiates write FOM and
     puts it into run queue for execution.

   - State transition function go through generic and extended phases
     @ref m0_io_fom_cob_rw_phases defined for I/O FOM (write FOM).
         - Gets as many buffers as it can from buffer_pool to transfer
           data for all descriptors. If there are insufficient buffers
           with buffer_pool to process all descriptors then its goes by
           batch by batch. At least one buffer is needed to start bulk
           transfer. If no buffer available then bulk I/O Service will
           wait till buffer_pool becomes non-empty.
         - Initiates zero-copy using rpc_bulk on acquired buffers and
           wait for zero-copy to complete for all descriptors on which
           it initiated.
         - Zero-copy completes
         - Initiates write data on STOB for all indexvec and wait for
           STOB I/O to complete
         - STOB I/O completes
         - Returns back some of buffers to buffer_pool if they are more
           than remaining descriptors.

   - Enqueue response in fo_rep_fop for the request handler to send the response
     back to the client

   <b> Read operation with zero-copy data transfer </b>
   @msc
   wordwraparcs="1", hscale="1.5";
   a [ label = Bulk_Client ],
   at[ label = Bulk_Client_Transport ],
   b [ label = Bulk_Server_Transport ],
   c [ label = Bulk_IO_Service, linecolor="#0000ff", textcolor="#0000ff"],
   d [ label = STOB_IO ];

   a->c  [ label = "Read FOP (m0_net_bufs_desc)"];
   c=>b  [ label = "Get network buffer from buffer_pool"];
   b>>c  [ label = "Got network buffer (m0_net_bufs)"];
   c=>d  [ label = "Initiates STOB I/O" ];
   d=>>c [ label = "STOB I/O completes"];
   c=>b  [ label = "Initiates zero-copy (m0_net_bufs, m0_net_bufs_desc)" ];
   b->at [ label = "Data transfer using RDMA" ];
   b->at [ label = "Data transfer using RDMA" ];
   ...;
   b->at [ label = "Data transfer using RDMA" ];
   b=>>c [ label = "Zero-copy finish"];
   c=>b  [ label = "Returns back network buffer  (m0_net_bufs)"];
   b>>c  [ label = "Network buffer released"];
   c->a  [ label = "Reply FOP (status)" ];

   @endmsc

   - Client sends read FOP to server. Read FOP contains the network buffer
     descriptor list and indexvecs list instead of actual data.
   - To process read FOP, request handler creates & initiates read FOM and puts
     it into run queue for execution.
   - State transition function go through generic and extended phases
     @ref m0_io_fom_readv_phases defined for read FOM.
         - Gets as many buffers as it can from buffer_pool to transfer
           data for all descriptors. If there are insufficient buffers
           with buffer_pool to process all descriptors then its goes by
           batch by batch. At least one buffer is needed to start bulk
           transfer. If no buffer available then bulk I/O Service will
           wait till buffer_pool becomes non-empty.
         - Initiates read data from STOB for all indexvecs and wait for
           STOB I/O to completes
         - STOB I/O completes
         - Initiates zero-copy using rpc_bulk on acquired buffers and
           wait for zero-copy to complete for all descriptors on which
           it initiated.
         - Zero-copy completes
         - Returns back some of buffers to buffer_pool if they are more
           than remaining descriptors.
   - Enqueue response in fo_rep_fop for the request handler to send the response
     back to the client

   On the basis of steps involved in these operations enumeration called @ref
   m0_io_fom_cob_rw_phases will be defined, that extends the standard FOM phases
   (enum m0_fom_standard_phase) with new phases to handle the state machine that
   sets up and executes read/write operations respectively involving bulk I/O.

   <hr>
   @subsection DLD-bulk-server-lspec-state State Transition Diagrams

   State Diagram For Write FOM :
   @dot
   digraph example {
       size = "5,10"
       node [shape=record, fontsize=10]
       S0 [label="Bulk I/O Service Init"]
       S1 [label="Initialize Write FOM"]
       S2 [label="Get buffers from buffer_pool"]
       S3 [label="Wait for buffer from buffer_pool"]
       S4 [label="Initiates zero-copy"]
       S5 [label="Wait for zero-copy to complete"]
       S6 [label="STOB I/O"]
       S7 [label="Wait for STOB I/O to complete"]
       S8 [label="Release network buffer"]
       S9 [label="Send reply FOP to client"]
       S0 -> S1 [label="Got Write FOP to process"]
       S1 -> S2 [label="Start processing Write FOP"]
       S2 -> S4 [label="Got Buffer"]
       S2 -> S3 [label="Buffer is not available"]
       S3 -> S3 [label="Buffer is not available"]
       S3 -> S4 [label="Got Buffer"]
       S4 -> S5 [label="Initiates request"]
       S5 -> S6 [label="zero-copy complete"]
       S6 -> S7 [label="launch I/O request"]
       S7 -> S8 [label="STOB I/O complete"]
       S8 -> S2 [label="process remaing request"]
       S8 -> S9 [label="Buffers returned back"]
   }
   @enddot

   Bulk I/O Service FOMs will be placed in wait queue for all states which
   needs to wait for task complete.

   State Diagram For Read FOM :
   @dot
   digraph example {
       size = "5,10"
       node [shape=record, fontsize=10]
       S0 [label="Bulk I/O Service Init"]
       S1 [label="Initialize Read FOM"]
       S2 [label="Get buffers from buffer_pool"]
       S3 [label="Wait for buffer from buffer_pool"]
       S4 [label="STOB I/O"]
       S5 [label="Wait for STOB I/O to complete"]
       S6 [label="Initiates zero-copy"]
       S7 [label="Wait for zero-copy to complete"]
       S8 [label="Release network buffer"]
       S9 [label="Send reply FOP to client"]
       S0 -> S1 [label="Got Read FOP to process"]
       S1 -> S2 [label="Start processing Read FOP"]
       S2 -> S4 [label="Got buffer"]
       S2 -> S3 [label="Buffer is not available"]
       S3 -> S3 [label="Buffer is not available"]
       S3 -> S4 [label="Got buffer"]
       S4 -> S5 [label="launch I/O request"]
       S5 -> S6 [label="STOB I/O complete"]
       S6 -> S7 [label="Initiates request"]
       S7 -> S8 [label="zero-copy complete"]
       S8 -> S2 [label= "process remaining request"]
       S8 -> S9 [label="Buffer returned back"]
   }
   @enddot

   Bulk I/O Service FOMs will be placed in wait queue for all states which
   needs to wait for task complete.

   @subsection DLD-bulk-server-lspec-buffers-mgnt Buffers Management

   - Buffers Initialization & De-allocation :

   I/O service maintains m0_buf_pool instance with data structure
   m0_reqh_service. Buffer pool m0_reqh_service::m0_buf_pool will be initialized
   in Bulk I/O Service start operation vector m0_io_service_start(). Bulk I/O
   service will use m0_buf_pool_init() to allocate and register specified
   number of network buffers and with specified size.

   Bulk I/O Service needs following parameters from configuration database to
   initialize buffer pool -

   IO_BULK_BUFFER_POOL_SIZE Number of network buffers in buffer pool.
   IO_BULK_BUFFER_SIZE Size of each network buffer.
   IO_BULK_BUFFER_NUM_SEGMENTS Number of segments in each buffer.

   Buffer pool de-allocation takes place in service operation vector
   m0_io_service_stop(). I/O service will use m0_buf_pool_fini() to de-allocate
   & de-register the network buffers.

   The buffer pool for bulk data transfer is private to the Bulk I/O service and
   is shared by all FOM instances executed by the service.

   - Buffer Acquire

   Bulk I/O Servers acquire the network buffer by calling buffer_pool interface
   m0_buf_pool_get(). If buffer available with buffer_pool then this function
   returns network buffer. And if buffer_pool empty the function returns NULL.
   Then FOM need to wait for _notEmpty signal from buffer_pool.

   Bulk I/O Service needs to get lock on buffer_pool instance while its request
   network buffer. And release lock after it get network buffer.

   - Buffer Release

   Bulk I/O Servers release the network buffer by calling buffer_pool interface
   m0_buf_pool_put(). It return back network buffer to  buffer_pool.

   Bulk I/O Service needs to get lock on buffer_pool instance while it request
   network buffer. And release lock after it get network buffer.

   - Buffer Pool Expansion
   @todo
   If buffer_pool reached to low threshold, Bulk I/O service may expand pool
   size. This can be done later to minimize waiting time for network buffer.

   @subsection DLD-bulk-server-lspec-service-registration Service Registration

   - Service Type Declaration

   Bulk I/O Service defines service type as follows -

   M0_REQH_SERVICE_TYPE_DEFINE(m0_io_service_type, &m0_io_service_type_ops,
                                "ioservice", io_service_addb_ct_type, 2);

   It also assigns service name and service type operations for Bulk I/O
   Service.

   - Service Type Registration

   Bulk I/O Service registers its service type with request handler using
   interface m0_reqh_service_type_register(). This function registers service
   type with global service type list for request handler. Service type
   operation m0_ioservice_alloc_and_init() will do this registration.

   @subsection DLD-bulk-server-lspec-thread Threading and Concurrency Model

   - resources<br>
     It uses pre-allocated and pre-registered network buffers. These buffers
     will not released until zero-copy completes and data from net buffers
     transfered to/from STOB. Since these buffers are pre-allocated
     & pre-registered with transport layer there should be some lock on these
     buffers so that no one can use same buffers.

   @subsection DLD-bulk-server-lspec-numa NUMA optimizations

   @subsection DLD-bulk-server-lspec-depends Dependencies
   - <b>r.reqh</b> : Request handler to execute Bulk I/O Service FOM
   - <b>r.bufferpool</b> : Network buffers for zero-copy
   - <b>r.fop</b> : To send bulk I/O operation request to server
   - <b>r.net.rdma</b> : Zero-copy data mechanism at network layer
   - <b>r.stob.read-write</b> : STOB I/O
   - <b>r.rpc_bulk</b> : For using zero-copy mechanism
   - <b>r.configuration.caching</b> : Configuration data being stored in node's
     memory.

   @subsection DLD-bulk-server-lspec-conformance Conformance
   - <b>i.bulkserver.async</b>  It implements state transition interface so
     to run I/O bulk service asynchronously.

   <hr>
   @section DLD-bulk-server-ut Unit Tests

   For isolated unit tests, each function implemented as part of Bulk I/O
   Service needs to test separately without communicating with other modules.
   This is not required to use anther modules which are communicating with
   Bulk I/O Server modules.

   - Test 01 : Call function m0_io_fom_cob_rw_create()<br>
               Input           : Read FOP (in-memory data structure m0_fop)<br>
               Expected Output : Create FOM of corresponding FOP type.

   - Test 02 : Call function m0_io_fom_cob_rw_create()<br>
               Input           : Write FOP (in-memory data structure m0_fop)<br>
               Expected Output : Create FOM of corresponding FOP type.

   - Test 03 : Call function m0_io_fom_cob_rw_init()<br>
               Input           : Read FOP (in-memory data structure m0_fop)<br>
               Expected Output : Initiates FOM with corresponding operation
                                 vectors and other pointers.
   - Test 04 : Call function m0_io_fom_cob_rw_init()<br>
               Input           : Write FOP (in-memory data structure m0_fop)<br>
               Expected Output : Initiates FOM with corresponding operation
                                 vectors and other pointers.

   - Test 05 : Call m0_io_fom_cob_rw_tick() with buffer pool size 1<br>
               Input : Read FOM with current phase
                       M0_FOPH_IO_FOM_BUFFER_ACQUIRE<br>
               Expected Output : Gets network buffer and pointer set into FOM
                                 with phase changed to M0_FOPH_IO_STOB_INIT and
                                 return value M0_FSO_AGAIN.

   - Test 06 : Call m0_io_fom_cob_rw_tick() with buffer pool size 0
               (empty buffer_pool)<br>
               Input : Read FOM with current phase
                       M0_FOPH_IO_FOM_BUFFER_ACQUIRE<br>
               Expected Output : Should not gets network buffer and NULL pointer
                                 set into FOM with phase changed to
                                 M0_FOPH_IO_FOM_BUFFER_WAIT and return
                                 value M0_FSO_WAIT.

   - Test 07 : Call m0_io_fom_cob_rw_tick() with buffer pool size 0
               (empty buffer_pool)<br>
               Input : Read FOM with current phase
                       M0_FOPH_IO_FOM_BUFFER_WAIT<br>
               Expected Output : Should not gets network buffer and NULL pointer
                                 set into FOM with phase not changed and return
                                 value M0_FSO_WAIT.

   - Test 08 : Call m0_io_fom_cob_rw_tick()<br>
               Input : Read FOM with current phase M0_FOPH_IO_STOB_INIT<br>
               Expected Output : Initiates STOB read with phase changed to
                                 M0_FOPH_IO_STOB_WAIT and return value
                                 M0_FSO_WAIT.

   - Test 09 : Call m0_io_fom_cob_rw_tick()<br>
               Input : Read FOM with current phase M0_FOPH_IO_ZERO_COPY_INIT<br>
               Expected Output : Initiates zero-copy with phase changed to
                                 M0_FOPH_IO_ZERO_COPY_WAIT return value
                                 M0_FSO_WAIT.

   - Test 10 : Call m0_io_fom_cob_rw_tick() with buffer pool size 1<br>
               Input : Write FOM with current phase
                       M0_FOPH_IO_FOM_BUFFER_ACQUIRE<br>
               Expected Output : Gets network buffer and pointer set into FOM
                                 with phase changed to
                                 M0_FOPH_IO_ZERO_COPY_INIT and return value
                                 M0_FSO_AGAIN.

   - Test 11 : Call function m0_io_fom_cob_rw_fini()<br>
               Input : Read FOM<br>
               Expected Output : Should de-allocate FOM.

   - Test 12 : Call m0_io_fom_cob_rw_tick()<br>
               Input : Read FOM with invalid STOB id and current phase
                       M0_FOPH_IO_STOB_INIT.<br>
               Expected Output : Should return error.

   - Test 13 : Call m0_io_fom_cob_rw_tick()<br>
               Input : Read FOM with current phase M0_FOPH_IO_ZERO_COPY_INIT
                       and wrong network buffer descriptor.<br>
               Expected Output : Should return error.

   - Test 14 : Call m0_io_fom_cob_rw_tick()<br>
               Input : Read FOM with current phase M0_FOPH_IO_STOB_WAIT with
                       result code of stob I/O m0_fom::m0_stob_io::si_rc set
                       to I/O error.<br>
               Expected Output : Should return error M0_FOS_FAILURE and I/O
                                 error set in relay FOP.

   <hr>
   @section DLD-bulk-server-it Integration Tests

   All the tests mentioned in Unit test section will be implemented with actual
   bulk I/O client.

   @section DLD-bulk-server-st System Tests

   All the tests mentioned in unit test section will be implemented with actual
   I/O (read, write) system calls.

   <hr>
   @section DLD-bulk-server-O Analysis
   - Acquiring network buffers for zero-copy need to be implemented as async
     operation, otherwise each I/O FOM try to acquire this resource resulting
     lots of request handler threads if buffers is not available.
   - Use of pre-allocated & pre-registered buffers could decrease I/O throughput
     since all I/O FOPs need this resource to process operation.
   - On other side usage of zero-copy improve the I/O performance.

   <hr>
   @section DLD-bulk-server-ref References
   References to other documents are essential.
   - @ref io_foms
   - <a href="https://docs.google.com/a/xyratex.com/document/d/
1-nGIrcQL9XYvvFcYRKqjN6SnwGcqx1ZwcgkBIhoAdJk/edit?hl=en_US">
   FOPFOM Programming Guide</a>
   - <a href="https://docs.google.com/a/xyratex.com/document/d/
1LjL0Ky6mCxxAgRSX6DIe7UMdt1CrFsSWG_2twBy5kI8/edit?hl=en_US">
   High Level Design - FOP State Machine</a>
   - <a href="https://docs.google.com/a/xyratex.com/Doc?docid=
0AQaCw6YRYSVSZGZmMzV6NzJfMTljbTZ3anhjbg&hl=en_US">
   High level design of rpc layer core</a>
 */

/**
 * @addtogroup io_foms
 * @{
 */
#define IOSERVICE_NAME "ioservice"

M0_TL_DESCR_DEFINE(stobio, "STOB I/O", static, struct m0_stob_io_desc,
		   siod_linkage,  siod_magic,
		   M0_STOB_IO_DESC_LINK_MAGIC,  M0_STOB_IO_DESC_HEAD_MAGIC);
M0_TL_DEFINE(stobio, static, struct m0_stob_io_desc);

M0_TL_DESCR_DEFINE(netbufs, "Aquired net buffers", static,
		   struct m0_net_buffer, nb_extern_linkage, nb_magic,
		   M0_NET_BUFFER_LINK_MAGIC, M0_IOS_NET_BUFFER_HEAD_MAGIC);
M0_TL_DEFINE(netbufs, static, struct m0_net_buffer);

M0_TL_DESCR_DEFINE(rpcbulkbufs, "rpc bulk buffers", static,
		   struct m0_rpc_bulk_buf, bb_link, bb_magic,
		   M0_RPC_BULK_BUF_MAGIC, M0_RPC_BULK_MAGIC);
M0_TL_DEFINE(rpcbulkbufs, static, struct m0_rpc_bulk_buf);

M0_TL_DESCR_DECLARE(bufferpools, M0_EXTERN);
M0_TL_DECLARE(bufferpools, M0_EXTERN, struct m0_rios_buffer_pool);

M0_INTERNAL bool m0_is_read_fop(const struct m0_fop *fop);
M0_INTERNAL bool m0_is_write_fop(const struct m0_fop *fop);
M0_INTERNAL bool m0_is_io_fop(const struct m0_fop *fop);
M0_INTERNAL struct m0_fop_cob_rw *io_rw_get(struct m0_fop *fop);
M0_INTERNAL struct m0_fop_cob_rw_reply *io_rw_rep_get(struct m0_fop *fop);
M0_INTERNAL bool m0_is_cob_create_fop(const struct m0_fop *fop);
M0_INTERNAL bool m0_is_cob_delete_fop(const struct m0_fop *fop);

static int m0_io_fom_cob_rw_create(struct m0_fop *fop, struct m0_fom **out,
				   struct m0_reqh *reqh);
static int m0_io_fom_cob_rw_tick(struct m0_fom *fom);
static void m0_io_fom_cob_rw_fini(struct m0_fom *fom);
static size_t m0_io_fom_cob_rw_locality_get(const struct m0_fom *fom);
static void m0_io_fom_cob_rw_addb_init(struct m0_fom *fom,
				       struct m0_addb_mc *mc);
M0_INTERNAL const char *m0_io_fom_cob_rw_service_name(struct m0_fom *fom);
static bool m0_io_fom_cob_rw_invariant(const struct m0_io_fom_cob_rw *io);

static int net_buffer_acquire(struct m0_fom *);
static int io_prepare(struct m0_fom *);
static int io_launch(struct m0_fom *);
static int io_finish(struct m0_fom *);
static int zero_copy_initiate(struct m0_fom *);
static int zero_copy_finish(struct m0_fom *);
static int net_buffer_release(struct m0_fom *);

static inline struct m0_addb_mc *fom_to_addb_mc(const struct m0_fom *fom);

/**
 * I/O FOM operation vector.
 */
struct m0_fom_ops ops = {
	.fo_fini = m0_io_fom_cob_rw_fini,
	.fo_tick = m0_io_fom_cob_rw_tick,
	.fo_home_locality = m0_io_fom_cob_rw_locality_get,
	.fo_addb_init = m0_io_fom_cob_rw_addb_init
};

/**
 * I/O FOM type operation vector.
 */
const struct m0_fom_type_ops io_fom_type_ops = {
	.fto_create = m0_io_fom_cob_rw_create,
};

/**
 * I/O Read FOM state transition table.
 * @see DLD-bulk-server-lspec-state
 */
static struct m0_io_fom_cob_rw_state_transition io_fom_read_st[] = {
[M0_FOPH_IO_FOM_PREPARE] =
{ M0_FOPH_IO_FOM_PREPARE, &io_prepare,
  M0_FOPH_IO_FOM_BUFFER_ACQUIRE, 0, "io preparation", },

[M0_FOPH_IO_FOM_BUFFER_ACQUIRE] =
{ M0_FOPH_IO_FOM_BUFFER_ACQUIRE, &net_buffer_acquire,
  M0_FOPH_IO_STOB_INIT, M0_FOPH_IO_FOM_BUFFER_WAIT, "Network buffer acquire", },

[M0_FOPH_IO_FOM_BUFFER_WAIT] =
{ M0_FOPH_IO_FOM_BUFFER_WAIT, &net_buffer_acquire,
  M0_FOPH_IO_STOB_INIT,  M0_FOPH_IO_FOM_BUFFER_WAIT, "Network buffer wait", },

[M0_FOPH_IO_STOB_INIT] =
{ M0_FOPH_IO_STOB_INIT, &io_launch,
  0,  M0_FOPH_IO_STOB_WAIT, "STOB I/O launch", },

[M0_FOPH_IO_STOB_WAIT] =
{ M0_FOPH_IO_STOB_WAIT, &io_finish,
  M0_FOPH_IO_ZERO_COPY_INIT, 0, "STOB I/O finish", },

[M0_FOPH_IO_ZERO_COPY_INIT] =
{ M0_FOPH_IO_ZERO_COPY_INIT, &zero_copy_initiate,
  0, M0_FOPH_IO_ZERO_COPY_WAIT, "Zero-copy initiate", },

[M0_FOPH_IO_ZERO_COPY_WAIT] =
{ M0_FOPH_IO_ZERO_COPY_WAIT, &zero_copy_finish,
  M0_FOPH_IO_BUFFER_RELEASE, 0, "Zero-copy finish", },

[M0_FOPH_IO_BUFFER_RELEASE] =
{ M0_FOPH_IO_BUFFER_RELEASE, &net_buffer_release,
  M0_FOPH_IO_FOM_BUFFER_ACQUIRE,  0, "Network buffer release", },
};

/**
 * I/O Write FOM state transition table.
 * @see DLD-bulk-server-lspec-state
 */
static const struct m0_io_fom_cob_rw_state_transition io_fom_write_st[] = {
[M0_FOPH_IO_FOM_PREPARE] =
{ M0_FOPH_IO_FOM_PREPARE, &io_prepare,
  M0_FOPH_IO_FOM_BUFFER_ACQUIRE, 0, "io preparation", },

[M0_FOPH_IO_FOM_BUFFER_ACQUIRE] =
{ M0_FOPH_IO_FOM_BUFFER_ACQUIRE, &net_buffer_acquire,
  M0_FOPH_IO_ZERO_COPY_INIT, M0_FOPH_IO_FOM_BUFFER_WAIT,
  "Network buffer acquire", },

[M0_FOPH_IO_FOM_BUFFER_WAIT] =
{ M0_FOPH_IO_FOM_BUFFER_WAIT, &net_buffer_acquire,
  M0_FOPH_IO_ZERO_COPY_INIT, M0_FOPH_IO_FOM_BUFFER_WAIT,
  "Network buffer wait", },

[M0_FOPH_IO_ZERO_COPY_INIT] =
{ M0_FOPH_IO_ZERO_COPY_INIT, &zero_copy_initiate,
  0, M0_FOPH_IO_ZERO_COPY_WAIT, "Zero-copy initiate", },

[M0_FOPH_IO_ZERO_COPY_WAIT] =
{ M0_FOPH_IO_ZERO_COPY_WAIT, &zero_copy_finish,
  M0_FOPH_IO_STOB_INIT, 0, "Zero-copy finish", },

[M0_FOPH_IO_STOB_INIT] =
{ M0_FOPH_IO_STOB_INIT, &io_launch,
  0, M0_FOPH_IO_STOB_WAIT, "STOB I/O launch", },

[M0_FOPH_IO_STOB_WAIT] =
{ M0_FOPH_IO_STOB_WAIT, &io_finish,
  M0_FOPH_IO_BUFFER_RELEASE, 0, "STOB I/O finish", },

[M0_FOPH_IO_BUFFER_RELEASE] =
{ M0_FOPH_IO_BUFFER_RELEASE, &net_buffer_release,
  M0_FOPH_IO_FOM_BUFFER_ACQUIRE, 0, "Network buffer release", },
};

struct m0_sm_state_descr io_phases[] = {
	[M0_FOPH_IO_FOM_PREPARE] = {
		.sd_name      = "IO Prepare",
		.sd_allowed   = M0_BITS(M0_FOPH_IO_FOM_BUFFER_ACQUIRE,
					M0_FOPH_FAILURE)
	},
	[M0_FOPH_IO_FOM_BUFFER_ACQUIRE] = {
		.sd_name      = "Network buffer acquire",
		.sd_allowed   = M0_BITS(M0_FOPH_IO_STOB_INIT,
					M0_FOPH_IO_ZERO_COPY_INIT,
					M0_FOPH_IO_FOM_BUFFER_WAIT,
					M0_FOPH_FAILURE)
	},
	[M0_FOPH_IO_FOM_BUFFER_WAIT] = {
		.sd_name      = "Network buffer wait",
		.sd_allowed   = M0_BITS(M0_FOPH_IO_STOB_INIT,
					M0_FOPH_IO_ZERO_COPY_INIT,
					M0_FOPH_IO_FOM_BUFFER_WAIT,
					M0_FOPH_FAILURE)
	},
	[M0_FOPH_IO_STOB_INIT] = {
		.sd_name      = "STOB I/O launch",
		.sd_allowed   = M0_BITS(M0_FOPH_IO_STOB_WAIT,
					M0_FOPH_FAILURE)
	},
	[M0_FOPH_IO_STOB_WAIT] = {
		.sd_name      = "STOB I/O finish",
		.sd_allowed   = M0_BITS(M0_FOPH_IO_ZERO_COPY_INIT,
					M0_FOPH_IO_BUFFER_RELEASE,
					M0_FOPH_FAILURE)
	},
	[M0_FOPH_IO_ZERO_COPY_INIT] = {
		.sd_name      = "Zero-copy initiate",
		.sd_allowed   = M0_BITS(M0_FOPH_IO_ZERO_COPY_WAIT,
					M0_FOPH_FAILURE)
	},
	[M0_FOPH_IO_ZERO_COPY_WAIT] = {
		.sd_name      = "Zero-copy finish",
		.sd_allowed   = M0_BITS(M0_FOPH_IO_BUFFER_RELEASE,
					M0_FOPH_IO_STOB_INIT,
					M0_FOPH_FAILURE)
	},
	[M0_FOPH_IO_BUFFER_RELEASE] = {
		.sd_name      = "Network buffer release",
		.sd_allowed   = M0_BITS(M0_FOPH_IO_FOM_BUFFER_ACQUIRE,
					M0_FOPH_SUCCESS)
	},
};

struct m0_sm_trans_descr io_phases_trans[] = {
	[ARRAY_SIZE(m0_generic_phases_trans)] =
	{"IO Prepared",
	 M0_FOPH_IO_FOM_PREPARE,
	 M0_FOPH_IO_FOM_BUFFER_ACQUIRE},
	{"IO Prepare Failed",
	 M0_FOPH_IO_FOM_PREPARE,
	 M0_FOPH_FAILURE},
	{"Network buffer acquired stobio init",
	 M0_FOPH_IO_FOM_BUFFER_ACQUIRE,
	 M0_FOPH_IO_STOB_INIT},
	{"Network buffer acquired zerocopy init",
	 M0_FOPH_IO_FOM_BUFFER_ACQUIRE,
	 M0_FOPH_IO_ZERO_COPY_INIT},
	{"Wait for network buffer",
	 M0_FOPH_IO_FOM_BUFFER_ACQUIRE,
	 M0_FOPH_IO_FOM_BUFFER_WAIT},
	{"Network buffer acquire failure",
	 M0_FOPH_IO_FOM_BUFFER_ACQUIRE,
	 M0_FOPH_FAILURE},
	{"Network buffer wait finished stobio init",
	 M0_FOPH_IO_FOM_BUFFER_WAIT,
	 M0_FOPH_IO_STOB_INIT},
	{"Wait for network buffer finished zerocopy init",
	 M0_FOPH_IO_FOM_BUFFER_WAIT,
	 M0_FOPH_IO_ZERO_COPY_INIT},
	{"Network buffer not available",
	 M0_FOPH_IO_FOM_BUFFER_WAIT,
	 M0_FOPH_IO_FOM_BUFFER_WAIT},
	{"Wait for network buffer failure",
	 M0_FOPH_IO_FOM_BUFFER_WAIT,
	 M0_FOPH_FAILURE},
	{"STOB I/O launched",
	 M0_FOPH_IO_STOB_INIT,
	 M0_FOPH_IO_STOB_WAIT},
	{"STOB I/O launch failed",
	 M0_FOPH_IO_STOB_INIT,
	 M0_FOPH_FAILURE},
	{"Wait for STOB I/O finished zerocopy init",
	 M0_FOPH_IO_STOB_WAIT,
	 M0_FOPH_IO_ZERO_COPY_INIT},
	{"Wait for STOB I/O finished buffer release",
	 M0_FOPH_IO_STOB_WAIT,
	 M0_FOPH_IO_BUFFER_RELEASE},
	{"Wait for STOB I/O failed",
	 M0_FOPH_IO_STOB_WAIT,
	 M0_FOPH_FAILURE},
	{"Zero-copy initiated",
	 M0_FOPH_IO_ZERO_COPY_INIT,
	 M0_FOPH_IO_ZERO_COPY_WAIT},
	{"Zero-copy initiate failed",
	 M0_FOPH_IO_ZERO_COPY_INIT,
	 M0_FOPH_FAILURE},
	{"Wait for Zero-copy finished buffer release",
	 M0_FOPH_IO_ZERO_COPY_WAIT,
	 M0_FOPH_IO_BUFFER_RELEASE},
	{"Wait for Zero-copy finished stobio init",
	 M0_FOPH_IO_ZERO_COPY_WAIT,
	 M0_FOPH_IO_STOB_INIT},
	{"Wait for Zero-copy failed",
	 M0_FOPH_IO_ZERO_COPY_WAIT,
	 M0_FOPH_FAILURE},
	{"Network buffer released",
	 M0_FOPH_IO_BUFFER_RELEASE,
	 M0_FOPH_IO_FOM_BUFFER_ACQUIRE},
	{"Network buffer released FOM succeed",
	 M0_FOPH_IO_BUFFER_RELEASE,
	 M0_FOPH_SUCCESS},
};

struct m0_sm_conf io_conf = {
	.scf_name      = "IO phases",
	.scf_nr_states = ARRAY_SIZE(io_phases),
	.scf_state     = io_phases,
	.scf_trans_nr  = ARRAY_SIZE(io_phases_trans),
	.scf_trans     = io_phases_trans,
};

static bool m0_io_fom_cob_rw_invariant(const struct m0_io_fom_cob_rw *io)
{
	struct m0_fop_cob_rw *rwfop = io_rw_get(io->fcrw_gen.fo_fop);

	return
		_0C(io != NULL) &&
		_0C(io->fcrw_ndesc == rwfop->crw_desc.id_nr) &&
		_0C(io->fcrw_curr_desc_index >= 0) &&
		_0C(io->fcrw_curr_desc_index <= rwfop->crw_desc.id_nr) &&
		_0C(M0_CHECK_EX(m0_tlist_invariant(&netbufs_tl,
						   &io->fcrw_netbuf_list))) &&
		_0C(io->fcrw_batch_size ==
		    netbufs_tlist_length(&io->fcrw_netbuf_list)) &&
		_0C(io->fcrw_req_count >= io->fcrw_req_count) &&
		_0C(ergo(io->fcrw_req_count > io->fcrw_req_count,
			 io->fcrw_num_stobio_launched > 0)) &&
		_0C(io->fcrw_curr_size <= io->fcrw_total_ioivec_cnt) &&
		_0C(io->fcrw_num_stobio_launched <=
		    stobio_tlist_length(&io->fcrw_stio_list)) &&
		_0C(ergo(io->fcrw_num_stobio_launched <
			 stobio_tlist_length(&io->fcrw_stio_list),
			 m0_fom_phase(&io->fcrw_gen) == M0_FOPH_IO_STOB_WAIT));
}

static bool m0_stob_io_desc_invariant(const struct m0_stob_io_desc *stobio_desc)
{
	return stobio_desc->siod_magic == M0_STOB_IO_DESC_LINK_MAGIC;
}

/**
 * Call back function which gets invoked on a single STOB I/O complete.
 * This function check for STOB I/O list and remove stobio entry from
 * list for completed STOB I/O. After completion of all STOB I/O it
 * sends signal to FOM so that it can again put into run queue.
 *
 * @param cb fom callback for completed STOB I/O entry
 */
static void stobio_complete_cb(struct m0_fom_callback *cb)
{
	struct m0_fom           *fom   = cb->fc_fom;
	struct m0_io_fom_cob_rw *fom_obj;
	struct m0_stob_io_desc  *stio_desc;

	M0_PRE(m0_fom_group_is_locked(fom));
	M0_ASSERT(m0_fom_phase(fom) == M0_FOPH_IO_STOB_WAIT);

	stio_desc = container_of(cb, struct m0_stob_io_desc, siod_fcb);
	M0_ASSERT(m0_stob_io_desc_invariant(stio_desc));

	fom_obj = container_of(fom, struct m0_io_fom_cob_rw, fcrw_gen);
	M0_ASSERT(m0_io_fom_cob_rw_invariant(fom_obj));

        M0_CNT_DEC(fom_obj->fcrw_num_stobio_launched);
	M0_ADDB_POST(fom_to_addb_mc(fom), &m0_addb_rt_ios_desc_io_finish,
		     M0_FOM_ADDB_CTX_VEC(fom),
		     stio_desc->siod_stob_io.si_stob.iv_index[0],
		     fom_obj->fcrw_req_count,
		     m0_time_sub(m0_time_now(), fom_obj->fcrw_io_launch_time));
        if (fom_obj->fcrw_num_stobio_launched == 0)
                m0_fom_ready(fom);
}

enum {
	/**
	 * @see io_fom_cob_rw_fid2stob_map(), io_fom_cob_rw_stob2fid_map().
	 */
	IO_FOM_STOB_KEY_MAX = 0x10000000ULL,
};

/**
 * Function to map given fid to corresponding Component object id(in turn,
 * storage object id).
 *
 * Current mapping function:
 * - if reqh has multiple ad stob domains, then:
 *   - stob domain id name is "adstob";
 *   - stob domain key is cob fid container;
 *   - stob key is cob fid key;
 * - if reqh has only one ad or linux stob domain, then:
 *   - stob domain id name is "adstob" or "linuxstob";
 *   - stob domain key is 0;
 *   - stob key is (cob fid container) * 10000 + (cob fid key)
 *
 * Stob domains are created in cs_storage_init().
 *
 * @param in file identifier
 * @param out corresponding STOB fid
 *
 * @see io_fom_cob_rw_stob2fid_map(), cs_storage_init().
 */
M0_INTERNAL void io_fom_cob_rw_fid2stob_map(const struct m0_fid *in,
					    struct m0_fid *out)
{
	uint8_t  type_id;
	uint64_t dom_id;
	uint64_t dom_key  = in->f_container;
	uint64_t stob_key = in->f_key;
	bool	 stob_ad  = m0_get()->i_reqh_uses_ad_stob;

	type_id = m0_stob_type_id_by_name(stob_ad ? "adstob" : "linuxstob");
	if (!m0_get()->i_reqh_has_multiple_ad_domains || !stob_ad) {
		M0_ASSERT_INFO(stob_key < IO_FOM_STOB_KEY_MAX &&
			       dom_key < UINT64_MAX / IO_FOM_STOB_KEY_MAX,
			       "stob_key = %"PRIu64", dom_key = %"PRIu64
			       ", IO_FOM_STOB_KEY_MAX = %lu",
			       stob_key, dom_key, (long)IO_FOM_STOB_KEY_MAX);
		stob_key = dom_key * IO_FOM_STOB_KEY_MAX + stob_key;
		dom_key	 = 0;
	}
	dom_id = m0_stob_domain__dom_id(type_id, dom_key);
	m0_stob_fid_make(out, dom_id, stob_key);
}

/**
 * Complementary function for io_fom_cob_rw_fid2stob_map().
 */
M0_INTERNAL void io_fom_cob_rw_stob2fid_map(const struct m0_fid *in,
					    struct m0_fid *out)
{
	uint64_t dom_id	  = m0_stob_fid_dom_id_get(in);
	uint64_t dom_key  = m0_stob_domain__dom_key(dom_id);
	uint64_t stob_key = m0_stob_fid_key_get(in);

	if (!m0_get()->i_reqh_has_multiple_ad_domains ||
	    !m0_get()->i_reqh_uses_ad_stob) {
		dom_key	  = stob_key / IO_FOM_STOB_KEY_MAX;
		stob_key %= IO_FOM_STOB_KEY_MAX;
	}
	*out = M0_FID_INIT(dom_key, stob_key);
}

static int io_fom_cob2file(struct m0_fom *fom, struct m0_fid *fid,
			   struct m0_file **out)
{
	int			   rc;
	struct m0_cob		  *cob;
	struct m0_cob_oikey	   oikey;
	struct m0_cob_domain	  *cob_dom;
	struct m0_reqh_io_service *ios;

	M0_PRE(fom != NULL);
	M0_PRE(m0_fid_is_set(fid));

	ios = container_of(fom->fo_service, struct m0_reqh_io_service,
			   rios_gen);
	cob_dom = ios->rios_cdom;
	if (cob_dom == NULL)
		return -EINVAL;

	m0_cob_oikey_make(&oikey, fid, 0);
	rc = m0_cob_locate(cob_dom, &oikey, M0_CA_NSKEY_FREE, &cob);
	if (rc == 0)
		*out = &cob->co_file;

	return rc;
}

/**
 * Copy aligned bufvec segments addresses from net buffer to stob bufvec.
 * STOB I/O expects exact same number of bufvec as index vecs for I/O
 * request. This function copies segment from network buffer with size
 * same as ivec_count.
 *
 * @param fom file operation machine instance.
 * @param obuf pointer to bufvec from stobio object.
 * @param ibuf pointer to bufve from network buffer from buffer pool.
 * @param ivec_count number of vectors  for stob I/O request.
 * @param bshift shift value for current stob to align bufvecs.
 *
 * @pre obuf != NULL
 * @pre ibuf != NULL
 *
 */
static int align_bufvec(struct m0_fom    *fom,
			struct m0_bufvec *obuf,
			struct m0_bufvec *ibuf,
			m0_bcount_t       ivec_count,
			uint32_t          bshift)
{
	int         i;
	m0_bcount_t bufvec_count;
	int         bufvec_seg_size;

	M0_PRE(fom != NULL);
	M0_PRE(obuf != NULL);
	M0_PRE(ibuf != NULL);

	/*
	 * ivec_count in I/O request is already aligned with stob shift.
	 * for bufvec count bufvec segment count should also align with shift.
	 */
	bufvec_seg_size = ibuf->ov_vec.v_count[0] >> bshift;
	/*
	 * It calculates number of bufvecs for I/O request on
	 * the basis of index vec count.
	 * @todo : Assuming size of each bufvec is same.
	 *         This can be better expressin ( or math function).
	 */
	bufvec_count = (ivec_count / bufvec_seg_size) +
		       ((ivec_count % bufvec_seg_size) == 0 ? 0 : 1);

	IOS_ALLOC_ARR(obuf->ov_vec.v_count, bufvec_count, &fom->fo_addb_ctx,
		      ALIGN_BUFVEC_1);
	IOS_ALLOC_ARR(obuf->ov_buf, bufvec_count, &fom->fo_addb_ctx,
		      ALIGN_BUFVEC_2);
	if (obuf->ov_vec.v_count == NULL || obuf->ov_buf == NULL) {
		m0_free(obuf->ov_vec.v_count);
		m0_free(obuf->ov_buf);
		return -ENOMEM;
	}

	obuf->ov_vec.v_nr = bufvec_count;
	/* Align bufvec before copying to bufvec from stob io */
	for (i = 0; i < bufvec_count; i++) {
		obuf->ov_vec.v_count[i] = ibuf->ov_vec.v_count[i] >> bshift;
		obuf->ov_buf[i]         = m0_stob_addr_pack(ibuf->ov_buf[i],
							    bshift);
	}
	return 0;
}

/**
 * Locates a storage object.
 */
static int stob_object_find(struct m0_fom *fom)
{
	int			 result;
	struct m0_io_fom_cob_rw	*fom_obj;
	struct m0_fid		 stob_fid;
	struct m0_fop_cob_rw	*rwfop;
	// struct m0_stob_domain	*fom_stdom;

	M0_PRE(fom != NULL);
        M0_PRE(m0_is_io_fop(fom->fo_fop));

	fom_obj = container_of(fom, struct m0_io_fom_cob_rw, fcrw_gen);
	M0_ASSERT(m0_io_fom_cob_rw_invariant(fom_obj));

	rwfop = io_rw_get(fom->fo_fop);

	io_fom_cob_rw_fid2stob_map(&rwfop->crw_fid, &stob_fid);
	result = m0_stob_find(&stob_fid, &fom_obj->fcrw_stob);
	if (result != 0)
		return result;
	if (m0_stob_state_get(fom_obj->fcrw_stob) == CSS_UNKNOWN)
		result = m0_stob_locate(fom_obj->fcrw_stob);
	if (result != 0)
		m0_stob_put(fom_obj->fcrw_stob);
	return result;
}

/**
 * Create and initiate I/O FOM and return generic struct m0_fom
 * Find the corresponding fom_type and associate it with m0_fom.
 * Associate fop with fom type.
 *
 * @param fop file operation packet need to process
 * @param out file operation machine need to allocate and initiate
 *
 * @pre fop != NULL
 * @pre out != NULL
 */
static int m0_io_fom_cob_rw_create(struct m0_fop *fop, struct m0_fom **out,
				   struct m0_reqh *reqh)
{
	int                      rc = 0;
	struct m0_fom           *fom;
	struct m0_io_fom_cob_rw *fom_obj;
	struct m0_fop_cob_rw    *rwfop;
	struct m0_fop           *rep_fop;

	M0_PRE(fop != NULL);
	M0_PRE(m0_is_io_fop(fop));
	M0_PRE(out != NULL);

	M0_ENTRY("fop=%p", fop);

	IOS_ALLOC_PTR(fom_obj, &m0_ios_addb_ctx, FOM_COB_RW_CREATE);
	if (fom_obj == NULL)
		return M0_RC(-ENOMEM);

	rep_fop = m0_is_read_fop(fop) ?
		    m0_fop_alloc(&m0_fop_cob_readv_rep_fopt, NULL) :
		    m0_fop_alloc(&m0_fop_cob_writev_rep_fopt, NULL);
	if (rep_fop == NULL) {
		m0_free(fom_obj);
		return M0_RC(-ENOMEM);
	}

	fom   = &fom_obj->fcrw_gen;
	rwfop = io_rw_get(fop);
	*out  = fom;
	m0_fom_init(fom, &fop->f_type->ft_fom_type, &ops, fop, rep_fop, reqh);
	m0_fop_put(rep_fop);

	fom_obj->fcrw_fom_start_time      = m0_time_now();
	fom_obj->fcrw_stob                = NULL;
	fom_obj->fcrw_ndesc               = rwfop->crw_desc.id_nr;
	fom_obj->fcrw_curr_desc_index     = 0;
	fom_obj->fcrw_curr_size           = 0;
	fom_obj->fcrw_batch_size          = 0;
	fom_obj->fcrw_req_count           = 0;
	fom_obj->fcrw_total_ioivec_cnt    = m0_io_count(&rwfop->crw_ivec);
	fom_obj->fcrw_count               = 0;
	fom_obj->fcrw_num_stobio_launched = 0;
	fom_obj->fcrw_bp                  = NULL;

	netbufs_tlist_init(&fom_obj->fcrw_netbuf_list);
	stobio_tlist_init(&fom_obj->fcrw_stio_list);
	stobio_tlist_init(&fom_obj->fcrw_done_list);

	M0_LOG(M0_DEBUG, "fom=%p : op=%s, desc=%d.", fom,
	       m0_is_read_fop(fop) ? "READ" : "WRITE", rwfop->crw_desc.id_nr);

        return M0_RC(rc);
}

/**
 * Checks client and server pool machine version numbers.
 * Checks target device state for cob fid.
 */
int ios__poolmach_check(struct m0_poolmach *poolmach,
			struct m0_pool_version_numbers *cliv,
			struct m0_fid *cob_fid)
{
	struct m0_pool_version_numbers curr;
	enum m0_pool_nd_state          device_state = 0;
	int                            rc;
	M0_ENTRY();

	m0_poolmach_current_version_get(poolmach, &curr);

	/* Check the client version and server version before any processing */
	if (m0_poolmach_version_before(cliv, &curr)) {
		M0_LOG(M0_DEBUG, "VERSION MISMATCH! poolmach = %p", poolmach);

		m0_poolmach_version_dump(cliv);
		m0_poolmach_version_dump(&curr);
		m0_poolmach_event_list_dump(poolmach);
		m0_poolmach_device_state_dump(poolmach);
		return M0_RC(M0_IOP_ERROR_FAILURE_VECTOR_VER_MISMATCH);
	}

	rc = m0_poolmach_device_state(poolmach, cob_fid->f_container,
				      &device_state);
	if ((rc != 0) || (device_state != M0_PNDS_ONLINE &&
			  device_state != M0_PNDS_SNS_REPAIRED)) {
		if (rc == 0) {
			M0_LOG(M0_DEBUG, "IO @"FID_F" on failed device: "
					 "state = %d",
			       FID_P(cob_fid), device_state);
			rc = -EIO;
		}
	}
	return M0_RC(rc);
}

static int io_prepare(struct m0_fom *fom)
{
	struct m0_fop_cob_rw           *rwfop;
	struct m0_poolmach             *poolmach;
	struct m0_reqh                 *reqh;
	struct m0_fop_cob_rw_reply     *rwrep;
	struct m0_pool_version_numbers *cliv;
	int                             rc;

	M0_ENTRY("fom=%p", fom);

	reqh = m0_fom_reqh(fom);
	poolmach = m0_ios_poolmach_get(reqh);
	rwfop = io_rw_get(fom->fo_fop);
	rwrep = io_rw_rep_get(fom->fo_rep_fop);
	cliv = (struct m0_pool_version_numbers*)(&rwfop->crw_version);

	M0_LOG(M0_DEBUG, "Preparing %s IO @"FID_F"",
	       m0_is_read_fop(fom->fo_fop)? "Read": "Write",
	       FID_P(&rwfop->crw_fid));
	/*
	 * Dumps the state of SNS repair with respect to global fid
	 * from IO fop.
	 * The IO request has already acquired file level lock on
	 * given global fid.
	 */
	rwrep->rwr_repair_done = m0_sns_cm_fid_repair_done(&rwfop->crw_gfid,
							   reqh);

	rc = ios__poolmach_check(poolmach, cliv, &rwfop->crw_fid);
	if (rc != 0)
		m0_fom_phase_move(fom, rc, M0_FOPH_FAILURE);

	M0_LEAVE();
	return M0_FSO_AGAIN;
}
/**
 * Acquire network buffers.
 * Gets as many network buffer as it can to process io request.
 * It needs at least one network buffer to start io processing.
 * If not able to get single buffer, FOM wait for buffer pool to
 * become non-empty.
 *
 * If acquired buffers are less than number of fop descriptors then
 * I/O processing will be done by batch.
 *
 * @param fom file operation machine instance.
 * @pre fom != NULL
 * @pre fom->fo_service != NULL
 * @pre m0_fom_phase(fom) == M0_FOPH_IO_FOM_BUFFER_ACQUIRE
 */
static int net_buffer_acquire(struct m0_fom *fom)
{
        uint32_t                   colour;
        int                        acquired_net_bufs;
        int                        required_net_bufs;
        struct m0_fop             *fop;
        struct m0_io_fom_cob_rw   *fom_obj;
        struct m0_net_transfer_mc *tm;

        M0_PRE(fom != NULL);
        M0_PRE(m0_is_io_fop(fom->fo_fop));
        M0_PRE(fom->fo_service != NULL);
        M0_PRE(m0_fom_phase(fom) == M0_FOPH_IO_FOM_BUFFER_ACQUIRE ||
               m0_fom_phase(fom) == M0_FOPH_IO_FOM_BUFFER_WAIT);

	M0_ENTRY("fom=%p", fom);

        fom_obj = container_of(fom, struct m0_io_fom_cob_rw, fcrw_gen);
        M0_ASSERT(m0_io_fom_cob_rw_invariant(fom_obj));

        fop = fom->fo_fop;
        fom_obj->fcrw_phase_start_time = m0_time_now();

        tm = io_fop_tm_get(fop);
        /**
         * Cache buffer pool pointer with FOM object.
         */
        if (fom_obj->fcrw_bp == NULL) {
                struct m0_reqh_io_service  *serv_obj;
                struct m0_rios_buffer_pool *bpdesc;
                struct m0_net_domain       *fop_ndom;

                serv_obj = container_of(fom->fo_service,
                                        struct m0_reqh_io_service, rios_gen);
                M0_ASSERT(m0_reqh_io_service_invariant(serv_obj));

                /* Get network buffer pool for network domain */
                fop_ndom = tm->ntm_dom;
                bpdesc = m0_tl_find(bufferpools, bpdesc,
				    &serv_obj->rios_buffer_pools,
				    bpdesc->rios_ndom == fop_ndom);
		fom_obj->fcrw_bp = bpdesc == NULL ? NULL : &bpdesc->rios_bp;
                M0_ASSERT(fom_obj->fcrw_bp != NULL);
        }
        colour = m0_net_tm_colour_get(tm);

        acquired_net_bufs = netbufs_tlist_length(&fom_obj->fcrw_netbuf_list);
        required_net_bufs = fom_obj->fcrw_ndesc - fom_obj->fcrw_curr_desc_index;

        /*
         * Acquire as many net buffers as to process all descriptors.
         * If FOM is able to acquire more buffers then it can change batch size
         * dynamically.
         */
        M0_ASSERT(acquired_net_bufs <= required_net_bufs);
        while (acquired_net_bufs < required_net_bufs) {
            struct m0_net_buffer *nb;

            m0_net_buffer_pool_lock(fom_obj->fcrw_bp);
            nb = m0_net_buffer_pool_get(fom_obj->fcrw_bp, colour);

            if (nb == NULL && acquired_net_bufs == 0) {
                    struct m0_rios_buffer_pool *bpdesc;

                    /*
                     * Network buffer is not available. At least one
                     * buffer is need for zero-copy. Registers FOM clink
                     * with buffer pool wait channel to get buffer
                     * pool non-empty signal.
                     */
                    bpdesc = container_of(fom_obj->fcrw_bp,
					  struct m0_rios_buffer_pool, rios_bp);
		    M0_ADDB_POST(fom_to_addb_mc(fom),
				 &m0_addb_rt_ios_buffer_pool_low,
				 M0_FOM_ADDB_CTX_VEC(fom));

                    m0_fom_wait_on(fom, &bpdesc->rios_bp_wait, &fom->fo_cb);

                    m0_fom_phase_set(fom, M0_FOPH_IO_FOM_BUFFER_WAIT);
                    m0_net_buffer_pool_unlock(fom_obj->fcrw_bp);

                    M0_LEAVE();
                    return M0_FSO_WAIT;
            } else if (nb == NULL) {
                    m0_net_buffer_pool_unlock(fom_obj->fcrw_bp);
                    /*
                     * Some network buffers are available for zero copy
                     * init. FOM can continue with available buffers.
                     */
                    break;
            }
            m0_net_buffer_pool_unlock(fom_obj->fcrw_bp);

            if (m0_is_read_fop(fop))
                   nb->nb_qtype = M0_NET_QT_ACTIVE_BULK_SEND;
            else
                   nb->nb_qtype = M0_NET_QT_ACTIVE_BULK_RECV;

            M0_INVARIANT_EX(m0_tlist_invariant(&netbufs_tl,
					       &fom_obj->fcrw_netbuf_list));

            netbufs_tlink_init(nb);
            netbufs_tlist_add(&fom_obj->fcrw_netbuf_list, nb);
            acquired_net_bufs++;
        }

        fom_obj->fcrw_batch_size = acquired_net_bufs;
	M0_LOG(M0_DEBUG, "Acquired network buffers, batch_size = %d.",
	       fom_obj->fcrw_batch_size);

	M0_LEAVE();
        return M0_FSO_AGAIN;
}

/**
 * Release network buffer.
 * Return back network buffer to buffer pool.
 * If acquired buffers are more than the remaining descriptors
 * release extra buffers so that other FOM can use.
 *
 * @param fom file operation machine.
 *
 * @pre fom != NULL
 * @pre fom->fo_service != NULL
 * @pre m0_fom_phase(fom) == M0_FOPH_IO_BUFFER_RELEASE
 */
static int net_buffer_release(struct m0_fom *fom)
{
        uint32_t                  colour;
        int                       acquired_net_bufs;
        int                       required_net_bufs;
        struct m0_fop             *fop;
        struct m0_io_fom_cob_rw   *fom_obj;
        struct m0_net_transfer_mc *tm;

	M0_PRE(fom != NULL);
	M0_PRE(m0_is_read_fop(fom->fo_fop) || m0_is_write_fop(fom->fo_fop));
	M0_PRE(fom->fo_service != NULL);
	M0_PRE(m0_fom_phase(fom) == M0_FOPH_IO_BUFFER_RELEASE);

	M0_ENTRY("fom=%p", fom);

	fom_obj = container_of(fom, struct m0_io_fom_cob_rw, fcrw_gen);
	M0_ASSERT(m0_io_fom_cob_rw_invariant(fom_obj));
	M0_ASSERT(fom_obj->fcrw_bp != NULL);

	fop    = fom->fo_fop;
	tm     = io_fop_tm_get(fop);
	colour = m0_net_tm_colour_get(tm);

	M0_INVARIANT_EX(m0_tlist_invariant(&netbufs_tl,
					   &fom_obj->fcrw_netbuf_list));
	acquired_net_bufs = netbufs_tlist_length(&fom_obj->fcrw_netbuf_list);
	required_net_bufs = fom_obj->fcrw_ndesc - fom_obj->fcrw_curr_desc_index;

	m0_net_buffer_pool_lock(fom_obj->fcrw_bp);
	while (acquired_net_bufs > required_net_bufs) {
		struct m0_net_buffer *nb;

		nb = netbufs_tlist_tail(&fom_obj->fcrw_netbuf_list);
		M0_ASSERT(nb != NULL);
		m0_net_buffer_pool_put(fom_obj->fcrw_bp, nb, colour);
		netbufs_tlink_del_fini(nb);
		acquired_net_bufs--;
	}
	m0_net_buffer_pool_unlock(fom_obj->fcrw_bp);

        fom_obj->fcrw_batch_size = acquired_net_bufs;
	M0_LOG(M0_DEBUG, "Released network buffers, batch_size = %d.",
	       fom_obj->fcrw_batch_size);

        if (required_net_bufs == 0)
               m0_fom_phase_set(fom, M0_FOPH_SUCCESS);

	M0_LEAVE();
	return M0_FSO_AGAIN;
}

/**
 * Initiate zero-copy
 * Initiates zero-copy for batch of descriptors.
 * And wait for zero-copy to complete for all descriptors.
 * Network layer signaled on m0_rpc_bulk::rb_chan on completion.
 *
 * @param fom file operation machine.
 *
 * @pre fom != NULL
 * @pre m0_fom_phase(fom) == M0_FOPH_IO_ZERO_COPY_INIT
 */
static int zero_copy_initiate(struct m0_fom *fom)
{
        int                          rc;
        struct m0_fop               *fop;
        struct m0_io_fom_cob_rw     *fom_obj;
        struct m0_fop_cob_rw        *rwfop;
        struct m0_rpc_bulk          *rbulk;
        struct m0_net_buffer        *nb;
        struct m0_net_buf_desc_data *nbd_data;
        struct m0_net_domain        *dom;
        uint32_t                     buffers_added = 0;
	m0_bcount_t                  max_seg_size;

        M0_PRE(fom != NULL);
        M0_PRE(m0_is_io_fop(fom->fo_fop));
        M0_PRE(m0_fom_phase(fom) == M0_FOPH_IO_ZERO_COPY_INIT);

	M0_ENTRY("fom=%p", fom);

	fom_obj = container_of(fom, struct m0_io_fom_cob_rw, fcrw_gen);
	M0_ASSERT(m0_io_fom_cob_rw_invariant(fom_obj));

	fom_obj->fcrw_phase_start_time = m0_time_now();

	fop   = fom->fo_fop;
	rwfop = io_rw_get(fop);
	rbulk = &fom_obj->fcrw_bulk;
	m0_rpc_bulk_init(rbulk);

	M0_INVARIANT_EX(m0_tlist_invariant(&netbufs_tl,
					   &fom_obj->fcrw_netbuf_list));
	dom          = io_fop_tm_get(fop)->ntm_dom;
	max_seg_size = m0_net_domain_get_max_buffer_segment_size(dom);
	nbd_data     = &rwfop->crw_desc.id_descs[fom_obj->
						 fcrw_curr_desc_index];

        /* Create rpc bulk bufs list using available net buffers */
        m0_tl_for(netbufs, &fom_obj->fcrw_netbuf_list, nb) {
                uint32_t                segs_nr = 0;
                struct m0_rpc_bulk_buf *rb_buf;
		m0_bcount_t             used_size;

		used_size = rwfop->crw_desc.id_descs[fom_obj->
						fcrw_curr_desc_index].bdd_used;
		segs_nr = used_size / max_seg_size;
		M0_LOG(M0_DEBUG, "segs_nr %d", segs_nr);

                /*
                 * @todo : Since passing only number of segmnts, supports full
                 *         stripe I/Os. Should set exact count for last segment
                 *         of network buffer. Also need to reset last segment
                 *         count to original since buffers are reused by other
                 *         I/O requests.
                 */
		rc = m0_rpc_bulk_buf_add(rbulk, segs_nr, dom, nb, &rb_buf);
                if (rc != 0) {
                        m0_fom_phase_move(fom, rc, M0_FOPH_FAILURE);
                        IOS_ADDB_FUNCFAIL(rc, ZERO_COPY_INITIATE_1,
					  &fom->fo_addb_ctx);
			M0_LEAVE();
                        return M0_FSO_AGAIN;
                }

                fom_obj->fcrw_curr_desc_index++;
                buffers_added++;
        } m0_tl_endfor;

        M0_ASSERT(buffers_added == fom_obj->fcrw_batch_size);

        /*
         * On completion of zero-copy on all buffers rpc_bulk
         * sends signal on channel rbulk->rb_chan.
         */
        m0_mutex_lock(&rbulk->rb_mutex);
        m0_fom_wait_on(fom, &rbulk->rb_chan, &fom->fo_cb);
        m0_mutex_unlock(&rbulk->rb_mutex);

        /*
         * This function deletes m0_rpc_bulk_buf object one
         * by one as zero copy completes on respective buffer.
         */
        rc = m0_rpc_bulk_load(rbulk, fop->f_item.ri_session->s_conn, nbd_data);
        if (rc != 0) {
                m0_mutex_lock(&rbulk->rb_mutex);
                m0_fom_callback_cancel(&fom->fo_cb);
                m0_mutex_unlock(&rbulk->rb_mutex);
                m0_rpc_bulk_buflist_empty(rbulk);
                m0_rpc_bulk_fini(rbulk);
                m0_fom_phase_move(fom, rc, M0_FOPH_FAILURE);
                IOS_ADDB_FUNCFAIL(rc, ZERO_COPY_INITIATE_2, &fom->fo_addb_ctx);
		M0_LEAVE();
                return M0_FSO_AGAIN;
        }
	M0_LOG(M0_DEBUG, "Zero-copy initiated. Added buffers %d",
	       buffers_added);

	M0_LEAVE();
        return M0_FSO_WAIT;
}

/**
 * Zero-copy Finish
 * Check for zero-copy result.
 *
 * @param fom file operation machine.
 *
 * @pre fom != NULL
 * @pre m0_fom_phase(fom) == M0_FOPH_IO_ZERO_COPY_WAIT
 */
static int zero_copy_finish(struct m0_fom *fom)
{
	struct m0_io_fom_cob_rw *fom_obj;
	struct m0_rpc_bulk      *rbulk;

	M0_ENTRY("fom=%p", fom);

        M0_PRE(fom != NULL);
        M0_PRE(m0_is_io_fop(fom->fo_fop));
        M0_PRE(m0_fom_phase(fom) == M0_FOPH_IO_ZERO_COPY_WAIT);

	fom_obj = container_of(fom, struct m0_io_fom_cob_rw, fcrw_gen);
        M0_ASSERT(m0_io_fom_cob_rw_invariant(fom_obj));

        rbulk = &fom_obj->fcrw_bulk;

        m0_mutex_lock(&rbulk->rb_mutex);
        M0_ASSERT(rpcbulkbufs_tlist_is_empty(&rbulk->rb_buflist));
        if (rbulk->rb_rc != 0){
                m0_fom_phase_move(fom, rbulk->rb_rc, M0_FOPH_FAILURE);
                IOS_ADDB_FUNCFAIL(rbulk->rb_rc, ZERO_COPY_FINISH,
				  &fom->fo_addb_ctx);
                m0_mutex_unlock(&rbulk->rb_mutex);
		M0_LEAVE();
                return M0_FSO_AGAIN;
        }

        m0_mutex_unlock(&rbulk->rb_mutex);
        m0_rpc_bulk_fini(rbulk);
	M0_LOG(M0_DEBUG, "Zero-copy finished.");

	M0_LEAVE();
        return M0_FSO_AGAIN;
}

static void stio_desc_fini(struct m0_stob_io_desc *stio_desc)
{
	struct m0_stob_io *stio = &stio_desc->siod_stob_io;

	stobio_tlist_remove(stio_desc);
	m0_stob_io_fini(stio);
	m0_fom_callback_fini(&stio_desc->siod_fcb);
	m0_free(stio->si_user.ov_vec.v_count);
	m0_free(stio->si_user.ov_buf);
	m0_indexvec_free(&stio->si_stob);
	m0_free(stio_desc);
}

/**
 * Launch STOB I/O
 * Helper function to launch STOB I/O.
 * This function initiates STOB I/O for all index vecs.
 * STOB I/O signaled on channel in m0_stob_io::si_wait.
 * There is a clink for each STOB I/O waiting on respective
 * m0_stob_io::si_wait. For every STOB I/O completion call-back
 * is launched to check its results and mark complete. After
 * all STOB I/O completes call-back function send signal to FOM
 * so that FOM gets out of wait-queue and placed in run-queue.
 *
 * @param fom file operation machine
 *
 * @pre fom != NULL
 * @pre m0_fom_phase(fom) == M0_FOPH_IO_STOB_INIT
 */
static int io_launch(struct m0_fom *fom)
{
	int			 rc;
	struct m0_fop		*fop;
	struct m0_io_fom_cob_rw	*fom_obj;
	struct m0_net_buffer    *nb;
	struct m0_fop_cob_rw	*rwfop;
	struct m0_file		*file;
	struct m0_net_domain    *dom;
	uint32_t                 index;

	M0_PRE(fom != NULL);
        M0_PRE(m0_is_io_fop(fom->fo_fop));
        M0_PRE(m0_fom_phase(fom) == M0_FOPH_IO_STOB_INIT);

	M0_ENTRY("fom=%p", fom);

	fom_obj = container_of(fom, struct m0_io_fom_cob_rw, fcrw_gen);
	M0_ASSERT(m0_io_fom_cob_rw_invariant(fom_obj));
	M0_ASSERT(fom_obj->fcrw_num_stobio_launched == 0);
	M0_ASSERT(fom_obj->fcrw_ivec.iv_vec.v_nr > 0);

	fom_obj->fcrw_phase_start_time = m0_time_now();

	fop   = fom->fo_fop;
	rwfop = io_rw_get(fop);
	dom   = io_fop_tm_get(fop)->ntm_dom;

	rc = stob_object_find(fom);
	if (rc != 0)
		goto out;

	rc = io_fom_cob2file(fom, &rwfop->crw_fid, &file);
	if (rc != 0)
		goto out;

	/*
	  Since the upper layer IO block size could differ with IO block size
	  of storage object, the block alignment and mapping is necessary.
	 */
	fom_obj->fcrw_bshift = m0_stob_block_shift(fom_obj->fcrw_stob);

	M0_INVARIANT_EX(m0_tlist_invariant(&netbufs_tl,
					   &fom_obj->fcrw_netbuf_list));
	M0_INVARIANT_EX(m0_tlist_invariant(&stobio_tl,
					   &fom_obj->fcrw_stio_list));

	index = fom_obj->fcrw_curr_desc_index;
	/*
	 * During write, zero copy is performed before stob I/O is launched.
	 * For zero copy processing fcrw_curr_desc_index is
	 * incremented for each processed net buffer. Whereas for read, first
	 * stob I/O is launched and then zero copy is performed. So index is
	 * decremented by value of number of net buffers to process in case of
	 * write operation.
	 */
	index -= m0_is_write_fop(fop) ?
		netbufs_tlist_length(&fom_obj->fcrw_netbuf_list) : 0;

	m0_tl_for(netbufs, &fom_obj->fcrw_netbuf_list, nb) {
		struct m0_indexvec     *mem_ivec;
		struct m0_stob_io_desc *stio_desc;
		struct m0_stob_io      *stio;
		m0_bcount_t             ivec_count;
		struct m0_buf	       *di_buf;
		struct m0_bufvec	cksum_data;
		m0_bcount_t             todo;

		IOS_ALLOC_PTR(stio_desc, &m0_ios_addb_ctx, IO_LAUNCH_2);
		if (stio_desc == NULL) {
			rc = -ENOMEM;
			break;
		}

		stio_desc->siod_magic = M0_STOB_IO_DESC_LINK_MAGIC;
		m0_fom_callback_init(&stio_desc->siod_fcb);

		stio = &stio_desc->siod_stob_io;
		m0_stob_io_init(stio);
		stio->si_fol_frag = &stio_desc->siod_fol_frag;

		mem_ivec  = &stio->si_stob;
		todo = rwfop->crw_desc.id_descs[index++].bdd_used >>
			fom_obj->fcrw_bshift;
		rc = m0_indexvec_split(&fom_obj->fcrw_ivec,
				       fom_obj->fcrw_curr_size, todo,
				       /* fom_obj->fcrw_bshift */ 0,
				       &fom->fo_addb_ctx,
				       M0_IOS_ADDB_LOC_FOM_IVEC_ALLOC,
				       mem_ivec);
                if (rc != 0) {
                        /*
                         * Since this stob io not added into list
                         * yet, free it here.
                         */
                        IOS_ADDB_FUNCFAIL(rc, IO_LAUNCH_2, &fom->fo_addb_ctx);
                        m0_stob_io_fini(stio);
                        m0_free(stio_desc);
                        break;
                }

                /*
                 * Copy aligned network buffer to stobio object.
                 * Also trim network buffer as per I/O size.
                 */
                ivec_count = m0_vec_count(&mem_ivec->iv_vec);
                rc = align_bufvec(fom, &stio->si_user, &nb->nb_buffer,
				  ivec_count, fom_obj->fcrw_bshift);
                if (rc != 0) {
                        /*
                         * Since this stob io not added into list
                         * yet, free it here.
                         */
			fom_obj->fcrw_rc = rc;
                        IOS_ADDB_FUNCFAIL(rc, IO_LAUNCH_3, &fom->fo_addb_ctx);
			stio_desc_fini(stio_desc);
                        break;
                }

                if (m0_is_write_fop(fop)) {
			uint32_t di_size = m0_di_size_get(file, ivec_count);
			uint32_t curr_pos = m0_di_size_get(file,
						fom_obj->fcrw_curr_size);
			di_buf = &rwfop->crw_di_data;
			if (di_buf != NULL) {
				struct m0_buf buf = M0_BUF_INIT(di_size,
						di_buf->b_addr + curr_pos);
				cksum_data = (struct m0_bufvec)
					M0_BUFVEC_INIT_BUF(&buf.b_addr,
							   &buf.b_nob);
				M0_ASSERT(file->fi_di_ops->do_check(file,
					  mem_ivec, &nb->nb_buffer,
					  &cksum_data));
			}
		}
		stio->si_opcode = m0_is_write_fop(fop) ? SIO_WRITE : SIO_READ;

		/*
		 * The value is already bshifted during conversion of
		 * m0_io_indexvec from on-wire to in-mem.
		 * */
		fom_obj->fcrw_curr_size += ivec_count;
                stio_desc->siod_fcb.fc_bottom = stobio_complete_cb;
                m0_mutex_lock(&stio->si_mutex);
                m0_fom_callback_arm(fom, &stio->si_wait, &stio_desc->siod_fcb);
                m0_mutex_unlock(&stio->si_mutex);

                M0_LOG(M0_DEBUG, "launch fom: %lu, req_count: %lx, "
		       "count: %lx, submitted: %lx, expect: %lx",
		       fom_obj->fcrw_fom_start_time,
		       fom_obj->fcrw_req_count, fom_obj->fcrw_count,
		       m0_vec_count(&stio->si_user.ov_vec), ivec_count);
		fom_obj->fcrw_io_launch_time = m0_time_now();

                rc = m0_stob_io_launch(stio, fom_obj->fcrw_stob,
				       &fom->fo_tx, NULL);
                if (rc != 0) {
			M0_LOG(M0_ERROR, "stob_io_launch failed: rc=%d", rc);
                        m0_mutex_lock(&stio->si_mutex);
                        m0_fom_callback_cancel(&stio_desc->siod_fcb);
                        m0_mutex_unlock(&stio->si_mutex);
                        /*
                         * Since this stob io not added into list
                         * yet, free it here.
                         */
			fom_obj->fcrw_rc = rc;
                        IOS_ADDB_FUNCFAIL(rc, IO_LAUNCH_4, &fom->fo_addb_ctx);
                        m0_stob_io_fini(stio);
			m0_fom_callback_fini(&stio_desc->siod_fcb);
			m0_free(stio_desc);
			break;
		}

		fom_obj->fcrw_req_count += ivec_count;
		M0_ASSERT(fom_obj->fcrw_req_count > 0);
		M0_CNT_INC(fom_obj->fcrw_num_stobio_launched);

		stobio_tlink_init(stio_desc);
		stobio_tlist_add(&fom_obj->fcrw_stio_list, stio_desc);
	} m0_tl_endfor;

	M0_LOG(M0_DEBUG, "total  fom: %lu, expect: %lx",
	       fom_obj->fcrw_fom_start_time,
	       fom_obj->fcrw_req_count - fom_obj->fcrw_count);

	if (fom_obj->fcrw_num_stobio_launched > 0) {
		M0_LOG(M0_DEBUG, "STOB I/O launched, io_descs = %d",
		       fom_obj->fcrw_num_stobio_launched);
		M0_LEAVE();
		return M0_FSO_WAIT;
	}

out:
	if (fom_obj->fcrw_stob != NULL)
		m0_stob_put(fom_obj->fcrw_stob);
	if (rc != 0) {
		m0_fom_phase_move(fom, rc, M0_FOPH_FAILURE);
		IOS_ADDB_FUNCFAIL(rc, IO_LAUNCH_1, &fom->fo_addb_ctx);
	} else {
		/* empty operation */
		M0_ASSERT(fom_obj->fcrw_num_stobio_launched == 0);
		M0_IMPOSSIBLE("Implement me.");
	}
	M0_LEAVE();
	return M0_FSO_AGAIN;
}

/**
 * This function finish STOB I/O.
 * It's check for STOB I/O result and return back STOB instance.
 *
 * @param fom instance file operation machine under execution
 *
 * @pre fom != NULL
 * @pre m0_fom_phase(fom) == M0_FOPH_IO_STOB_WAIT
 */
static int io_finish(struct m0_fom *fom)
{
        struct m0_io_fom_cob_rw *fom_obj;
        struct m0_stob_io_desc  *stio_desc;
	int                      rc  = 0;
	m0_bcount_t              nob = 0;

        M0_PRE(fom != NULL);
        M0_PRE(m0_is_io_fop(fom->fo_fop));
        M0_PRE(m0_fom_phase(fom) == M0_FOPH_IO_STOB_WAIT);

	M0_ENTRY("fom=%p", fom);

	if (M0_FI_ENABLED("fake_error"))
		rc = -EINVAL;

        fom_obj = container_of(fom, struct m0_io_fom_cob_rw, fcrw_gen);
        M0_ASSERT(m0_io_fom_cob_rw_invariant(fom_obj));
        M0_ASSERT(fom_obj->fcrw_num_stobio_launched == 0);
        M0_INVARIANT_EX(m0_tlist_invariant(&stobio_tl,
					   &fom_obj->fcrw_stio_list));
        /*
         * Empty the list as all STOB I/O completed here.
         */
	m0_tl_teardown(stobio, &fom_obj->fcrw_stio_list, stio_desc) {
                struct m0_stob_io *stio;

                stio = &stio_desc->siod_stob_io;

                if (stio->si_rc != 0) {
                        rc = stio->si_rc;
                } else {
                        nob += stio->si_count;
                        M0_LOG(M0_DEBUG, "rw_count %lx, si_count %lx",
                               fom_obj->fcrw_count, stio->si_count);
                }
		stobio_tlist_add(&fom_obj->fcrw_done_list, stio_desc);
        }

	M0_LOG(M0_DEBUG, "got    fom: %lu, req_count: %lx, "
	       "count: %lx, nob: %lx", fom_obj->fcrw_fom_start_time,
	       fom_obj->fcrw_req_count, fom_obj->fcrw_count, nob);
	fom_obj->fcrw_count += nob;
	M0_ADDB_POST(fom_to_addb_mc(fom), &m0_addb_rt_ios_io_finish,
		     M0_FOM_ADDB_CTX_VEC(fom), fom_obj->fcrw_count,
		     m0_time_sub(m0_time_now(),
				 fom_obj->fcrw_phase_start_time));
        m0_stob_put(fom_obj->fcrw_stob);
	M0_ASSERT(ergo(rc == 0,
		       fom_obj->fcrw_req_count == fom_obj->fcrw_count));
	rc = fom_obj->fcrw_rc ?: rc;
        if (rc != 0) {
		M0_LOG(M0_ERROR, "rc=%d", rc);
		m0_fom_phase_move(fom, rc, M0_FOPH_FAILURE);
                IOS_ADDB_FUNCFAIL(rc, IO_FINISH, &fom->fo_addb_ctx);
		M0_LEAVE();
	        return M0_FSO_AGAIN;
        }

	M0_LOG(M0_DEBUG, "STOB I/O finished.");

	M0_LEAVE();
        return M0_FSO_AGAIN;
}

static int indexvec_wire2mem(struct m0_fom *fom, uint32_t bshift)
{
	int                      rc = 0;
	uint32_t                 max_frags_nr = 0;
	struct m0_io_fom_cob_rw *fom_obj;
	struct m0_fop_cob_rw    *rwfop;
	struct m0_indexvec      *iv;

	M0_PRE(fom != NULL);

	fom_obj = container_of(fom, struct m0_io_fom_cob_rw, fcrw_gen);

	rwfop = io_rw_get(fom->fo_fop);
	iv = &fom_obj->fcrw_ivec;
	fom_obj->fcrw_bshift = bshift;

	max_frags_nr = rwfop->crw_ivec.ci_nr;
	M0_LOG(M0_DEBUG, "max_frags_nr=%d", max_frags_nr);

	if (max_frags_nr > 0)
		rc = m0_indexvec_wire2mem(&rwfop->crw_ivec, max_frags_nr,
					  bshift, &m0_ios_addb_ctx,
					  M0_IOS_ADDB_LOC_FOM_IVEC_ALLOC, iv);
	return M0_RC(rc);
}

/*
 * This function converts the on-wire m0_io_indexvec to in-mem m0_indexvec,
 * with index and count values appropriately block shifted.
 * If current operation is `write', it also adds the BE-credit to fom_tx_credit
 */
static void stob_be_credit(struct m0_fom *fom, bool is_op_write)
{

	int			 rc;
	struct m0_io_fom_cob_rw	*fom_obj;
	struct m0_stob_domain	*fom_stdom;

	M0_PRE(fom != NULL);
        M0_PRE(m0_is_io_fop(fom->fo_fop));

	fom_obj = container_of(fom, struct m0_io_fom_cob_rw, fcrw_gen);
	M0_ASSERT(m0_io_fom_cob_rw_invariant(fom_obj));

	if (M0_FI_ENABLED("no_write_credit"))
		return;

	rc = stob_object_find(fom);
	M0_ASSERT(rc == 0);
	fom_stdom = m0_stob_dom_get(fom_obj->fcrw_stob);
	M0_ASSERT(fom_stdom != NULL);

	indexvec_wire2mem(fom, m0_stob_block_shift(fom_obj->fcrw_stob));

	if (is_op_write)
		m0_stob_write_credit(fom_stdom, &fom_obj->fcrw_ivec,
				     m0_fom_tx_credit(fom));

	m0_stob_put(fom_obj->fcrw_stob);
}


/**
 * State Transition function for I/O operation that executes
 * on data server.
 *
 * @param fom instance file operation machine under execution
 *
 * @pre fom != NULL
 */
static int m0_io_fom_cob_rw_tick(struct m0_fom *fom)
{
	int                                       rc = 0;
	struct m0_io_fom_cob_rw                  *fom_obj;
	struct m0_io_fom_cob_rw_state_transition  st = { M0_FOPH_FAILURE, NULL,
							 M0_FOPH_FAILURE,
							 M0_FOPH_FAILURE};
	struct m0_poolmach                       *poolmach;
	struct m0_reqh                           *reqh;
	struct m0_fop_cob_rw                     *rwfop;
	struct m0_fop_cob_rw_reply               *rwrep;

	M0_PRE(fom != NULL);
	M0_PRE(m0_is_io_fop(fom->fo_fop));

	fom_obj = container_of(fom, struct m0_io_fom_cob_rw, fcrw_gen);
	M0_ASSERT(m0_io_fom_cob_rw_invariant(fom_obj));

	reqh = m0_fom_reqh(fom);
	poolmach = m0_ios_poolmach_get(reqh);

	/* first handle generic phase */
        if (m0_fom_phase(fom) < M0_FOPH_NR) {
		if (m0_fom_phase(fom) == M0_FOPH_TXN_OPEN)
			stob_be_credit(fom, m0_is_write_fop(fom->fo_fop));
                return m0_fom_tick_generic(fom);
	}

	st = m0_is_read_fop(fom->fo_fop) ?
		io_fom_read_st[m0_fom_phase(fom)] :
		io_fom_write_st[m0_fom_phase(fom)];

	rc = (*st.fcrw_st_state_function)(fom);
	M0_ASSERT(rc == M0_FSO_AGAIN || rc == M0_FSO_WAIT);

	M0_ASSERT(m0_io_fom_cob_rw_invariant(fom_obj));

	/* Set operation status in reply fop if FOM ends.*/
        if (m0_fom_phase(fom) == M0_FOPH_SUCCESS ||
            m0_fom_phase(fom) == M0_FOPH_FAILURE) {
		rwfop = io_rw_get(fom->fo_fop);
		rwrep = io_rw_rep_get(fom->fo_rep_fop);
		rwrep->rwr_rc    = m0_fom_rc(fom);
		rwrep->rwr_count = fom_obj->fcrw_count << fom_obj->fcrw_bshift;
		m0_ios_poolmach_version_updates_pack(poolmach,
						     &rwfop->crw_version,
						     &rwrep->rwr_fv_version,
						     &rwrep->rwr_fv_updates);
		return rc;
	}

	m0_fom_phase_set(fom, rc == M0_FSO_AGAIN ?
				st.fcrw_st_next_phase_again :
				st.fcrw_st_next_phase_wait);
	M0_ASSERT(m0_fom_phase(fom) > M0_FOPH_NR &&
		  m0_fom_phase(fom) <= M0_FOPH_IO_BUFFER_RELEASE);

	return rc;
}

/**
 * Finalise of I/O file operation machine.
 * This is the right place to free all resources acquired by FOM
 *
 * @param fom instance file operation machine under execution
 *
 * @pre fom != NULL
 */
static void m0_io_fom_cob_rw_fini(struct m0_fom *fom)
{
	uint32_t		  colour;
	struct m0_fop		  *fop;
	struct m0_io_fom_cob_rw	  *fom_obj;
	struct m0_reqh_io_service *serv_obj;
	struct m0_net_buffer	  *nb;
	struct m0_net_transfer_mc *tm;
	struct m0_addb_io_stats   *stats;
        struct m0_stob_io_desc    *stio_desc;

	M0_PRE(fom != NULL);

	fom_obj = container_of(fom, struct m0_io_fom_cob_rw, fcrw_gen);
        M0_ASSERT(m0_io_fom_cob_rw_invariant(fom_obj));
	serv_obj = container_of(fom->fo_service, struct m0_reqh_io_service,
				rios_gen);
	M0_ASSERT(m0_reqh_io_service_invariant(serv_obj));

	fop = fom->fo_fop;
	M0_LOG(M0_DEBUG, "FOM finished : operation=%s, nbytes=%lx.",
	       m0_is_read_fop(fop) ? "READ" : "WRITE", fom_obj->fcrw_count);

	tm     = io_fop_tm_get(fop);
	colour = m0_net_tm_colour_get(tm);

	if (fom_obj->fcrw_bp != NULL) {
		M0_INVARIANT_EX(m0_tlist_invariant(&netbufs_tl,
						   &fom_obj->fcrw_netbuf_list));
		m0_net_buffer_pool_lock(fom_obj->fcrw_bp);
		m0_tl_for (netbufs, &fom_obj->fcrw_netbuf_list, nb) {
			m0_net_buffer_pool_put(fom_obj->fcrw_bp, nb, colour);
			netbufs_tlink_del_fini(nb);
		} m0_tl_endfor;
		m0_net_buffer_pool_unlock(fom_obj->fcrw_bp);
		netbufs_tlist_fini(&fom_obj->fcrw_netbuf_list);
	}

	m0_tl_teardown(stobio, &fom_obj->fcrw_done_list, stio_desc)
		stio_desc_fini(stio_desc);

	if (fom_obj->fcrw_ivec.iv_vec.v_nr > 0)
		m0_indexvec_free(&fom_obj->fcrw_ivec);

	stobio_tlist_fini(&fom_obj->fcrw_done_list);
	stobio_tlist_fini(&fom_obj->fcrw_stio_list);

	M0_FOM_ADDB_POST(fom, &fom->fo_service->rs_reqh->rh_addb_mc,
			 &m0_addb_rt_ios_rwfom_finish,
			 m0_fom_rc(fom), fom_obj->fcrw_count,
			 m0_time_sub(m0_time_now(),
				      fom_obj->fcrw_fom_start_time));

	stats = &serv_obj->rios_rwfom_stats[m0_is_read_fop(fop)? 0 : 1];
	m0_addb_counter_update(&stats->ais_times_cntr, (uint64_t)
			       m0_time_sub(m0_time_now(),
					   fom_obj->fcrw_fom_start_time) >> 10);
	m0_addb_counter_update(&stats->ais_sizes_cntr,
			       (uint64_t) fom_obj->fcrw_count);

	m0_fom_fini(fom);
	m0_free(fom_obj);
}

/**
 * Get locality of file operation machine.
 *
 * @param fom instance file operation machine under execution
 *
 * @pre fom != NULL
 * @pre fom->fo_fop != NULL
 */
static size_t m0_io_fom_cob_rw_locality_get(const struct m0_fom *fom)
{
	struct m0_fop_cob_rw *rw;

	M0_PRE(fom != NULL);
	M0_PRE(fom->fo_fop != NULL);

	rw = io_rw_get(fom->fo_fop);
	return rw->crw_fid.f_container;
}

/**
 *
 */
static void m0_io_fom_cob_rw_addb_init(struct m0_fom *fom,
				       struct m0_addb_mc *mc)
{
        struct m0_fop_cob_rw *rwfop;
	uint8_t              *cntr_data;
	int                   trans_size = m0_addb_sm_counter_data_size(
					   &m0_addb_rt_ios_io_fom_phase_stats);

	rwfop = io_rw_get(fom->fo_fop);
	M0_ADDB_CTX_INIT(mc, &fom->fo_addb_ctx, &m0_addb_ct_cob_io_rw_fom,
			 &fom->fo_service->rs_addb_ctx,
			 rwfop->crw_fid.f_container, rwfop->crw_fid.f_key,
			 rwfop->crw_desc.id_nr, rwfop->crw_flags);
	m0_fom_op_addb_ctx_import(fom, &rwfop->crw_addb_ctx_id);

	if (M0_FI_ENABLED("skip_counter_alloc"))
		return;
	IOS_ALLOC_ARR(cntr_data, trans_size,
		      &m0_ios_addb_ctx, FOM_COB_RW_ADDB_INIT);
	if (cntr_data != NULL)
		m0_addb_sm_counter_init(&fom->fo_sm_phase_stats,
					&m0_addb_rt_ios_io_fom_phase_stats,
					cntr_data, trans_size);
}

/**
 * Returns service name which executes this fom.
 *
 * @param fom instance file operation machine under execution
 *
 * @pre fom != NULL
 * @pre fom->fo_fop != NULL
 */
M0_INTERNAL const char *m0_io_fom_cob_rw_service_name(struct m0_fom *fom)
{
	M0_PRE(fom != NULL);
	M0_PRE(fom->fo_fop != NULL);

	return IOSERVICE_NAME;
}

static inline struct m0_addb_mc *fom_to_addb_mc(const struct m0_fom *fom)
{
	return &fom->fo_service->rs_reqh->rh_addb_mc;
}

#undef M0_TRACE_SUBSYSTEM

/** @} end of io_foms */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
