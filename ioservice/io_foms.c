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
#include "fid/fid.h"
#include "reqh/reqh.h"
#include "reqh/reqh_service.h"
#include "net/buffer_pool.h"
#include "ioservice/io_service.h"
#include "lib/tlist.h"

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
                       to I/O error.
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
extern const struct c2_tl_descr nbp_colormap_tl;

C2_TL_DESCR_DEFINE(stobio, "STOB I/O", static, struct c2_stob_io_desc,
                   siod_linkage,  siod_magic,
                   0x53544f42492f4f,  0x73746f62692f6f);
C2_TL_DEFINE(stobio, static, struct c2_stob_io_desc);

C2_TL_DESCR_DEFINE(netbufs, "Aquired net buffers", static,
		   struct c2_net_buffer, nb_lru, nb_magic,
		   NET_BUFFER_LINK_MAGIC, NET_BUFFER_HEAD_MAGIC);
C2_TL_DEFINE(netbufs, static, struct c2_net_buffer);

C2_TL_DESCR_DEFINE(rpcbulkbufs, "rpc bulk buffers", static,
		   struct c2_rpc_bulk_buf, bb_link, bb_magic,
		   C2_RPC_BULK_BUF_MAGIC, 0xfacade12c3ed1b1f); /* head magic? */
C2_TL_DEFINE(rpcbulkbufs, static, struct c2_rpc_bulk_buf);

C2_TL_DESCR_DEFINE(services, "reqh handler services", static,
		   struct c2_reqh_service, rs_linkage, rs_magic,
		   C2_REQH_MAGIC, C2_RHS_MAGIC);
C2_TL_DEFINE(services, static, struct c2_reqh_service);

extern bool is_read(const struct c2_fop *fop);
extern bool is_write(const struct c2_fop *fop);
extern bool is_io(const struct c2_fop *fop);
extern struct c2_fop_cob_rw *io_rw_get(struct c2_fop *fop);
extern struct c2_fop_cob_rw_reply *io_rw_rep_get(struct c2_fop *fop);

static int c2_io_fom_cob_rw_create(struct c2_fom_type *t, struct c2_fom **m);
int c2_io_fom_cob_rw_init(struct c2_fop *fop, struct c2_fom **out);
static int c2_io_fom_cob_rw_state(struct c2_fom *fom);
static void c2_io_fom_cob_rw_fini(struct c2_fom *fom);
static size_t c2_io_fom_cob_rw_locality_get(const struct c2_fom *fom);

/**
 * I/O FOM operation vector.
 */
static const struct c2_fom_ops c2_io_fom_cob_rw_ops = {
	.fo_fini = c2_io_fom_cob_rw_fini,
	.fo_state = c2_io_fom_cob_rw_state,
	.fo_home_locality = c2_io_fom_cob_rw_locality_get,
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
static const struct c2_fom_type c2_io_fom_cob_rw_mopt = {
	.ft_ops = &c2_io_cob_rw_type_ops,
};

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
        struct c2_stob_io        *stio;
        struct c2_io_fom_cob_rw  *fom_obj;
        struct c2_stob_io_desc   *stio_desc;

        stio_desc = container_of(clink, struct c2_stob_io_desc, siod_clink);
        fom_obj = stio_desc->siod_fom;

        stio = &stio_desc->siod_stob_io;

        if (stio->si_rc != 0){
                fom_obj->fcrw_gen.fo_rc = stio->si_rc;
                fom_obj->fcrw_gen.fo_phase = FOPH_FAILURE;
        }

        /** Update successfull data transfered count*/
        fom_obj->fcrw_bytes_transfered += stio->si_count;

        c2_free(stio->si_stob.iv_vec.v_count);
        c2_free(stio->si_stob.iv_index);
        c2_stob_io_fini(stio);
        /** Remove c2_stob_io_desc from stob io desc list. */
        stobio_tlist_del(stio_desc);

        if (stobio_tlist_is_empty(&fom_obj->fcrw_stio_list))
                c2_chan_signal(&fom_obj->fcrw_wait);

        return true;
};

/**
 * Funtion to get corresponding color for specified tm.
 *
 * @param color_map color map (tm to number map) for buffer pool.
 * @param tm transfer machine for which color value is needed.
 *
 * @pre color_map != NULL
 * @pre tm != NULL;
 */
static int io_fom_cob_rw_get_color(struct c2_tl *color_map,
                                   struct c2_net_transfer_mc *tm)
{
         int                        color = 0;
         struct c2_net_transfer_mc *tm_ptr = NULL;

        C2_PRE(color_map != NULL);
        C2_PRE(tm != NULL);

        /** find the domain */
        c2_tlist_for(&nbp_colormap_tl, color_map, tm_ptr)
        {
                C2_ASSERT(tm_ptr != NULL);
                if (tm == tm_ptr)
                        break;
                color++;
        } c2_tlist_endfor;

	return color;;
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
static void io_fom_cob_rw_fid2stob_map(const struct c2_fid *in,
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
static void io_fom_cob_rw_fid_wire2mem(struct c2_fop_file_fid *in,
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
 * @param in indexvec wire format
 * @param out indexvec memory format
 *
 * @pre in != NULL
 * @pre out != NULL
 */
static void io_fom_cob_rw_indexvec_wire2mem(struct c2_io_indexvec *in,
                                 struct c2_indexvec *out)
{
        int         i;

        C2_PRE(in != NULL);
        C2_PRE(out != NULL);

        /**
         * This memory will be freed after its container stob
         * completes and before destructing container stob object.
         */
        C2_ALLOC_ARR(out->iv_vec.v_count, in->ci_nr);
        C2_ALLOC_ARR(out->iv_index, in->ci_nr);

        out->iv_vec.v_nr = in->ci_nr;
        for (i = 0; i < in->ci_nr; i++) {
                out->iv_index[i] = in->ci_iosegs[i].ci_index;
                out->iv_vec.v_count[i] = in->ci_iosegs[i].ci_count;
        }
}

/**
 * Allocate I/O FOM and return generic struct c2_fom
 *
 * @param t file operation machine type need to process
 * @param out file operation machine instance
 *
 * @pre t != NULL
 * @pre out != NULL
 */
static int c2_io_fom_cob_rw_create(struct c2_fom_type *t, struct c2_fom **out)
{
        struct c2_fom             *fom;
        struct c2_io_fom_cob_rw   *fom_obj;

        C2_PRE(t != NULL);
        C2_PRE(out != NULL);

        fom_obj= c2_alloc(sizeof *fom_obj);
        if (fom_obj == NULL)
                return -ENOMEM;
        fom = &fom_obj->fcrw_gen;
        fom->fo_type = t;

        fom->fo_ops = &c2_io_fom_cob_rw_ops;

        *out = fom;
        return 0;
}

/**
 * Initiate I/O FOM and return generic struct c2_fom
 * Find the corresponding fom_type and associate it with c2_fom.
 * Associate fop with fom type.
 *
 * @param fop file operation packat need to process
 * @param fom file operation machine need to allocate and initiate
 *
 * @pre fop != NULL
 * @pre m != NULL
 */
int c2_io_fom_cob_rw_init(struct c2_fop *fop, struct c2_fom **out)
{
        int                      result;
        struct c2_fom           *fom;
        struct c2_io_fom_cob_rw *fom_obj;
        struct c2_fop_cob_rw    *rwfop;
        struct c2_fom_type       fomtype;

        C2_PRE(fop != NULL);
        C2_PRE(out != NULL);

        fop->f_type->ft_fom_type =  c2_io_fom_cob_rw_mopt;
        fomtype = fop->f_type->ft_fom_type;
        result = fomtype.ft_ops->fto_create(&(fop->f_type->ft_fom_type), out);
        if (result == 0)
            return result;

        fom = *out;

        c2_fom_init(fom);
        fom->fo_fop = fop;

        /**
         * Since reqh services information not available
         * it is initiated to NULL. Service instance will
         * be set to fom while FOM execution.
         * todo : This should be done by reqh handler
         *        after FOM init call returns.
         */
        fom->fo_service = NULL;

        if (is_read(fop))
                fom->fo_rep_fop = c2_fop_alloc(&c2_fop_cob_readv_rep_fopt,
                                               NULL);
        else
                fom->fo_rep_fop = c2_fop_alloc(&c2_fop_cob_writev_rep_fopt,
                                               NULL);
        if (fom->fo_rep_fop == NULL) {
                c2_free(fom_obj);
                return -ENOMEM;
        }

        fom_obj = container_of(*out, struct c2_io_fom_cob_rw, fcrw_gen);

        fom_obj->fcrw_stob = NULL;

        rwfop = io_rw_get(fop);
        fom_obj->fcrw_ndesc = rwfop->crw_desc.id_nr;
        fom_obj->fcrw_curr_desc_index = 0;
        fom_obj->fcrw_curr_ivec_index = 0;
        fom_obj->fcrw_batch_size = rwfop->crw_desc.id_nr;
        fom_obj->fcrw_bytes_transfered = 0;
        c2_chan_init(&fom_obj->fcrw_wait);
        netbufs_tlist_init(&fom_obj->fcrw_netbuf_list);
        stobio_tlist_init(&fom_obj->fcrw_stio_list);

        return result;
}

/**
 * Acquare network buffers.
 * Gets as many network buffer as it can to process io request.
 * It should atleast get one network buffer to start io processing.
 * If not able to get single buffer, FOM wait for buffer pool to
 * become non-empty.
 *
 * If acquired buffers are less than number of fop descriptors then
 * I/O processing will be done by batch.
 *
 * @param fom file operation machine instance.
 * @pre fom != NULL
 * @pre fom->fo_phase == FOPH_IO_FOM_BUFFER_ACQUIRE
 */
static int io_fom_cob_rw_get_net_buffer(struct c2_fom *fom)
{
        int                       color;
        int                       aquired_net_bufs;
        int                       required_net_bufs;
        struct c2_tl              *services;
        struct c2_fop             *fop = fom->fo_fop;
        struct c2_io_fom_cob_rw   *fom_obj = NULL;
        struct c2_reqh_service    *service;
        struct c2_reqh_io_service *serv_obj;
        struct c2_net_buffer_pool *bp = NULL;
        struct c2_net_transfer_mc *tm = NULL;

        C2_PRE(fom != NULL);
        C2_PRE(fom->fo_phase == FOPH_IO_FOM_BUFFER_ACQUIRE);

        /**
         * TODO : This shoud be done in reqh handler code after FOM create and
         *        before adding FOM into runqueue.
         */
        if (fom->fo_service == NULL) {
                services = &fom->fo_loc->fl_dom->fd_reqh->rh_services;
                c2_tlist_for(&services_tl, services, service) {
                        if (strcmp(service->rs_type->rst_name,
                            IOSERVICE_NAME) == 0) {
                                fom->fo_service = service;
                                break;
                        }
                } c2_tlist_endfor;
        }
	/** End TODO */

        fom_obj = container_of(fom, struct c2_io_fom_cob_rw, fcrw_gen);
        serv_obj = container_of(service, struct c2_reqh_io_service, rios_gen);
        bp = &serv_obj->rios_nb_pool;
        tm = &fop->f_item.ri_session->s_conn->c_rpcmachine->cr_tm;
        color = io_fom_cob_rw_get_color(&serv_obj->rios_nbp_color_map, tm);
        aquired_net_bufs = netbufs_tlist_length(&fom_obj->fcrw_netbuf_list);
        required_net_bufs = fom_obj->fcrw_ndesc - aquired_net_bufs;


        /**
         * Aquire as many net buffers as to process all discriptors.
         * If FOM able to acquire more buffers then it change batch side
         * dynamically.
         */
        for (;aquired_net_bufs < required_net_bufs;) {
            struct c2_net_buffer      *nb = NULL;

            c2_net_buffer_pool_lock(bp);
            nb = c2_net_buffer_pool_get(bp, color);
            c2_net_buffer_pool_unlock(bp);
            if (nb == NULL && aquired_net_bufs == 0) {
                    /**
                     * Network buffer not available. Atleast one
                     * buffer need for zero-copy. Registers FOM clink
                     * with buffer pool wait channel to get buffer
                     * pool non-empty signal.
                     */
                    c2_fom_block_at(fom, &serv_obj->rios_nbp_wait);

                    fom->fo_phase = FOPH_IO_FOM_BUFFER_WAIT;

                    return  FSO_WAIT;
            } else if (nb == NULL) {
                    break;
            }

            if (is_read(fop))
                   nb->nb_qtype = C2_NET_QT_ACTIVE_BULK_SEND;
            else
                   nb->nb_qtype = C2_NET_QT_ACTIVE_BULK_RECV;

            netbufs_tlist_add(&fom_obj->fcrw_netbuf_list, nb);
            aquired_net_bufs++;

        }
        fom_obj->fcrw_batch_size = aquired_net_bufs;

        if (is_read(fop))
                fom->fo_phase = FOPH_IO_STOB_INIT;
        else
                fom->fo_phase = FOPH_IO_ZERO_COPY_INIT;

        return FSO_AGAIN;
}

/**
 * Release network buffer.
 * Return back netwotk buffer to buffer pool.
 * If acquired buffers are more than the remaining descriptors
 * release extra buffers so that other FOM can use.
 *
 * @param fom file operation machine.
 *
 * @pre fom != NULL
 * @pre fom->fo_phase == FOPH_IO_BUFFER_RELEASE
 */
static int io_fom_cob_rw_release_net_buffer(struct c2_fom *fom)
{
        int                             color;
        int                             aquired_net_bufs;
        int                             required_net_bufs;
        struct c2_fop                  *fop;
        struct c2_io_fom_cob_rw        *fom_obj = NULL;
        struct c2_reqh_io_service      *serv_obj;
        struct c2_net_buffer_pool      *bp;
        struct c2_net_transfer_mc      *tm;


        C2_PRE(fom != NULL);
        C2_PRE(fom->fo_phase == FOPH_IO_BUFFER_RELEASE);

        fop = fom->fo_fop;
        fom_obj = container_of(fom, struct c2_io_fom_cob_rw, fcrw_gen);
        serv_obj = container_of(fom->fo_service, struct c2_reqh_io_service,
                                rios_gen);
        tm = &(fop->f_item.ri_session->s_conn->c_rpcmachine->cr_tm);
        color = io_fom_cob_rw_get_color(&serv_obj->rios_nbp_color_map, tm);
        aquired_net_bufs = netbufs_tlist_length(&fom_obj->fcrw_netbuf_list);
        if (fom_obj->fcrw_curr_desc_index >= fom_obj->fcrw_ndesc)
                required_net_bufs = 0;
        else
                required_net_bufs = fom_obj->fcrw_ndesc - aquired_net_bufs;

        bp = &serv_obj->rios_nb_pool;

        for (;aquired_net_bufs > required_net_bufs;) {
                struct c2_net_buffer           *nb = NULL;

                netbufs_tlist_tail(&fom_obj->fcrw_netbuf_list);
                C2_ASSERT(nb != NULL);

                c2_net_buffer_pool_lock(bp);
                c2_net_buffer_pool_put(bp, nb, color);
                c2_net_buffer_pool_unlock(bp);

                netbufs_tlist_del(nb);
                aquired_net_bufs--;
        }

        if (!required_net_bufs) {
               fom->fo_phase = FOPH_FINISH;
        } else {
                if (is_read(fop))
                        fom->fo_phase = FOPH_IO_STOB_INIT;
                else
                        fom->fo_phase = FOPH_IO_ZERO_COPY_INIT;
        }

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

        C2_PRE(fom != NULL);
        C2_PRE(fom->fo_phase == FOPH_IO_ZERO_COPY_INIT);

        rwfop = io_rw_get(fop);
        rbulk = &fom_obj->fcrw_bulk;

        c2_rpc_bulk_init(rbulk);

        /** find the domain */
        c2_tlist_for(&netbufs_tl, &fom_obj->fcrw_netbuf_list, nb) {
                struct c2_rpc_bulk_buf     *rb_buf = NULL;

                C2_ASSERT(nb != NULL);

                C2_ALLOC_PTR(rb_buf);
                if (rb_buf == NULL) {
                        fom->fo_rc = -ENOMEM;
                        fom->fo_phase = FOPH_FAILURE;
                        return FSO_AGAIN;
                }

                rb_buf->bb_magic = C2_RPC_BULK_BUF_MAGIC;
                rb_buf->bb_nbuf = *nb;
                rb_buf->bb_rbulk = rbulk;

                rpcbulkbufs_tlist_add(&rbulk->rb_buflist, rb_buf);

        } c2_tlist_endfor;

        net_desc = &rwfop->crw_desc.id_descs[fom_obj->fcrw_curr_desc_index];
        rpc_item = (const struct c2_rpc_item *)&(fop->f_item);

        c2_fom_block_at(fom, &rbulk->rb_chan);

        rc = c2_rpc_bulk_load(rbulk, rpc_item, net_desc);
        if (rc != 0){
                fom->fo_rc = rc;
                fom->fo_phase = FOPH_FAILURE;
                return FSO_AGAIN;
        }

        fom_obj->fcrw_curr_desc_index += fom_obj->fcrw_batch_size;

        fom->fo_phase = FOPH_IO_ZERO_COPY_WAIT;

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
static int io_fom_cob_rw_initiate_zero_copy_wait(struct c2_fom *fom)
{
        struct c2_fop             *fop = fom->fo_fop;
        struct c2_io_fom_cob_rw   *fom_obj;
        struct c2_rpc_bulk        *rbulk;
        struct c2_fop_cob_rw      *rwfop;
        struct c2_rpc_bulk_buf    *rb_buf = NULL;

        C2_PRE(fom != NULL);
        C2_PRE(fom->fo_phase == FOPH_IO_ZERO_COPY_WAIT);

        rwfop = io_rw_get(fop);
	fom_obj = container_of(fom, struct c2_io_fom_cob_rw, fcrw_gen);

        rbulk = &fom_obj->fcrw_bulk;

        if (rbulk->rb_rc != 0){
                fom->fo_rc = rbulk->rb_rc;
                fom->fo_phase = FOPH_FAILURE;
                return FSO_AGAIN;
        }

        c2_tlist_for(&rpcbulkbufs_tl, &rbulk->rb_buflist, rb_buf) {
                rpcbulkbufs_tlist_del(rb_buf);
        } c2_tlist_endfor;

        c2_rpc_bulk_fini(rbulk);

        if (is_read(fop))
                fom->fo_phase = FOPH_IO_BUFFER_RELEASE;
        else
                fom->fo_phase = FOPH_IO_STOB_INIT;

        return FSO_AGAIN;
}

/**
 * Launch STOB I/O
 * Helper function to launch STOB I/O. This is async
 * funciton it initiates STOB I/O for all index vecs and
 * return. STOB I/O signaled on channel in c2_stob_io::si_wait.
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
	uint64_t			 bmask;
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
        C2_PRE(fom->fo_phase == FOPH_IO_STOB_INIT);

	fom_obj = container_of(fom, struct c2_io_fom_cob_rw, fcrw_gen);
	fop = fom->fo_fop;
	rwfop = io_rw_get(fop);

	ffid = &rwfop->crw_fid;
	io_fom_cob_rw_fid_wire2mem(ffid, &fid);
	io_fom_cob_rw_fid2stob_map(&fid, &stobid);
	fom_stdom = fom->fo_loc->fl_dom->fd_reqh->rh_stdom;

	rc = c2_stob_find(fom_stdom, &stobid, &fom_obj->fcrw_stob);
	if (rc != 0)
		goto cleanup;

	rc = c2_stob_locate(fom_obj->fcrw_stob, &fom->fo_tx);
	if (rc != 0)
		goto cleanup_st;


	/* Since the upper layer IO block size could differ with IO block size
	   of storage object, the block alignment and mapping is necesary. */
	bshift = fom_obj->fcrw_stob->so_op->sop_block_shift(fom_obj->fcrw_stob);
	bmask = (1 << bshift) - 1;

        c2_tlist_for(&netbufs_tl, &fom_obj->fcrw_netbuf_list, nb) {
                struct c2_indexvec     *mem_ivec;
                struct c2_stob_io_desc *stio_desc;
                struct c2_stob_io      *stio;

                C2_ALLOC_PTR(stio_desc);

                c2_clink_init(&stio_desc->siod_clink,
                              &io_fom_cob_rw_stobio_complete_cb);
                stio_desc->siod_fom = fom_obj;

	        stio = &stio_desc->siod_stob_io;
	        c2_stob_io_init(stio);

                mem_ivec = &stio->si_stob;
	        wire_ivec = rwfop->crw_ivecs.cis_ivecs[fom_obj->fcrw_curr_ivec_index++];
                io_fom_cob_rw_indexvec_wire2mem(&wire_ivec, mem_ivec);

                /** Find out buffer address, offset and count required for stob
                   io. Due to existing limitations of kxdr wrapper over sunrpc,
                   read reply fop can not contain a vector, only a segment.
                   Ideally, all IO fops should carry an IO vector. */
                if (is_write(fop)) {
                	/* Make an FOL transaction record. */
                	rc = c2_fop_fol_rec_add(fop, fom->fo_fol,
                                                &fom->fo_tx.tx_dbtx);
                	if (rc != 0)
                		goto cleanup_st;

                	stio->si_opcode = SIO_WRITE;
                } else {
                	stio->si_opcode = SIO_READ;
                }

                stio->si_user = nb->nb_buffer;

                c2_clink_add(&stio->si_wait, &stio_desc->siod_clink);
                rc = c2_stob_io_launch(stio, fom_obj->fcrw_stob,
                                       &fom->fo_tx, NULL);
                if (rc != 0)
                        goto cleanup_st;

                stobio_tlist_add(&fom_obj->fcrw_stio_list, stio_desc);

        } c2_tlist_endfor;

        c2_fom_block_at(fom, &fom_obj->fcrw_wait);

        fom->fo_phase = FOPH_IO_STOB_WAIT;

	return FSO_WAIT;

cleanup_st:
	c2_stob_put(fom_obj->fcrw_stob);
cleanup:
	C2_ASSERT(rc != 0);
	fom->fo_rc = rc;
	fom->fo_phase = FOPH_FAILURE;
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
static int io_fom_cob_rw_io_launch_wait(struct c2_fom *fom)
{
        struct c2_fop             *fop = fom->fo_fop;
        struct c2_io_fom_cob_rw   *fom_obj;

        C2_PRE(fom != NULL);
        C2_PRE(fom->fo_phase == FOPH_IO_STOB_WAIT);

        fom_obj = container_of(fom, struct c2_io_fom_cob_rw, fcrw_gen);

        c2_stob_put(fom_obj->fcrw_stob);

        if (fom->fo_rc != 0) {
	        fom->fo_phase = FOPH_FAILURE;
	        return FSO_AGAIN;
        }

        if (is_read(fop))
                fom->fo_phase = FOPH_IO_ZERO_COPY_INIT;
        else
                fom->fo_phase = FOPH_IO_BUFFER_RELEASE;

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
        int        rc = 0;

        C2_PRE(fom != NULL);
        C2_PRE(is_io(fom->fo_fop));

        if (fom->fo_phase < FOPH_NR) {
                rc = c2_fom_state_generic(fom);
                return rc;
        }

        switch(fom->fo_phase){
        case FOPH_IO_FOM_BUFFER_ACQUIRE:
                rc = io_fom_cob_rw_get_net_buffer(fom);
                break;
        case FOPH_IO_FOM_BUFFER_WAIT:
                rc = io_fom_cob_rw_get_net_buffer(fom);
                break;
        case FOPH_IO_STOB_INIT:
                rc = io_fom_cob_rw_io_launch(fom);
                break;
        case FOPH_IO_STOB_WAIT:
                rc = io_fom_cob_rw_io_launch_wait(fom);
                break;
        case FOPH_IO_ZERO_COPY_INIT:
                rc = io_fom_cob_rw_initiate_zero_copy(fom);
                break;
        case FOPH_IO_ZERO_COPY_WAIT:
                rc = io_fom_cob_rw_initiate_zero_copy_wait(fom);
                break;
        case FOPH_IO_BUFFER_RELEASE:
               rc = io_fom_cob_rw_release_net_buffer(fom);
                break;
        default:
                C2_IMPOSSIBLE("Invalid phase of rw fom.");
        }

        return rc;
}

/**
 * Finalise of I/O file operation machine.
 *
 * @param fom instance file operation machine under execution
 *
 * @pre fom != NULL
 */
static void c2_io_fom_cob_rw_fini(struct c2_fom *fom)
{
        struct c2_io_fom_cob_rw        *fom_obj;
        struct c2_fop_cob_rw_reply     *rwrep;

        C2_PRE(fom != NULL);

        fom_obj = container_of(fom, struct c2_io_fom_cob_rw, fcrw_gen);

        /** send operation status with replay fop*/
        rwrep = io_rw_rep_get(fom->fo_rep_fop);
        rwrep->rwr_rc = fom->fo_rc;
        rwrep->rwr_count = fom_obj->fcrw_bytes_transfered;

        fom->fo_rc = 0;
        fom->fo_phase = FOPH_SUCCESS;

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

