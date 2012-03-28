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
 * Original author: Anand Vidwansa <Anand_Vidwansa@xyratex.com>
 *                  Anup Barve <Anup_Barve@xyratex.com>
 * Original creation date: 03/21/2011
 * Revision       : Rajanikant Chirmade <Rajanikant_Chirmade@xyratex.com>
 * Revision date  : 09/14/2011
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "fop/fop.h"
#include "fop/fop_format.h"
#include "ioservice/io_foms.h"
#include "ioservice/io_fops.h"
#include "stob/stob.h"
#include "lib/errno.h"
#include "lib/memory.h"
#include "net/net.h"
#include "net/net_internal.h"
#include "fid/fid.h"
#include "reqh/reqh.h"
#include "reqh/reqh_service.h"
#include "net/buffer_pool.h"
#include "ioservice/io_service.h"
#include "lib/tlist.h"
#include "lib/assert.h"
#include "addb/addb.h"
#include "colibri/colibri_setup.h"
#include "stob/linux.h"

#ifdef __KERNEL__
#include "ioservice/linux_kernel/io_fops_k.h"
#else
#include "ioservice/io_fops_u.h"
#endif

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

   - <b>Bulk I/O Service</b> Colibri ioservice which process read/write FOPs.
   - <b>FOP</b> File operation packet, a description of file operation
     suitable for sending over network or storing on a storage device.
     File operation packet (FOP) identifies file operation type and operation
     parameters.
   - <b>FOM</b> FOP state machine (FOM) is a state machine that represents
     current state of the FOP's execution on a node. FOM is associated with
     the particular FOP and implicitly includes this FOP as part of its state.
   - <b>zero-copy</b> Copy between a source and destination takes place without
     any intermediate copies to staging areas.
   - <b>STOB</b> Storage object (STOB) is a basic C2 data structure containing
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
   - <b>r.non-blocking.few-threads</b> Colibri service should use a relatively
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

   Bulk I/O Service implements I/O FOMs to process I/O FOP @ref io_foms.

   - Bulk read FOM process FOP of type c2_fop_cob_readv
   - Bulk write FOM process FOP of type c2_fop_cob_writev

   The Bulk I/O Service interface c2_ioservice_fop_init() registers and
   initiates I/O FOPs with it. Following are the Bulk I/O Service FOP type.

   - c2_fop_cob_readv,
   - c2_fop_cob_writev,
   - c2_fop_cob_readv_rep,
   - c2_fop_cob_writev_rep

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

   a->c  [ label = "Write FOP (c2_net_bufs_desc list, indexvecs list)"];
   c=>b  [ label = "Get network buffer from buffer_pool"];
   b>>c  [ label = "Got network buffer (c2_net_bufs)"];
   c=>b  [ label = "Initiates zero-copy (c2_net_bufs, c2_net_bufs_desc)" ];
   at->b [ label = "Data transfer using RDMA" ];
   at->b [ label = "Data transfer using RDMA" ];
   ...;
   at->b [ label = "Data transfer using RDMA" ];
   b=>>c [ label = "Zero-copy finish"];
   c=>d  [ label = "Initiates STOB I/O" ];
   d=>>c [ label = "STOB I/O completes"];
   c=>b  [ label = "Return back buffer to buffer_pool (c2_net_bufs)"];
   b>>c  [ label = "Network buffers released"];
   c->a  [ label = "Reply FOP (status)" ];

   @endmsc

   - Client sends write FOP to server. Write FOP contains the network buffer
     descriptor list and indexvecs list instead of actual data.
   - To process write FOP, request handler creates & initiates write FOM and put
     it into run queue for execution.
   - State transition function go through generic and extended phases
     @ref c2_io_fom_cob_rw_phases defined for I/O FOM (write FOM).
         - Gets as many buffers as it can from buffer_pool to transfer
           data for all descriptors. If there are insufficient buffers
           with buffer_pool to process all descriptors then its goes by
           batch by batch. Atleast one buffer needed to start bulk
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

   a->c  [ label = "Read FOP (c2_net_bufs_desc)"];
   c=>b  [ label = "Get network buffer from buffer_pool"];
   b>>c  [ label = "Got network buffer (c2_net_bufs)"];
   c=>d  [ label = "Initiates STOB I/O" ];
   d=>>c [ label = "STOB I/O completes"];
   c=>b  [ label = "Initiates zero-copy (c2_net_bufs, c2_net_bufs_desc)" ];
   b->at [ label = "Data transfer using RDMA" ];
   b->at [ label = "Data transfer using RDMA" ];
   ...;
   b->at [ label = "Data transfer using RDMA" ];
   b=>>c [ label = "Zero-copy finish"];
   c=>b  [ label = "Returns back network buffer  (c2_net_bufs)"];
   b>>c  [ label = "Network buffer released"];
   c->a  [ label = "Reply FOP (status)" ];

   @endmsc

   - Client sends read FOP to server. Read FOP contains the network buffer
     descriptor list and indexvecs list instead of actual data.
   - To process read FOP, request handler creates & initiates read FOM and puts
     it into run queue for execution.
   - State transition function go through generic and extended phases
     @ref c2_io_fom_readv_phases defined for read FOM.
         - Gets as many buffers as it can from buffer_pool to transfer
           data for all descriptors. If there are insufficient buffers
           with buffer_pool to process all descriptors then its goes by
           batch by batch. Atleast one buffer needed to start bulk
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

   On the basis of steps involved in these operations enumeration called
   @ref c2_io_fom_cob_rw_phases will be defined, that extends the standard
   FOM phases (enum c2_fom_phase) with new phases to handle the state machine
   that sets up and executes read/write operations  respectively involving
   bulk I/O.

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
       S2 -> S3 [label="Buffer not available"]
       S3 -> S3 [label="Buffer not available"]
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
       S2 -> S3 [label="Buffer not available"]
       S3 -> S3 [label="Buffer not available"]
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

   I/O service maintains c2_buf_pool instance with data structure
   c2_reqh_service. Buffer pool c2_reqh_service::c2_buf_pool will be initialized
   in Bulk I/O Service start operation vector c2_io_service_start(). Bulk I/O
   service will use c2_buf_pool_init() to allocate and register specified
   number of network buffers and with specified size.

   Bulk I/O Service needs following parameters from configuration database to
   initialize buffer pool -

   IO_BULK_BUFFER_POOL_SIZE Number of network buffers in buffer pool.
   IO_BULK_BUFFER_SIZE Size of each network buffer.
   IO_BULK_BUFFER_NUM_SEGMENTS Number of segments in each buffer.

   Buffer pool de-allocation takes place in service operation vector
   c2_io_service_stop(). I/O service will use c2_buf_pool_fini() to de-allocate
   & de-register the network buffers.

   The buffer pool for bulk data transfer is private to the Bulk I/O service and
   is shared by all FOM instances executed by the service.

   - Buffer Acquire

   Bulk I/O Servers acquire the network buffer by calling buffer_pool interface
   c2_buf_pool_get(). If buffer available with buffer_pool then this function
   returns network buffer. And if buffer_pool empty the function returns NULL.
   Then FOM need to wait for _notEmpty signal from buffer_pool.

   Bulk I/O Service needs to get lock on buffer_pool instance while its request
   network buffer. And release lock after it get network buffer.

   - Buffer Release

   Bulk I/O Servers release the network buffer by calling buffer_pool interface
   c2_buf_pool_put(). It return back network buffer to  buffer_pool.

   Bulk I/O Service needs to get lock on buffer_pool instance while it request
   network buffer. And release lock after it get network buffer.

   - Buffer Pool Expansion
   @todo
   If buffer_pool reached to low threshold, Bulk I/O service may expand pool
   size. This can be done later to minimize waiting time for network buffer.

   @subsection DLD-bulk-server-lspec-service-registration Service Registration

   - Service Type Declaration

   Request handler provides the macro C2_REQH_SERVICE_TYPE_DECLARE to declare
   service type. Bulk I/O Service use this macro to declare service type as
   follows -

   C2_REQH_SERVICE_TYPE_DECLARE(c2_io_service_type, &c2_io_service_type_ops,
                                "ioservice");

   It's also assign service name and service type operations for Bulk I/O
   Service.

   - Service Type Registration

   Bulk I/O Service registers its service type with request handler using
   interface c2_reqh_service_type_register(). This function registers service
   type with global service type list for request handler. Service type
   operation c2_ioservice_alloc_and_init() will do this registration.

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

   - Test 01 : Call function c2_io_fom_cob_rw_create()<br>
               Input           : Read FOP (in-memory data structure c2_fop)<br>
               Expected Output : Create FOM of corresponding FOP type.

   - Test 02 : Call function c2_io_fom_cob_rw_create()<br>
               Input           : Write FOP (in-memory data structure c2_fop)<br>
               Expected Output : Create FOM of corresponding FOP type.

   - Test 03 : Call function c2_io_fom_cob_rw_init()<br>
               Input           : Read FOP (in-memory data structure c2_fop)<br>
               Expected Output : Initiates FOM with corresponding operation
                                 vectors and other pointers.
   - Test 04 : Call function c2_io_fom_cob_rw_init()<br>
               Input           : Write FOP (in-memory data structure c2_fop)<br>
               Expected Output : Initiates FOM with corresponding operation
                                 vectors and other pointers.

   - Test 05 : Call c2_io_fom_cob_rw_state() with buffer pool size 1<br>
               Input : Read FOM with current phase
                       FOPH_IO_FOM_BUFFER_ACQUIRE<br>
               Expected Output : Gets network buffer and pointer set into FOM
                                 with phase changed to FOPH_IO_STOB_INIT and
                                 return value FSO_AGAIN.

   - Test 06 : Call c2_io_fom_cob_rw_state() with buffer pool size 0
               (empty buffer_pool)<br>
               Input : Read FOM with current phase
                       FOPH_IO_FOM_BUFFER_ACQUIRE<br>
               Expected Output : Should not gets network buffer and NULL pointer
                                 set into FOM with phase changed to
                                 FOPH_IO_FOM_BUFFER_WAIT and return
                                 value FSO_WAIT.

   - Test 07 : Call c2_io_fom_cob_rw_state() with buffer pool size 0
               (empty buffer_pool)<br>
               Input : Read FOM with current phase
                       FOPH_IO_FOM_BUFFER_WAIT<br>
               Expected Output : Should not gets network buffer and NULL pointer
                                 set into FOM with phase not changed and return
                                 value FSO_WAIT.

   - Test 08 : Call c2_io_fom_cob_rw_state()<br>
               Input : Read FOM with current phase FOPH_IO_STOB_INIT<br>
               Expected Output : Initiates STOB read with phase changed to
                                 FOPH_IO_STOB_WAIT and return value FSO_WAIT.

   - Test 09 : Call c2_io_fom_cob_rw_state()<br>
               Input : Read FOM with current phase FOPH_IO_ZERO_COPY_INIT<br>
               Expected Output : Initiates zero-copy with phase changed to
                                 FOPH_IO_ZERO_COPY_WAIT return value FSO_WAIT.

   - Test 10 : Call c2_io_fom_cob_rw_state() with buffer pool size 1<br>
               Input : Write FOM with current phase
                       FOPH_IO_FOM_BUFFER_ACQUIRE<br>
               Expected Output : Gets network buffer and pointer set into FOM
                                 with phase changed to
                                 FOPH_IO_ZERO_COPY_INIT and return value
                                 FSO_AGAIN.

   - Test 11 : Call function c2_io_fom_cob_rw_fini()<br>
               Input : Read FOM<br>
               Expected Output : Should de-allocate FOM.

   - Test 12 : Call c2_io_fom_cob_rw_state()<br>
               Input : Read FOM with invalid STOB id and current phase
                       FOPH_IO_STOB_INIT.<br>
               Expected Output : Should return error.

   - Test 13 : Call c2_io_fom_cob_rw_state()<br>
               Input : Read FOM with current phase FOPH_IO_ZERO_COPY_INIT
                       and wrong network buffer descriptor.<br>
               Expected Output : Should return error.

   - Test 14 : Call c2_io_fom_cob_rw_state()<br>
               Input : Read FOM with current phase FOPH_IO_STOB_WAIT with
                       result code of stob I/O c2_fom::c2_stob_io::si_rc set
                       to I/O error.<br>
               Expected Output : Should return error FOS_FAILURE and I/O error
                                 set in relay FOP.

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
     lots of request handler threads if buffers not available.
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

C2_TL_DESCR_DEFINE(stobio, "STOB I/O", static, struct c2_stob_io_desc,
                   siod_linkage,  siod_magic,
                   C2_STOB_IO_DESC_LINK_MAGIC,  C2_STOB_IO_DESC_HEAD_MAGIC);
C2_TL_DEFINE(stobio, static, struct c2_stob_io_desc);

C2_TL_DESCR_DEFINE(netbufs, "Aquired net buffers", static,
		   struct c2_net_buffer, nb_ioservice_linkage, nb_magic,
		   C2_NET_BUFFER_LINK_MAGIC, C2_NET_BUFFER_HEAD_MAGIC_IOFOM);
C2_TL_DEFINE(netbufs, static, struct c2_net_buffer);

C2_TL_DESCR_DEFINE(rpcbulkbufs, "rpc bulk buffers", static,
		   struct c2_rpc_bulk_buf, bb_link, bb_magic,
		   C2_RPC_BULK_BUF_MAGIC, C2_RPC_BULK_MAGIC);
C2_TL_DEFINE(rpcbulkbufs, static, struct c2_rpc_bulk_buf);

/**
 * @todo
 * This is stuff function.
 * This function will be available as pert of "netprov" task.
 * This stuff should removed once netprov merged into master.
 */
static uint32_t c2_net_tm_colour_get(struct c2_net_transfer_mc *tm)
{
        uint32_t colour;
        C2_PRE(c2_net__tm_invariant(tm));
        colour = tm->ntm_pool_colour;
        return colour;
}


/**
 * I/O FOM addb event location object
 */
const struct c2_addb_loc io_fom_addb_loc = {
        .al_name = "io_fom"
};

extern const struct c2_tl_descr bufferpools_tl;

extern bool c2_is_read_fop(const struct c2_fop *fop);
extern bool c2_is_write_fop(const struct c2_fop *fop);
extern bool c2_is_io_fop(const struct c2_fop *fop);
extern struct c2_fop_cob_rw *io_rw_get(struct c2_fop *fop);
extern struct c2_fop_cob_rw_reply *io_rw_rep_get(struct c2_fop *fop);
extern bool c2_is_cob_create_fop(const struct c2_fop *fop);
extern bool c2_is_cob_delete_fop(const struct c2_fop *fop);

static int c2_io_fom_cob_rw_create(struct c2_fop *fop, struct c2_fom **m);
int c2_io_fom_cob_rw_init(struct c2_fop *fop, struct c2_fom **out);
static int c2_io_fom_cob_rw_state(struct c2_fom *fom);
static void c2_io_fom_cob_rw_fini(struct c2_fom *fom);
static size_t c2_io_fom_cob_rw_locality_get(const struct c2_fom *fom);
const char *c2_io_fom_cob_rw_service_name (struct c2_fom *fom);
static bool c2_io_fom_cob_rw_invariant(const struct c2_io_fom_cob_rw *io);

static int io_fom_cob_rw_acquire_net_buffer(struct c2_fom *);
static int io_fom_cob_rw_io_launch(struct c2_fom *);
static int io_fom_cob_rw_io_finish(struct c2_fom *);
static int io_fom_cob_rw_initiate_zero_copy(struct c2_fom *);
static int io_fom_cob_rw_zero_copy_finish(struct c2_fom *);
static int io_fom_cob_rw_release_net_buffer(struct c2_fom *);

/**
 * I/O FOM operation vector.
 */
static const struct c2_fom_ops c2_io_fom_cob_rw_ops = {
	.fo_fini = c2_io_fom_cob_rw_fini,
	.fo_state = c2_io_fom_cob_rw_state,
	.fo_home_locality = c2_io_fom_cob_rw_locality_get,
        .fo_service_name = c2_io_fom_cob_rw_service_name,
};

/**
 * I/O FOM type operation vector.
 */
static const struct c2_fom_type_ops c2_io_cob_rw_type_ops = {
	.fto_create = c2_io_fom_cob_rw_create,
};

/**
 * I/O FOM type operation.
 */
const struct c2_fom_type c2_io_fom_cob_rw_mopt = {
	.ft_ops = &c2_io_cob_rw_type_ops,
};

/**
 * I/O Read FOM state transition table.
 * @see DLD-bulk-server-lspec-state
 */
static struct c2_io_fom_cob_rw_state_transition io_fom_read_st[] = {

[FOPH_IO_FOM_BUFFER_ACQUIRE] =
{ FOPH_IO_FOM_BUFFER_ACQUIRE, &io_fom_cob_rw_acquire_net_buffer,
  FOPH_IO_STOB_INIT, FOPH_IO_FOM_BUFFER_WAIT, "Network buffer acquire", },

[FOPH_IO_FOM_BUFFER_WAIT] =
{ FOPH_IO_FOM_BUFFER_WAIT, &io_fom_cob_rw_acquire_net_buffer,
  FOPH_IO_STOB_INIT,  FOPH_IO_FOM_BUFFER_WAIT, "Network buffer wait", },

[FOPH_IO_STOB_INIT] =
{ FOPH_IO_STOB_INIT, &io_fom_cob_rw_io_launch,
  0,  FOPH_IO_STOB_WAIT, "STOB I/O launch", },

[FOPH_IO_STOB_WAIT] =
{ FOPH_IO_STOB_WAIT, &io_fom_cob_rw_io_finish,
  FOPH_IO_ZERO_COPY_INIT, 0, "STOB I/O finish", },

[FOPH_IO_ZERO_COPY_INIT] =
{ FOPH_IO_ZERO_COPY_INIT, &io_fom_cob_rw_initiate_zero_copy,
  0, FOPH_IO_ZERO_COPY_WAIT, "Zero-copy initiate", },

[FOPH_IO_ZERO_COPY_WAIT] =
{ FOPH_IO_ZERO_COPY_WAIT, &io_fom_cob_rw_zero_copy_finish,
  FOPH_IO_BUFFER_RELEASE, 0, "Zero-copy finish", },

[FOPH_IO_BUFFER_RELEASE] =
{ FOPH_IO_BUFFER_RELEASE, &io_fom_cob_rw_release_net_buffer,
  FOPH_IO_FOM_BUFFER_ACQUIRE,  0, "Network buffer release", },
};

/**
 * I/O Write FOM state transition table.
 * @see DLD-bulk-server-lspec-state
 */
static struct c2_io_fom_cob_rw_state_transition io_fom_write_st[] = {

[FOPH_IO_FOM_BUFFER_ACQUIRE] =
{ FOPH_IO_FOM_BUFFER_ACQUIRE, &io_fom_cob_rw_acquire_net_buffer,
  FOPH_IO_ZERO_COPY_INIT, FOPH_IO_FOM_BUFFER_WAIT, "Network buffer acquire", },

[FOPH_IO_FOM_BUFFER_WAIT] =
{ FOPH_IO_FOM_BUFFER_WAIT, &io_fom_cob_rw_acquire_net_buffer,
  FOPH_IO_ZERO_COPY_INIT, FOPH_IO_FOM_BUFFER_WAIT, "Network buffer wait", },

[FOPH_IO_ZERO_COPY_INIT] =
{ FOPH_IO_ZERO_COPY_INIT, &io_fom_cob_rw_initiate_zero_copy,
  0, FOPH_IO_ZERO_COPY_WAIT, "Zero-copy initiate", },

[FOPH_IO_ZERO_COPY_WAIT] =
{ FOPH_IO_ZERO_COPY_WAIT, &io_fom_cob_rw_zero_copy_finish,
  FOPH_IO_STOB_INIT, 0, "Zero-copy finish", },

[FOPH_IO_STOB_INIT] =
{ FOPH_IO_STOB_INIT, &io_fom_cob_rw_io_launch,
  0, FOPH_IO_STOB_WAIT, "STOB I/O launch", },

[FOPH_IO_STOB_WAIT] =
{ FOPH_IO_STOB_WAIT, &io_fom_cob_rw_io_finish,
  FOPH_IO_BUFFER_RELEASE, 0, "STOB I/O finish", },

[FOPH_IO_BUFFER_RELEASE] =
{ FOPH_IO_BUFFER_RELEASE, &io_fom_cob_rw_release_net_buffer,
  FOPH_IO_FOM_BUFFER_ACQUIRE, 0, "Network buffer release", },
};

static bool c2_io_fom_cob_rw_invariant(const struct c2_io_fom_cob_rw *io)
{
        int                      acquired_net_buffs = 0;
        struct c2_fop_cob_rw    *rwfop;

        if (io == NULL)
                return false;

        rwfop = io_rw_get(io->fcrw_gen.fo_fop);
        if (io->fcrw_ndesc != rwfop->crw_desc.id_nr)
                return false;

        if (io->fcrw_curr_desc_index < 0 ||
            io->fcrw_curr_desc_index > rwfop->crw_desc.id_nr)
                return false;

        if (io->fcrw_curr_ivec_index < 0 ||
            io->fcrw_curr_ivec_index > rwfop->crw_ivecs.cis_nr)
                return false;

        if (!c2_tlist_invariant(&netbufs_tl, &io->fcrw_netbuf_list))
                return false;

        acquired_net_buffs = netbufs_tlist_length(&io->fcrw_netbuf_list);
        if (io->fcrw_batch_size != acquired_net_buffs)
                return false;

        return true;
}

static bool c2_stob_io_desc_invariant(const struct c2_stob_io_desc *stobio_desc)
{
        if (stobio_desc->siod_magic != C2_STOB_IO_DESC_LINK_MAGIC)
                return false;

        return true;
}

static bool c2_reqh_io_service_invariant(const struct c2_reqh_io_service *rios)
{
        if (rios->rios_magic != C2_REQH_IO_SERVICE_MAGIC)
                return false;

        return true;
}
/**
 * Call back function which gets invoked on a single STOB I/O complete.
 * This function check for STOB I/O list and remove stobio entry from
 * list for completed STOB I/O. After completion of all STOB I/O it
 * sends signal to FOM so that it can again put into run queue.
 *
 * @param clink clink for completed STOB I/O entry
 */

static bool io_fom_cob_rw_stobio_complete_cb(struct c2_clink *clink)
{
        struct c2_fop            *fop = NULL;
        struct c2_fom            *fom = NULL;
        struct c2_stob_io        *stio;
        struct c2_io_fom_cob_rw  *fom_obj;
        struct c2_stob_io_desc   *stio_desc;

        stio_desc = container_of(clink, struct c2_stob_io_desc, siod_clink);
        C2_ASSERT(c2_stob_io_desc_invariant(stio_desc));

        fom_obj = stio_desc->siod_fom;
        C2_ASSERT(c2_io_fom_cob_rw_invariant(fom_obj));

        stio    = &stio_desc->siod_stob_io;
        fop     = fom_obj->fcrw_gen.fo_fop;
        fom     = &fom_obj->fcrw_gen;

        /* Update transfered data count till no error in FOM execution. */
        if (fom->fo_rc == 0) {

                /*
                 * If stob I/O failed, declared FOM as failed and assign
                 * STOB I/O error code to FOM return code.
                 */
                if (stio->si_rc == 0) {
		        if (c2_is_write_fop(fop)) {
                                int rc;
			        /*
                                 * Make an FOL transaction record.
                                 * @todo : Need to consider FOL failure.
                                 */
			          rc = c2_fop_fol_rec_add(fop, fom->fo_fol,
						          &fom->fo_tx.tx_dbtx);
				  fom->fo_rc = rc;
                          }
                          /* Update successfull data transfered count*/
		          fom_obj->fcrw_bytes_transfered += stio->si_count;
                } else {
                        fom->fo_rc = stio->si_rc;
                        fom->fo_phase = FOPH_FAILURE;
                }

        }
        c2_mutex_lock(&fom_obj->fcrw_stio_mutex);
        fom_obj->fcrw_num_stobio_launched--;

        if (fom_obj->fcrw_num_stobio_launched == 0) {
                c2_mutex_unlock(&fom_obj->fcrw_stio_mutex);
                c2_chan_signal(&fom_obj->fcrw_wait);
                return true;
        }
        c2_mutex_unlock(&fom_obj->fcrw_stio_mutex);

        return true;
};

/**
 * Function to map given fid to corresponding Component object id(in turn,
 * storage object id).
 * Currently, this mapping is identity. But it is subject to
 * change as per the future requirements.
 *
 * @param in file identifier
 * @param out corresponding STOB identifier
 *
 * @pre in != NULL
 * @pre out != NULL
 */
void io_fom_cob_rw_fid2stob_map(const struct c2_fid *in,
                                struct c2_stob_id *out)
{
        C2_PRE(in != NULL);
        C2_PRE(out != NULL);

	out->si_bits.u_hi = in->f_container;
	out->si_bits.u_lo = in->f_key;
}

/**
 * Function to map the on-wire FOP format to in-core FOP format.
 *
 * @param in file identifier wire format
 * @param out file identifier memory format
 *
 * @pre in != NULL
 * @pre out != NULL
 */
void io_fom_cob_rw_fid_wire2mem(struct c2_fop_file_fid *in,
                                struct c2_fid *out)
{
        C2_PRE(in != NULL);
        C2_PRE(out != NULL);

	out->f_container = in->f_seq;
	out->f_key = in->f_oid;
}

/**
 * Function to convert the on-wire indexvec to in-memory indexvec format.
 * Since c2_io_indexvec (on-wire structure) and c2_indexvec (in-memory
 * structure different, it needs conversion.
 * c2_indexvec pointer from c2_stob_io so memory allocated for this
 * should de-allocated before c2_stio_fini() when respective STOB I/O
 * completes.
 *
 * @param fom file operation machine instance.
 * @param in indexvec wire format
 * @param out indexvec memory format
 * @param bshift shift value for current stob to align index vecs.
 *
 * @pre in != NULL
 * @pre out != NULL
 */
static int  io_fom_cob_rw_indexvec_wire2mem(struct c2_fom         *fom,
                                            struct c2_io_indexvec *in,
                                            struct c2_indexvec    *out,
                                            uint32_t               bshift)
{
        int         i;

        C2_PRE(fom != NULL);
        C2_PRE(in != NULL);
        C2_PRE(out != NULL);

        /*
         * This memory will be freed after its container stob
         * completes and before destructing container stob object.
         */
        C2_ALLOC_ARR(out->iv_vec.v_count, in->ci_nr);
        if (out->iv_vec.v_count == NULL) {
                C2_ADDB_ADD(&fom->fo_fop->f_addb, &io_fom_addb_loc,
                            c2_addb_oom);
                return -ENOMEM;
        }
        C2_ALLOC_ARR(out->iv_index, in->ci_nr);
        if (out->iv_index == NULL) {
                C2_ADDB_ADD(&fom->fo_fop->f_addb, &io_fom_addb_loc,
                            c2_addb_oom);
                c2_free(out->iv_vec.v_count);
                return -ENOMEM;
        }

        out->iv_vec.v_nr = in->ci_nr;
        for (i = 0; i < in->ci_nr; i++) {
                out->iv_index[i] = in->ci_iosegs[i].ci_index >> bshift;
                out->iv_vec.v_count[i] = in->ci_iosegs[i].ci_count >> bshift;
        }

        return 0;
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
static int io_fom_cob_rw_align_bufvec (struct c2_fom    *fom,
                                       struct c2_bufvec *obuf,
                                       struct c2_bufvec *ibuf,
                                       c2_bcount_t       ivec_count,
                                       uint32_t          bshift)
{
        int                rc = 0;
        int                i = 0;
        c2_bcount_t        bufvec_count = 0;
        int                bufvec_seg_size = 0;

        C2_PRE(fom != NULL);
        C2_PRE(obuf != NULL);
        C2_PRE(ibuf != NULL);

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
                       ((ivec_count % bufvec_seg_size) == 0 ? 0:1);

        C2_ALLOC_ARR(obuf->ov_vec.v_count, bufvec_count);
        if (obuf->ov_vec.v_count == NULL) {
                rc = -ENOMEM;
                C2_ADDB_ADD(&fom->fo_fop->f_addb, &io_fom_addb_loc,
                            c2_addb_oom);
                return rc;
        }

        C2_ALLOC_ARR(obuf->ov_buf, bufvec_count);
        if (obuf->ov_buf == NULL) {
                rc = -ENOMEM;
                C2_ADDB_ADD(&fom->fo_fop->f_addb, &io_fom_addb_loc,
                            c2_addb_oom);
                c2_free(obuf->ov_vec.v_count);
                return rc;
        }

        obuf->ov_vec.v_nr = bufvec_count;
        /* Align bufvec before copying to bufvec from stob io */
        for (i = 0; i < bufvec_count; i++) {
                obuf->ov_vec.v_count[i] = ibuf->ov_vec.v_count[i] >> bshift;
                obuf->ov_buf[i]         = c2_stob_addr_pack(ibuf->ov_buf[i],
                                                            bshift);
        }

        return rc;
}

/**
 * Create and initiate I/O FOM and return generic struct c2_fom
 * Find the corresponding fom_type and associate it with c2_fom.
 * Associate fop with fom type.
 *
 * @param fop file operation packet need to process
 * @param fom file operation machine need to allocate and initiate
 *
 * @pre fop != NULL
 * @pre m != NULL
 */
int c2_io_fom_cob_rw_create(struct c2_fop *fop, struct c2_fom **out)
{
        int                      rc = 0;
        struct c2_fom           *fom;
        struct c2_io_fom_cob_rw *fom_obj;
        struct c2_fop_cob_rw    *rwfop;

        C2_PRE(fop != NULL);
        C2_PRE(c2_is_io_fop(fop));
        C2_PRE(out != NULL);

        C2_ALLOC_PTR(fom_obj);
        if (fom_obj == NULL) {
                rc = -ENOMEM;
                return rc;
        }

        fom  = &fom_obj->fcrw_gen;
        *out = fom;
        c2_fom_init(fom, &fop->f_type->ft_fom_type,
		    &c2_io_fom_cob_rw_ops, fop,
		    c2_is_read_fop(fop) ?
		    c2_fop_alloc(&c2_fop_cob_readv_rep_fopt, NULL) :
		    c2_fop_alloc(&c2_fop_cob_writev_rep_fopt, NULL));

        if (fom->fo_rep_fop == NULL) {
                c2_fom_fini(fom);
                c2_free(fom_obj);
                rc = -ENOMEM;
                C2_ADDB_ADD(&fom->fo_fop->f_addb, &io_fom_addb_loc,
                            c2_addb_oom);
                return rc;
        }

        fom_obj->fcrw_fom_start_time = c2_time_now();
        fom_obj->fcrw_stob       = NULL;

        rwfop = io_rw_get(fop);

        fom_obj->fcrw_ndesc               = rwfop->crw_desc.id_nr;
        fom_obj->fcrw_curr_desc_index     = 0;
        fom_obj->fcrw_curr_ivec_index     = 0;
        fom_obj->fcrw_batch_size          = 0;
        fom_obj->fcrw_bytes_transfered    = 0;
        fom_obj->fcrw_num_stobio_launched = 0;
        fom_obj->fcrw_bp                  = NULL;

        c2_chan_init(&fom_obj->fcrw_wait);
        netbufs_tlist_init(&fom_obj->fcrw_netbuf_list);
        stobio_tlist_init(&fom_obj->fcrw_stio_list);
        c2_mutex_init(&fom_obj->fcrw_stio_mutex);

        C2_ADDB_ADD(&fom->fo_fop->f_addb, &io_fom_addb_loc, c2_addb_trace,
		    "FOM created : type=rw.");

        return rc;
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
 * @pre fom->fo_phase == FOPH_IO_FOM_BUFFER_ACQUIRE
 */
static int io_fom_cob_rw_acquire_net_buffer(struct c2_fom *fom)
{
        uint32_t                      colour;
        int                           acquired_net_bufs;
        int                           required_net_bufs;
        struct c2_fop                *fop;
        struct c2_io_fom_cob_rw      *fom_obj = NULL;
        struct c2_net_transfer_mc     tm;

        C2_PRE(fom != NULL);
        C2_PRE(c2_is_io_fop(fom->fo_fop));
        C2_PRE(fom->fo_service != NULL);
        C2_PRE(fom->fo_phase == FOPH_IO_FOM_BUFFER_ACQUIRE ||
               fom->fo_phase == FOPH_IO_FOM_BUFFER_WAIT);

        fom_obj = container_of(fom, struct c2_io_fom_cob_rw, fcrw_gen);
        C2_ASSERT(c2_io_fom_cob_rw_invariant(fom_obj));

        fop = fom->fo_fop;
        fom_obj->fcrw_phase_start_time = c2_time_now();

        /**
         * Cache buffer pool pointer with FOM object.
         */
        if (fom_obj->fcrw_bp == NULL) {
                struct c2_reqh_io_service    *serv_obj;
                struct c2_rios_buffer_pool   *bpdesc = NULL;
                struct c2_net_domain         *fop_ndom = NULL;

                serv_obj = container_of(fom->fo_service,
                                        struct c2_reqh_io_service, rios_gen);
                C2_ASSERT(c2_reqh_io_service_invariant(serv_obj));

                /* Get network buffer pool for network domain */
                fop_ndom
                = fop->f_item.ri_session->s_conn->c_rpcmachine->cr_tm.ntm_dom;
                c2_tlist_for(&bufferpools_tl, &serv_obj->rios_buffer_pools,
                             bpdesc) {
                        if (bpdesc->rios_ndom == fop_ndom) {
                                fom_obj->fcrw_bp = &bpdesc->rios_bp;
                                break;
                        }
                } c2_tlist_endfor;
                C2_ASSERT(fom_obj->fcrw_bp != NULL);
        }

        tm = fop->f_item.ri_session->s_conn->c_rpcmachine->cr_tm;
        colour = c2_net_tm_colour_get(&tm);

        acquired_net_bufs = netbufs_tlist_length(&fom_obj->fcrw_netbuf_list);
        required_net_bufs = fom_obj->fcrw_ndesc - fom_obj->fcrw_curr_desc_index;

        /*
         * Aquire as many net buffers as to process all discriptors.
         * If FOM able to acquire more buffers then it change batch size
         * dynamically.
         */
        C2_ASSERT(acquired_net_bufs <= required_net_bufs);
        while (acquired_net_bufs < required_net_bufs) {
            struct c2_net_buffer      *nb = NULL;

            c2_net_buffer_pool_lock(fom_obj->fcrw_bp);
            nb = c2_net_buffer_pool_get(fom_obj->fcrw_bp, colour);

            if (nb == NULL && acquired_net_bufs == 0) {
                    struct c2_rios_buffer_pool   *bpdesc = NULL;
                    /*
                     * Network buffer not available. Atleast one
                     * buffer need for zero-copy. Registers FOM clink
                     * with buffer pool wait channel to get buffer
                     * pool non-empty signal.
                     */
                    bpdesc = container_of(fom_obj->fcrw_bp,
                                        struct c2_rios_buffer_pool, rios_bp);
                    c2_fom_block_at(fom, &bpdesc->rios_bp_wait);

                    fom->fo_phase = FOPH_IO_FOM_BUFFER_WAIT;
                    c2_net_buffer_pool_unlock(fom_obj->fcrw_bp);

                    return  FSO_WAIT;
            } else if (nb == NULL) {
                    c2_net_buffer_pool_unlock(fom_obj->fcrw_bp);
                    /*
                     * Some network buffers are available for zero copy
                     * init. FOM can continue with available buffers.
                     */
                    break;
            }
            c2_net_buffer_pool_unlock(fom_obj->fcrw_bp);

            if (c2_is_read_fop(fop))
                   nb->nb_qtype = C2_NET_QT_ACTIVE_BULK_SEND;
            else
                   nb->nb_qtype = C2_NET_QT_ACTIVE_BULK_RECV;

            C2_ASSERT(c2_tlist_invariant(&netbufs_tl,
                                         &fom_obj->fcrw_netbuf_list));

            netbufs_tlink_init(nb);
            netbufs_tlist_add(&fom_obj->fcrw_netbuf_list, nb);
            acquired_net_bufs++;
        }

        fom_obj->fcrw_batch_size = acquired_net_bufs;

        return FSO_AGAIN;
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
 * @pre fom->fo_phase == FOPH_IO_BUFFER_RELEASE
 */
static int io_fom_cob_rw_release_net_buffer(struct c2_fom *fom)
{
        uint32_t                        colour;
        int                             acquired_net_bufs;
        int                             required_net_bufs;
        struct c2_fop                  *fop;
        struct c2_io_fom_cob_rw        *fom_obj = NULL;
        struct c2_net_transfer_mc       tm;


        C2_PRE(fom != NULL);
        C2_PRE(c2_is_read_fop(fom->fo_fop) || c2_is_write_fop(fom->fo_fop));
        C2_PRE(fom->fo_service != NULL);
        C2_PRE(fom->fo_phase == FOPH_IO_BUFFER_RELEASE);

        fom_obj = container_of(fom, struct c2_io_fom_cob_rw, fcrw_gen);
        C2_ASSERT(c2_io_fom_cob_rw_invariant(fom_obj));
        C2_ASSERT(fom_obj->fcrw_bp != NULL);

        fop    = fom->fo_fop;
        tm     = fop->f_item.ri_session->s_conn->c_rpcmachine->cr_tm;
        colour = c2_net_tm_colour_get(&tm);

        C2_ASSERT(c2_tlist_invariant(&netbufs_tl, &fom_obj->fcrw_netbuf_list));
        acquired_net_bufs = netbufs_tlist_length(&fom_obj->fcrw_netbuf_list);
        required_net_bufs = fom_obj->fcrw_ndesc - fom_obj->fcrw_curr_desc_index;

        c2_net_buffer_pool_lock(fom_obj->fcrw_bp);
        while (acquired_net_bufs > required_net_bufs) {
                struct c2_net_buffer           *nb = NULL;

                nb = netbufs_tlist_tail(&fom_obj->fcrw_netbuf_list);
                C2_ASSERT(nb != NULL);
                c2_net_buffer_pool_put(fom_obj->fcrw_bp, nb, colour);
                netbufs_tlink_del_fini(nb);
                acquired_net_bufs--;
        }
        c2_net_buffer_pool_unlock(fom_obj->fcrw_bp);

        fom_obj->fcrw_batch_size = acquired_net_bufs;

        if (required_net_bufs == 0)
               fom->fo_phase = FOPH_SUCCESS;

        return FSO_AGAIN;
}

/**
 * Initiate zero-copy
 * Initiates zero-copy for batch of descriptors.
 * And wait for zero-copy to complete for all descriptors.
 * Network layer signaled on c2_rpc_bulk::rb_chan on completion.
 *
 * @param fom file operation machine.
 *
 * @pre fom != NULL
 * @pre fom->fo_phase == FOPH_IO_ZERO_COPY_INIT
 */
static int io_fom_cob_rw_initiate_zero_copy(struct c2_fom *fom)
{
        int                        rc = 0;
        struct c2_fop             *fop = fom->fo_fop;
        struct c2_io_fom_cob_rw   *fom_obj = NULL;
        struct c2_fop_cob_rw      *rwfop;
        const struct c2_rpc_item  *rpc_item;
        struct c2_rpc_bulk        *rbulk;
        struct c2_net_buffer      *nb = NULL;
        struct c2_net_buf_desc    *net_desc;
        struct c2_net_domain      *dom;
        uint32_t                   buffers_added = 0;

        C2_PRE(fom != NULL);
        C2_PRE(c2_is_io_fop(fom->fo_fop));
        C2_PRE(fom->fo_phase == FOPH_IO_ZERO_COPY_INIT);

        fom_obj = container_of(fom, struct c2_io_fom_cob_rw, fcrw_gen);
        C2_ASSERT(c2_io_fom_cob_rw_invariant(fom_obj));

        fom_obj->fcrw_phase_start_time = c2_time_now();

        rwfop = io_rw_get(fop);
        rbulk = &fom_obj->fcrw_bulk;

        c2_rpc_bulk_init(rbulk);

        C2_ASSERT(c2_tlist_invariant(&netbufs_tl, &fom_obj->fcrw_netbuf_list));
        dom      =  fop->f_item.ri_session->s_conn->c_rpcmachine->cr_tm.ntm_dom;
        net_desc = &rwfop->crw_desc.id_descs[fom_obj->fcrw_curr_desc_index];
        rpc_item = (const struct c2_rpc_item *)&(fop->f_item);

        /* Create rpc bulk bufs list using available net buffers */
        c2_tlist_for(&netbufs_tl, &fom_obj->fcrw_netbuf_list, nb) {
                int                         current_index;
                uint32_t                    segs_nr;
                struct c2_rpc_bulk_buf     *rb_buf = NULL;

	        current_index = fom_obj->fcrw_curr_desc_index;
                segs_nr = rwfop->crw_ivecs.cis_ivecs[current_index].ci_nr;

                /*
                 * @todo : Since passing only number of segmnts, supports full
                 *         stripe I/Os. Should set exact count for last segment
                 *         of network buffer. Also need to reset last segment
                 *         count to original since buffers are reused by other
                 *         I/O requests.
                 */

                rc = c2_rpc_bulk_buf_add(rbulk, segs_nr, dom, nb, &rb_buf);
                if (rc != 0) {
                        fom->fo_rc = rc;
                        fom->fo_phase = FOPH_FAILURE;
                        C2_ADDB_ADD(&fom->fo_fop->f_addb, &io_fom_addb_loc,
                                    c2_addb_func_fail,
                                    "io_fom_cob_rw_initiate_zero_copy", rc);
                        return FSO_AGAIN;
                }

                fom_obj->fcrw_curr_desc_index++;
                buffers_added++;
        } c2_tlist_endfor;

        C2_ASSERT(buffers_added == fom_obj->fcrw_batch_size);

        /*
         * On completion of zero-copy on all buffers rpc_bulk
         * sends signal on channel rbulk->rb_chan.
         */
        c2_fom_block_at(fom, &rbulk->rb_chan);

        /*
         * This function deletes c2_rpc_bulk_buf object one
         * by one as zero copy completes on respective buffer.
         */
        rc = c2_rpc_bulk_load(rbulk, rpc_item->ri_session->s_conn, net_desc);
        if (rc != 0){
                c2_rpc_bulk_buflist_empty(rbulk);
                c2_rpc_bulk_fini(rbulk);
                fom->fo_rc = rc;
                fom->fo_phase = FOPH_FAILURE;
                C2_ADDB_ADD(&fom->fo_fop->f_addb, &io_fom_addb_loc,
                            c2_addb_func_fail,
                            "io_fom_cob_rw_initiate_zero_copy", rc);
                return FSO_AGAIN;
        }

        return FSO_WAIT;
}

/**
 * Zero-copy Finish
 * Check for zero-copy result.
 *
 * @param fom file operation machine.
 *
 * @pre fom != NULL
 * @pre fom->fo_phase == FOPH_IO_ZERO_COPY_WAIT
 */
static int io_fom_cob_rw_zero_copy_finish(struct c2_fom *fom)
{
        struct c2_io_fom_cob_rw   *fom_obj;
        struct c2_rpc_bulk        *rbulk;

        C2_PRE(fom != NULL);
        C2_PRE(c2_is_io_fop(fom->fo_fop));
        C2_PRE(fom->fo_phase == FOPH_IO_ZERO_COPY_WAIT);

	fom_obj = container_of(fom, struct c2_io_fom_cob_rw, fcrw_gen);
        C2_ASSERT(c2_io_fom_cob_rw_invariant(fom_obj));

        rbulk = &fom_obj->fcrw_bulk;

        c2_mutex_lock(&rbulk->rb_mutex);
        C2_ASSERT(rpcbulkbufs_tlist_is_empty(&rbulk->rb_buflist));
        if (rbulk->rb_rc != 0){
                fom->fo_rc = rbulk->rb_rc;
                fom->fo_phase = FOPH_FAILURE;
                C2_ADDB_ADD(&fom->fo_fop->f_addb, &io_fom_addb_loc,
                            c2_addb_func_fail,
                            "io_fom_cob_rw_zero_copy_finish", fom->fo_rc);
                c2_mutex_unlock(&rbulk->rb_mutex);
                return FSO_AGAIN;
        }

        c2_mutex_unlock(&rbulk->rb_mutex);
        c2_rpc_bulk_fini(rbulk);

        return FSO_AGAIN;
}

/**
 * Launch STOB I/O
 * Helper function to launch STOB I/O.
 * This function initiates STOB I/O for all index vecs.
 * STOB I/O signaled on channel in c2_stob_io::si_wait.
 * There is a clink for each STOB I/O waiting on respective
 * c2_stob_io::si_wait. For every STOB I/O completion call-back
 * is launched to check its results and mark complete. After
 * all STOB I/O completes call-back function send signal to FOM
 * so that FOM gets out of wait-queue and placed in run-queue.
 *
 * @param fom file operation machine
 *
 * @pre fom != NULL
 * @pre fom->fo_phase == FOPH_IO_STOB_INIT
 */
static int io_fom_cob_rw_io_launch(struct c2_fom *fom)
{
	int				 rc;
	uint32_t			 bshift;
	struct c2_fid			 fid;
	struct c2_fop			*fop;
	struct c2_io_fom_cob_rw	        *fom_obj;
	struct c2_stob_id		 stobid;
        struct c2_net_buffer            *nb = NULL;
	struct c2_fop_cob_rw		*rwfop;
        struct c2_io_indexvec            wire_ivec;
	struct c2_stob_domain		*fom_stdom;
	struct c2_fop_file_fid		*ffid;

	C2_PRE(fom != NULL);
        C2_PRE(c2_is_io_fop(fom->fo_fop));
        C2_PRE(fom->fo_phase == FOPH_IO_STOB_INIT);

	fom_obj = container_of(fom, struct c2_io_fom_cob_rw, fcrw_gen);
        C2_ASSERT(c2_io_fom_cob_rw_invariant(fom_obj));
        C2_ASSERT(stobio_tlist_is_empty(&fom_obj->fcrw_stio_list));
        C2_ASSERT(fom_obj->fcrw_num_stobio_launched == 0);

        fom_obj->fcrw_phase_start_time = c2_time_now();

	fop = fom->fo_fop;
	rwfop = io_rw_get(fop);

	ffid = &rwfop->crw_fid;
	io_fom_cob_rw_fid_wire2mem(ffid, &fid);
	io_fom_cob_rw_fid2stob_map(&fid, &stobid);
	fom_stdom = fom->fo_loc->fl_dom->fd_reqh->rh_stdom;

	rc = c2_stob_find(fom_stdom, &stobid, &fom_obj->fcrw_stob);
	if (rc != 0)
		goto cleanup;

        /*
         * @todo :
         * Internally c2_db_cursor_get() takes explicitely RW lock.
         * Need to define enum lock modes and pass to c2_db_cursor_get().
         * Till this issue fix, I/O FOM use c2_fom_block_enter() before
         * stob io locate since it coult block.
         * c2_fom_block_leave() should call after c2_stob_locate() returns.
         *
         * @note : This issue of explicitely RW lock will be addressed
         *         in separate task. Completion of that task remove
         *          block_enter  & block_leave.
         */
        c2_fom_block_enter(fom);
	rc = c2_stob_locate(fom_obj->fcrw_stob, &fom->fo_tx);
	if (rc != 0) {
		goto cleanup_st;
	}

        c2_fom_block_leave(fom);

	/*
           Since the upper layer IO block size could differ with IO block size
	   of storage object, the block alignment and mapping is necesary.
         */
	bshift = fom_obj->fcrw_stob->so_op->sop_block_shift(fom_obj->fcrw_stob);

        C2_ASSERT(c2_tlist_invariant(&netbufs_tl, &fom_obj->fcrw_netbuf_list));
        C2_ASSERT(c2_tlist_invariant(&stobio_tl, &fom_obj->fcrw_stio_list));

        c2_mutex_lock(&fom_obj->fcrw_stio_mutex);
        c2_tlist_for(&netbufs_tl, &fom_obj->fcrw_netbuf_list, nb) {
                struct c2_indexvec     *mem_ivec;
                struct c2_stob_io_desc *stio_desc;
                struct c2_stob_io      *stio;
                c2_bcount_t             ivec_count;

                C2_ALLOC_PTR(stio_desc);
                if (stio_desc == NULL) {
                        C2_ADDB_ADD(&fom->fo_fop->f_addb, &io_fom_addb_loc,
                                    c2_addb_oom);
                        rc = -ENOMEM;
                        break;
                }

                stio_desc->siod_magic = C2_STOB_IO_DESC_LINK_MAGIC;
                c2_clink_init(&stio_desc->siod_clink,
                              &io_fom_cob_rw_stobio_complete_cb);
                stio_desc->siod_fom = fom_obj;

	        stio = &stio_desc->siod_stob_io;
	        c2_stob_io_init(stio);

                mem_ivec = &stio->si_stob;
	        wire_ivec =
                rwfop->crw_ivecs.cis_ivecs[fom_obj->fcrw_curr_ivec_index++];
		rc = io_fom_cob_rw_indexvec_wire2mem(fom, &wire_ivec,
                                                     mem_ivec, bshift);
                if (rc != 0) {
                        /*
                         * Since this stob io not added into list
                         * yet, free it here.
                         */
                        C2_ADDB_ADD(&fom->fo_fop->f_addb, &io_fom_addb_loc,
                                    c2_addb_func_fail,
                                    "io_fom_cob_rw_io_launch", rc);
                        c2_stob_io_fini(stio);
                        c2_free(stio_desc);
                        break;
                }

                /*
                 * Copy aligned network buffer to stobio object.
                 * Also trim network buffer as per I/O size.
                 */
                ivec_count = c2_vec_count(&mem_ivec->iv_vec);
                rc = io_fom_cob_rw_align_bufvec(fom, &stio->si_user,
                                                &nb->nb_buffer,
                                                ivec_count,
                                                bshift);
                if (rc != 0) {
                        /*
                         * Since this stob io not added into list
                         * yet, free it here.
                         */
                        C2_ADDB_ADD(&fom->fo_fop->f_addb, &io_fom_addb_loc,
                                    c2_addb_func_fail,
                                    "io_fom_cob_rw_io_launch", rc);
                        /*
                         * @todo: need to add memory free allocated in stio
                         *        in thid function.
                         */
                        c2_stob_io_fini(stio);
                        c2_free(stio_desc);
                        break;
                }

                if (c2_is_write_fop(fop))
                        stio->si_opcode = SIO_WRITE;
                else
                        stio->si_opcode = SIO_READ;

                c2_clink_add(&stio->si_wait, &stio_desc->siod_clink);
                rc = c2_stob_io_launch(stio, fom_obj->fcrw_stob,
                                       &fom->fo_tx, NULL);
                if (rc != 0) {
                        /*
                         * Since this stob io not added into list
                         * yet, free it here.
                         */
                        C2_ADDB_ADD(&fom->fo_fop->f_addb, &io_fom_addb_loc,
                                    c2_addb_func_fail,
                                    "io_fom_cob_rw_io_launch", rc);
                        c2_clink_del(&stio_desc->siod_clink);
                        c2_stob_io_fini(stio);
                        c2_free(stio_desc);
                        break;
                }

                fom_obj->fcrw_num_stobio_launched++;
                stobio_tlink_init(stio_desc);
                stobio_tlist_add(&fom_obj->fcrw_stio_list, stio_desc);

        } c2_tlist_endfor;

        /*
           1. Add FOM clink to wait channel only if atleast one STOB I/O
              launched.
           2. Unlock STOB I/O mutex only after FOM clink added to waiting
              channel.
           3. I/O FOM behavior in different scenarios:
              a. No I/O launched - will not wait switch to FOPH_IO_STOB_WAIT
                 and declare fOM as failure.
              b. STOB I/O launched less than batch size - will set errorcode
                 to FOM and switch to FOPH_IO_STOB_WAIT. Will discard results
                 of launched I/O.
              c. STOB I/O launched equal to batch size - will switch to
                 FOPH_IO_STOB_WAIT and check for STOB I/O results.
          */
        if ( fom_obj->fcrw_num_stobio_launched > 0) {
                c2_fom_block_at(fom, &fom_obj->fcrw_wait);

                /*
                 * If I/O launched less that batch size call-back will
                 * ignore STOB I/O results.
                 */
	        fom->fo_rc = rc;

                c2_mutex_unlock(&fom_obj->fcrw_stio_mutex);

	        return FSO_WAIT;
        }

        c2_mutex_unlock(&fom_obj->fcrw_stio_mutex);

cleanup_st:
        c2_stob_put(fom_obj->fcrw_stob);
cleanup:
	C2_ASSERT(rc != 0);
	fom->fo_rc = rc;
	fom->fo_phase = FOPH_FAILURE;
        C2_ADDB_ADD(&fom->fo_fop->f_addb, &io_fom_addb_loc, c2_addb_func_fail,
                    "io_fom_cob_rw_io_launch", rc);
	return FSO_AGAIN;
}

/**
 * This function finish STOB I/O.
 * It's check for STOB I/O result and return back STOB instance.
 *
 * @param fom instance file operation machine under execution
 *
 * @pre fom != NULL
 * @pre fom->fo_phase == FOPH_IO_STOB_WAIT
 */
static int io_fom_cob_rw_io_finish(struct c2_fom *fom)
{
        struct c2_io_fom_cob_rw   *fom_obj;
        struct c2_stob_io_desc    *stio_desc;

        C2_PRE(fom != NULL);
        C2_PRE(c2_is_io_fop(fom->fo_fop));
        C2_PRE(fom->fo_phase == FOPH_IO_STOB_WAIT);

        fom_obj = container_of(fom, struct c2_io_fom_cob_rw, fcrw_gen);
        C2_ASSERT(c2_io_fom_cob_rw_invariant(fom_obj));
        C2_ASSERT(fom_obj->fcrw_num_stobio_launched == 0);
        C2_ASSERT(c2_tlist_invariant(&stobio_tl, &fom_obj->fcrw_stio_list));

        /*
         * Empty the list as all STOB I/O completed here.
         */
        c2_mutex_lock(&fom_obj->fcrw_stio_mutex);
        c2_tlist_for (&stobio_tl, &fom_obj->fcrw_stio_list, stio_desc) {
                struct c2_stob_io *stio;

                stio = &stio_desc->siod_stob_io;

                c2_free(stio->si_user.ov_vec.v_count);
                c2_free(stio->si_user.ov_buf);

                c2_free(stio->si_stob.iv_vec.v_count);
                c2_free(stio->si_stob.iv_index);

                c2_clink_del(&stio_desc->siod_clink);
                c2_clink_fini(&stio_desc->siod_clink);

                c2_stob_io_fini(stio);

                stobio_tlist_del(stio_desc);
        } c2_tlist_endfor;
        c2_mutex_unlock(&fom_obj->fcrw_stio_mutex);

        c2_stob_put(fom_obj->fcrw_stob);

        if (fom->fo_rc != 0) {
	        fom->fo_phase = FOPH_FAILURE;
                C2_ADDB_ADD(&fom->fo_fop->f_addb, &io_fom_addb_loc,
                            c2_addb_func_fail, "io_fom_cob_rw_io_finish",
                            fom->fo_rc);
	        return FSO_AGAIN;
        }

        return FSO_AGAIN;
}

/**
 * State Transition function for I/O operation that executes
 * on data server.
 *
 * @param fom instance file operation machine under execution
 *
 * @pre fom != NULL
 */
static int c2_io_fom_cob_rw_state(struct c2_fom *fom)
{
        int                                       rc = 0;
        struct c2_io_fom_cob_rw                  *fom_obj;
        struct c2_io_fom_cob_rw_state_transition  st;

        C2_PRE(fom != NULL);
        C2_PRE(c2_is_io_fop(fom->fo_fop));

        fom_obj = container_of(fom, struct c2_io_fom_cob_rw, fcrw_gen);
        C2_ASSERT(c2_io_fom_cob_rw_invariant(fom_obj));

        if (fom->fo_phase < FOPH_NR) {
                rc = c2_fom_state_generic(fom);
                return rc;
        }

        if (c2_is_read_fop(fom->fo_fop))
                st = io_fom_read_st[fom->fo_phase];
        else
                st = io_fom_write_st[fom->fo_phase];

        rc = (*st.fcrw_st_state_function)(fom);
        C2_ASSERT(rc == FSO_AGAIN || rc == FSO_WAIT);

        /* Set operation status in reply fop if FOM ends.*/
        if (fom->fo_phase == FOPH_SUCCESS || fom->fo_phase == FOPH_FAILURE) {
                struct c2_fop_cob_rw_reply *rwrep;

                C2_ASSERT(ergo(fom->fo_phase == FOPH_FAILURE, fom->fo_rc < 0));

                rwrep = io_rw_rep_get(fom->fo_rep_fop);
                rwrep->rwr_rc = fom->fo_rc;
                rwrep->rwr_count = fom_obj->fcrw_bytes_transfered;
                return rc;
        }

        fom->fo_phase = (rc == FSO_AGAIN) ? st.fcrw_st_next_phase_again :
                        st.fcrw_st_next_phase_wait;
        C2_ASSERT(fom->fo_phase > FOPH_NR &&
                  fom->fo_phase <= FOPH_IO_BUFFER_RELEASE);

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
static void c2_io_fom_cob_rw_fini(struct c2_fom *fom)
{
        uint32_t                        colour = 0;
        struct c2_fop                  *fop = fom->fo_fop;
        struct c2_io_fom_cob_rw        *fom_obj;
        struct c2_net_buffer           *nb = NULL;
        struct c2_stob_io_desc         *stio_desc = NULL;
        struct c2_net_transfer_mc       tm;

        C2_PRE(fom != NULL);

        fom_obj = container_of(fom, struct c2_io_fom_cob_rw, fcrw_gen);

        C2_ADDB_ADD(&fom->fo_fop->f_addb, &io_fom_addb_loc, c2_addb_trace,
		    "FOM finished : type=rw.");

        tm     = fop->f_item.ri_session->s_conn->c_rpcmachine->cr_tm;
        colour = c2_net_tm_colour_get(&tm);

        c2_chan_fini(&fom_obj->fcrw_wait);

        C2_ASSERT(fom_obj->fcrw_bp != NULL);
        C2_ASSERT(c2_tlist_invariant(&netbufs_tl, &fom_obj->fcrw_netbuf_list));
        c2_net_buffer_pool_lock(fom_obj->fcrw_bp);
        c2_tlist_for (&netbufs_tl, &fom_obj->fcrw_netbuf_list, nb) {
                c2_net_buffer_pool_put(fom_obj->fcrw_bp, nb, colour);
                netbufs_tlink_del_fini(nb);
        } c2_tlist_endfor;
        c2_net_buffer_pool_unlock(fom_obj->fcrw_bp);
        netbufs_tlist_fini(&fom_obj->fcrw_netbuf_list);

        C2_ASSERT(c2_tlist_invariant(&stobio_tl, &fom_obj->fcrw_stio_list));
        c2_mutex_lock(&fom_obj->fcrw_stio_mutex);
        c2_tlist_for (&stobio_tl, &fom_obj->fcrw_stio_list, stio_desc) {
                struct c2_stob_io *stio;

                stio = &stio_desc->siod_stob_io;

                c2_free(stio->si_user.ov_vec.v_count);
                c2_free(stio->si_user.ov_buf);

                c2_free(stio->si_stob.iv_vec.v_count);
                c2_free(stio->si_stob.iv_index);

                c2_stob_io_fini(stio);

                stobio_tlist_del(stio_desc);
        } c2_tlist_endfor;
        c2_mutex_unlock(&fom_obj->fcrw_stio_mutex);
        stobio_tlist_fini(&fom_obj->fcrw_stio_list);

        c2_mutex_fini(&fom_obj->fcrw_stio_mutex);

        c2_fom_fini(fom);

        c2_free(fom_obj);
}

/**
 * Get locality of file operation machine.
 *
 * @param fom instance file operation machine under execution
 *
 * @pre fom != NULL
 * @pre fom->fo_fop != NULL
 */
static size_t c2_io_fom_cob_rw_locality_get(const struct c2_fom *fom)
{
	C2_PRE(fom != NULL);
	C2_PRE(fom->fo_fop != NULL);

        return fom->fo_fop->f_type->ft_rpc_item_type.rit_opcode;
}

/**
 * Returns service name which executes this fom.
 *
 * @param fom instance file operation machine under execution
 *
 * @pre fom != NULL
 * @pre fom->fo_fop != NULL
 */
const char *c2_io_fom_cob_rw_service_name (struct c2_fom *fom)
{
        C2_PRE(fom != NULL);
        C2_PRE(fom->fo_fop != NULL);

        return IOSERVICE_NAME;
}

#if 0
/* Forward Declarations. */
static int    cob_fom_create(struct c2_fop *fop, struct c2_fom **out);
static int    cc_fom_create(struct c2_fom **out);
static void   cc_fom_fini(struct c2_fom *fom);
static void   cc_fom_populate(struct c2_fom *fom);
static int    cc_fom_state(struct c2_fom *fom);
static int    cc_stob_create(struct c2_fom *fom, struct c2_fom_cob_create *cc);
static int    cc_cob_create(struct c2_fom *fom, struct c2_fom_cob_create *cc);
static int    cc_cobfid_map_add(struct c2_fom *fom,
				struct c2_fom_cob_create *cc);

static void   cd_fom_fini(struct c2_fom *fom);
static int    cd_fom_state(struct c2_fom *fom);
static int    cd_fom_create(struct c2_fom **out);
static void   cd_fom_populate(struct c2_fom *fom);
static int    cd_cob_delete(struct c2_fom *fom, struct c2_fom_cob_delete *cd);
static int    cd_stob_delete(struct c2_fom *fom, struct c2_fom_cob_delete *cd);
static int    cd_cobfid_map_delete(struct c2_fom *fom,
				   struct c2_fom_cob_delete *cd);

static size_t cob_fom_locality_get(const struct c2_fom *fom);

static inline struct c2_fom_cob_create *cc_fom_get(struct c2_fom *fom);
static inline struct c2_fom_cob_delete *cd_fom_get(struct c2_fom *fom);

enum {
	CC_COB_VERSION_INIT	= 0,
	CC_COB_HARDLINK_NR	= 1,
	CD_FOM_STOBIO_LAST_REFS = 1,
};

static const struct c2_addb_loc cc_fom_addb_loc = {
	.al_name = "create_cob_fom",
};

C2_ADDB_EV_DEFINE(cc_fom_func_fail, "create cob func failed.",
		  C2_ADDB_EVENT_FUNC_FAIL, C2_ADDB_FUNC_CALL);

/**
 * Cob create fom ops.
 */
static struct c2_fom_ops cc_fom_ops = {
	.fo_fini	  = cc_fom_fini,
	.fo_state	  = cc_fom_state,
	.fo_home_locality = cob_fom_locality_get,
	.fo_service_name  = c2_io_fom_cob_rw_service_name,
};

static const struct c2_fom_type_ops cc_fom_type_ops = {
	.fto_create = cob_fom_create,
};

struct c2_fom_type cc_fom_type = {
	.ft_ops = &cc_fom_type_ops,
};

static const struct c2_addb_loc cd_fom_addb_loc = {
	.al_name = "cob_delete_fom",
};

C2_ADDB_EV_DEFINE(cd_fom_func_fail, "cob delete fom func failed.",
		  C2_ADDB_EVENT_FUNC_FAIL, C2_ADDB_FUNC_CALL);

/**
 * Cob delete fom ops.
 */
static const struct c2_fom_ops cd_fom_ops = {
	.fo_fini	  = cd_fom_fini,
	.fo_state	  = cd_fom_state,
	.fo_home_locality = cob_fom_locality_get,
	.fo_service_name  = c2_io_fom_cob_rw_service_name,
};

static const struct c2_fom_type_ops cd_fom_type_ops = {
	.fto_create = cob_fom_create,
};

struct c2_fom_type cd_fom_type = {
	.ft_ops = &cd_fom_type_ops
};

static int cob_fom_create(struct c2_fop *fop, struct c2_fom **out)
{
	int			  rc;
	bool			  cob_create;
	struct c2_fom		 *fom;
	struct c2_fom_cob_create *cc;
	struct c2_fom_cob_delete *cd;

	C2_PRE(fop != NULL);
	C2_PRE(fop->f_type != NULL);
	C2_PRE(out != NULL);
	C2_PRE(c2_is_cob_create_fop(fop) || c2_is_cob_delete_fop(fop));

	cob_create = c2_is_cob_create_fop(fop);

	rc = cob_create ? cc_fom_create(out) : cd_fom_create(out);
	if (rc != 0) {
		C2_ADDB_ADD(&fop->f_addb, &cc_fom_addb_loc, cc_fom_func_fail,
			    "Failed to create cob_create fom.", rc);
		return rc;
	}
	fom = *out;
	C2_ASSERT(fom != NULL);

	fom->fo_fop  = fop;
	fom->fo_type = &fop->f_type->ft_fom_type;
	if (cob_create) {
		cc = cc_fom_get(fom);
		cc_fom_populate(fom);
	} else {
		cd = cd_fom_get(fom);
		cd_fom_populate(fom);
	}

	c2_fom_init(fom);
	fom->fo_rep_fop = c2_fop_alloc(&c2_fop_cob_op_reply_fopt, NULL);
	if (fom->fo_rep_fop == NULL) {
		C2_ADDB_ADD(&fop->f_addb, &cc_fom_addb_loc, c2_addb_oom);
		cob_create ? c2_free(cc) : c2_free(cd);
		return -ENOMEM;
	}

	return rc;
}

static int cc_fom_create(struct c2_fom **out)
{
	struct c2_fom_cob_create *cc;

	C2_PRE(out != NULL);

	C2_ALLOC_PTR(cc);
	if (cc == NULL)
		return -ENOMEM;

	cc->fcc_cc.cc_fom.fo_ops  = &cc_fom_ops;
	*out = &cc->fcc_cc.cc_fom;
	return 0;
}

static inline struct c2_fom_cob_create *cc_fom_get(struct c2_fom *fom)
{
	struct c2_fom_cob_common *cc;

	C2_PRE(fom != NULL);

	cc = container_of(fom, struct c2_fom_cob_common, cc_fom);
	return container_of(cc, struct c2_fom_cob_create, fcc_cc);
}

static void cc_fom_fini(struct c2_fom *fom)
{
	struct c2_fom_cob_create *cc;

	C2_PRE(fom != NULL);

	cc = cc_fom_get(fom);

	c2_fom_fini(fom);
	c2_free(cc);
}

static size_t cob_fom_locality_get(const struct c2_fom *fom)
{
	C2_PRE(fom != NULL);

        return fom->fo_fop->f_type->ft_rpc_item_type.rit_opcode;
}

static void cc_fom_populate(struct c2_fom *fom)
{
	struct c2_fom_cob_create *cc;
	struct c2_fop_cob_create *f;

	C2_PRE(fom != NULL);

	cc = cc_fom_get(fom);
	f = c2_fop_data(fom->fo_fop);
	C2_ASSERT(f != NULL);

	io_fom_cob_rw_fid_wire2mem(&f->cc_common.c_gobfid,
				   &cc->fcc_cc.cc_gfid);
	io_fom_cob_rw_fid_wire2mem(&f->cc_common.c_cobfid,
				   &cc->fcc_cc.cc_cfid);
}

static int cc_fom_state(struct c2_fom *fom)
{
	int                         rc;
	struct c2_fom_cob_create   *cc;
	struct c2_fop_cob_op_reply *reply;

	C2_PRE(fom != NULL);
	C2_PRE(fom->fo_ops != NULL);
	C2_PRE(fom->fo_type != NULL);

	if (fom->fo_phase < FOPH_NR) {
		rc = c2_fom_state_generic(fom);
		return rc;
	}

	if (fom->fo_phase == FOPH_CC_COB_CREATE) {
		cc = cc_fom_get(fom);

		rc = cc_stob_create(fom, cc);
		if (rc != 0)
			goto out;

		rc = cc_cob_create(fom, cc);
		if (rc != 0)
			goto out;

		rc = cc_cobfid_map_add(fom, cc);
	} else
		C2_IMPOSSIBLE("Invalid phase for cob create fom.");

out:
	reply = c2_fop_data(fom->fo_rep_fop);
	reply->cor_rc = rc;

	fom->fo_rc = rc;
	fom->fo_phase = (rc == 0) ? FOPH_SUCCESS : FOPH_FAILURE;
	return FSO_AGAIN;
}

static int cc_stob_create(struct c2_fom *fom, struct c2_fom_cob_create *cc)
{
	int			  rc;
	struct c2_stob		 *stob;
	struct c2_stob_id	  stobid;
	struct c2_stob_domain	 *stdom;

	C2_PRE(fom != NULL);
	C2_PRE(cc != NULL);

	stdom = fom->fo_loc->fl_dom->fd_reqh->rh_stdom;
	io_fom_cob_rw_fid2stob_map(&cc->fcc_cc.cc_cfid, &stobid);

	rc = c2_stob_find(stdom, &stobid, &stob);
	if (rc != 0) {
		C2_ADDB_ADD(&fom->fo_fop->f_addb, &cc_fom_addb_loc,
			    cc_fom_func_fail,
			    "c2_stob_find() failed in cc_stob_create().", rc);
		return rc;
	}
	C2_ASSERT(stob != NULL);

	rc = c2_stob_create(stob, &fom->fo_tx);
	if (rc != 0)
		C2_ADDB_ADD(&fom->fo_fop->f_addb, &cc_fom_addb_loc,
			    cc_fom_func_fail,
			    "Stob creation failed in cc_stob_create().", rc);
	else
		cc->fcc_stob_id = stob->so_id;

	c2_stob_put(stob);
	return rc;
}

static int cc_cob_create(struct c2_fom *fom, struct c2_fom_cob_create *cc)
{
	int			  rc;
	struct c2_cob		 *cob;
	struct c2_cob_domain	 *cdom;
	struct c2_fop_cob_create *fop;
	struct c2_cob_nskey	 *nskey;
	struct c2_cob_nsrec	  nsrec;
	struct c2_cob_fabrec	  fabrec;

	C2_PRE(fom != NULL);
	C2_PRE(cc != NULL);

	cdom = fom->fo_loc->fl_dom->fd_reqh->rh_cob_domain;
	C2_ASSERT(cdom != NULL);
	fop = c2_fop_data(fom->fo_fop);

	c2_cob_nskey_make(&nskey, cc->fcc_cc.cc_cfid.f_container,
			  cc->fcc_cc.cc_cfid.f_key, fop->cc_cobname.ib_buf);
	if (nskey == NULL) {
		C2_ADDB_ADD(&fom->fo_fop->f_addb, &cc_fom_addb_loc,
			    c2_addb_oom);
		return -ENOMEM;
	}

	nsrec.cnr_stobid = cc->fcc_stob_id;
	nsrec.cnr_nlink = CC_COB_HARDLINK_NR;

	fabrec.cfb_version.vn_lsn = c2_fol_lsn_allocate(fom->fo_fol);
	fabrec.cfb_version.vn_vc = CC_COB_VERSION_INIT;

	rc = c2_cob_create(cdom, nskey, &nsrec, &fabrec, CA_NSKEY_FREE, &cob,
			   &fom->fo_tx.tx_dbtx);

	/*
	 * For rest of all errors, cob code puts reference on cob
	 * which in turn finalizes the in-memory cob object.
	 * The flag CA_NSKEY_FREE takes care of deallocating memory for
	 * nskey during cob finalization.
	 */
	if (rc == 0) {
		C2_ADDB_ADD(&fom->fo_fop->f_addb, &cc_fom_addb_loc,
			    c2_addb_trace, "Cob created successfully.");
		/**
		 * Since c2_cob_locate() does not cache in-memory cobs,
		 * c2_cob_locate() allocates a new c2_cob structure
		 * by default. Hence releasing reference of cob here which
		 * otherwise would cause a memory leak.
		 */
		c2_cob_put(cob);
	} else if (rc == -ENOMEM) {
		c2_free(nskey->cnk_name.b_data);
		C2_ADDB_ADD(&fom->fo_fop->f_addb, &cc_fom_addb_loc,
			    cc_fom_func_fail,
			    "Memory allocation failed in cc_cob_create().", rc);
	}

	return rc;
}

static int cc_cobfid_map_add(struct c2_fom *fom, struct c2_fom_cob_create *cc)
{
	int			rc;
	struct c2_uint128	cob_fid;
	struct c2_colibri      *cctx;
	struct c2_cobfid_setup *s = NULL;

	C2_PRE(fom != NULL);
	C2_PRE(cc != NULL);

	cctx = c2_cs_ctx_get(fom->fo_service);
	C2_ASSERT(cctx != NULL);
	c2_mutex_lock(&cctx->cc_mutex);
	rc = c2_cobfid_setup_get(&s, cctx);
	c2_mutex_unlock(&cctx->cc_mutex);
	C2_ASSERT(rc == 0 && s != NULL);

	cob_fid.u_hi = cc->fcc_cc.cc_cfid.f_container;
	cob_fid.u_lo = cc->fcc_cc.cc_cfid.f_key;

	rc = c2_cobfid_setup_recadd(s, cc->fcc_cc.cc_gfid, cob_fid);
	if (rc != 0)
		C2_ADDB_ADD(&fom->fo_fop->f_addb, &cc_fom_addb_loc,
			    cc_fom_func_fail, "cobfid_map_add() failed.", rc);
	else
		C2_ADDB_ADD(&fom->fo_fop->f_addb, &cc_fom_addb_loc,
			    c2_addb_trace, "Record added to cobfid_map.");

	c2_mutex_lock(&cctx->cc_mutex);
	c2_cobfid_setup_put(cctx);
	c2_mutex_unlock(&cctx->cc_mutex);
	return rc;
}

static int cd_fom_create(struct c2_fom **out)
{
	struct c2_fom_cob_delete *cd;

	C2_PRE(out != NULL);

	C2_ALLOC_PTR(cd);
	if (cd == NULL)
		return -ENOMEM;

	cd->fcd_cc.cc_fom.fo_ops = &cd_fom_ops;
	*out = &cd->fcd_cc.cc_fom;
	return 0;
}

static inline struct c2_fom_cob_delete *cd_fom_get(struct c2_fom *fom)
{
	struct c2_fom_cob_common *c;

	C2_PRE(fom != NULL);

	c = container_of(fom, struct c2_fom_cob_common, cc_fom);
	return container_of(c, struct c2_fom_cob_delete, fcd_cc);
}

static void cd_fom_fini(struct c2_fom *fom)
{
	struct c2_fom_cob_delete *cd;

	C2_PRE(fom != NULL);

	cd = cd_fom_get(fom);

	c2_fom_fini(fom);
	c2_free(cd);
}

static void cd_fom_populate(struct c2_fom *fom)
{
	struct c2_fom_cob_delete *cd;
	struct c2_fop_cob_delete *f;

	C2_PRE(fom != NULL);

	cd = cd_fom_get(fom);
	f = c2_fop_data(fom->fo_fop);

	io_fom_cob_rw_fid_wire2mem(&f->cd_common.c_gobfid,
				   &cd->fcd_cc.cc_gfid);
	io_fom_cob_rw_fid_wire2mem(&f->cd_common.c_cobfid,
				   &cd->fcd_cc.cc_cfid);

	cd->fcd_stobid.si_bits.u_hi = f->cd_common.c_cobfid.f_seq;
	cd->fcd_stobid.si_bits.u_lo = f->cd_common.c_cobfid.f_oid;
}

static int cd_fom_state(struct c2_fom *fom)
{
	int                         rc;
	struct c2_fom_cob_delete   *cd;
	struct c2_fop_cob_op_reply *reply;

	C2_PRE(fom != NULL);
	C2_PRE(fom->fo_ops != NULL);
	C2_PRE(fom->fo_type != NULL);

	if (fom->fo_phase < FOPH_NR) {
		rc = c2_fom_state_generic(fom);
		return rc;
	}

	if (fom->fo_phase == FOPH_CD_COB_DEL) {
		cd = cd_fom_get(fom);

		rc = cd_cob_delete(fom, cd);
		if (rc != 0)
			goto out;

		rc = cd_stob_delete(fom, cd);
		if (rc != 0)
			goto out;

		rc = cd_cobfid_map_delete(fom, cd);
	} else
		C2_IMPOSSIBLE("Invalid phase for cob delete fom.");

out:
	reply = c2_fop_data(fom->fo_rep_fop);
	reply->cor_rc = rc;

	fom->fo_rc = rc;
	fom->fo_phase = (rc == 0) ? FOPH_SUCCESS : FOPH_FAILURE;
	return FSO_AGAIN;
}

static int cd_cob_delete(struct c2_fom *fom, struct c2_fom_cob_delete *cd)
{
	int			  rc;
	struct c2_cob	         *cob;
	struct c2_cob_domain	 *cdom;

	C2_PRE(fom != NULL);
	C2_PRE(cd != NULL);

	cdom = fom->fo_loc->fl_dom->fd_reqh->rh_cob_domain;
	C2_ASSERT(cdom != NULL);

	rc = c2_cob_locate(cdom, &cd->fcd_stobid, &cob, &fom->fo_tx.tx_dbtx);
	if (rc != 0) {
		C2_ADDB_ADD(&fom->fo_fop->f_addb, &cd_fom_addb_loc,
			    cd_fom_func_fail,
			    "c2_cob_locate() failed.", rc);
		return rc;
	}
	C2_ASSERT(cob != NULL);

	rc = c2_cob_delete(cob, &fom->fo_tx.tx_dbtx);
	if (rc != 0)
		C2_ADDB_ADD(&fom->fo_fop->f_addb, &cd_fom_addb_loc,
			    cd_fom_func_fail, "c2_cob_delete() failed.", rc);
	else
		C2_ADDB_ADD(&fom->fo_fop->f_addb, &cc_fom_addb_loc,
			    c2_addb_trace, "Cob deleted successfully.");

	return rc;
}

static int cd_stob_delete(struct c2_fom *fom, struct c2_fom_cob_delete *cd)
{
	int			  rc;
	struct c2_stob		 *stob = NULL;
	struct c2_stob_domain	 *stdom;

	C2_PRE(fom != NULL);
	C2_PRE(cd != NULL);

	stdom = fom->fo_loc->fl_dom->fd_reqh->rh_stdom;

	rc = c2_stob_find(stdom, &cd->fcd_stobid, &stob);
	if (rc != 0) {
		C2_ADDB_ADD(&fom->fo_fop->f_addb, &cd_fom_addb_loc,
			    cd_fom_func_fail,
			    "c2_stob_find() failed.", rc);
		return rc;
	}
	C2_ASSERT(stob != NULL);

	C2_ASSERT(stob->so_ref.a_value == CD_FOM_STOBIO_LAST_REFS);
	c2_stob_put(stob);
	C2_ADDB_ADD(&fom->fo_fop->f_addb, &cc_fom_addb_loc,
		    c2_addb_trace, "Stob deleted successfully.");

	return rc;
}

static int cd_cobfid_map_delete(struct c2_fom *fom,
				struct c2_fom_cob_delete *cd)
{
	int			  rc;
	struct c2_uint128	  cob_fid;
	struct c2_colibri        *cctx;
	struct c2_cobfid_setup	 *s;

	C2_PRE(fom != NULL);
	C2_PRE(cd != NULL);

	cctx = c2_cs_ctx_get(fom->fo_service);
	C2_ASSERT(cctx != NULL);
	c2_mutex_lock(&cctx->cc_mutex);
	rc = c2_cobfid_setup_get(&s, cctx);
	c2_mutex_unlock(&cctx->cc_mutex);
	C2_ASSERT(rc == 0 && s != NULL);

	cob_fid.u_hi = cd->fcd_cc.cc_cfid.f_container;
	cob_fid.u_lo = cd->fcd_cc.cc_cfid.f_key;

	rc = c2_cobfid_setup_recdel(s, cd->fcd_cc.cc_gfid, cob_fid);
	if (rc != 0)
		C2_ADDB_ADD(&fom->fo_fop->f_addb, &cd_fom_addb_loc,
			    cd_fom_func_fail,
			    "c2_cobfid_setup_delrec() failed.", rc);
	else
		C2_ADDB_ADD(&fom->fo_fop->f_addb, &cc_fom_addb_loc,
				c2_addb_trace,
				"Record removed from cobfid_map.");

	c2_mutex_lock(&cctx->cc_mutex);
	c2_cobfid_setup_put(cctx);
	c2_mutex_unlock(&cctx->cc_mutex);
	return rc;
}
#endif

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
