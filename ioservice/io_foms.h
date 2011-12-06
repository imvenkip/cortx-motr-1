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
 * Original author: Anand Vidwansa <Anand_Vidwansa@xyratex.com>
 *                  Anup Barve <Anup_Barve@xyratex.com>
 * Original creation date: 03/21/2011
 * Revision       : Rajanikant Chirmade <Rajanikant_Chirmade@xyratex.com>
 * Revision date  : 09/14/2011
 */

#ifndef __COLIBRI_IOSERVICE_IO_FOMS_H__
#define __COLIBRI_IOSERVICE_IO_FOMS_H__

/**
   @page DLD-bulk-server-fspec Functional Specification

      - @ref DLD-bulk-server-fspec-ds
      - @ref DLD-bulk-server-fspec-if
      - @ref io_foms "FOP State Machines for I/O FOPs"
      - @ref DLD_bulk_server_fspec_ioservice_operations

   @section DLD-bulk-server-fspec Functional Specification
   This section describes the data structure, external interfaces of the
   component and briefly identifies the consumers of these interfaces.

   @subsection DLD-bulk-server-fspec-ds Data structures

   I/O FOMs use the following data structure:

   The Bulk I/O Service is required to maintain run-time context of I/O FOMs.
   The data structure @ref c2_io_fom maintain required context data.

   - Pointer to generic FOM structure<br>
     This holds the information about generic FOM (e.g. its generic states,
     type, locality, reply FOP, file operation log etc.)
   - STOB identifier<br>
     Storage object identifier which tells the actual device on which I/O
     operation intended.
   - STOB operation vector for direct I/O<br>
     This holds the information required for async STOB I/O (e.g. data segments,
     operations, channel to signal on completion etc.)
   - Network buffers pointer<br>
     zero-copy fills this buffers in bulk write case.
   - Network buffer descriptor<br>
     Client side (passive side) network buffer descriptors. Zero-copy use this
     description while it pulls data from client in bulk write case.

   @subsection DLD-bulk-server-fspec-if Interfaces
   Bulk I/O Service will be implemented as read FOM and write FOM. Since
   request handler processes FOM, each FOM needs to define its operations:

   Bulk I/O FOP type Operations :
   @verbatim

   c2_io_fom_read_init()        Request handler uses this interface to
                                initiate read FOM.
   c2_io_fom_write_init()       Request handler uses this interface to
                                initiate write FOM.
   @endverbatim

   Bulk I/O FOM type operations:

   @verbatim
   c2_io_fom_read_create()     Request handler uses this interface to
                               create read FOM.
   c2_io_fom_write_create()    Request handler uses this interface to
                               create write FOM.
   @endverbatim

   Bulk I/O FOM operations :

   @verbatim
   c2_io_fom_locality_get()   Request handler uses this interface to
                              get the locality for this read FOM.
   c2_io_fom_read_state()     Request handler uses this interface to
                              execute next state of read FOM.
   c2_io_fom_write_state()    Request handler uses this interface to
                              execute next state of write FOM.
   c2_io_fom_read_fini()      Request handler uses this interface after
                              read FOM finishes its execution.
   c2_io_fom_write_fini()     Request handler uses this interface after
                              write FOM finishes its execution.
   @endverbatim

   Bulk I/O Service type operations :

   @verbatim

   c2_io_service_init()       This interface will be called by request handler
                              to create & initiate Bulk I/O Service.

   @endverbatim

   Bulk I/O Service operations :
   @verbatim

   c2_io_service_start()      This interface will be called by request handler
                              to start Buk I/O Service.

   c2_io_service_stop()       This interface will be called by request handler
                              to stop Buk I/O Service.

   c2_io_service_fini()       This interface will be called by request handler
                              to finish & de-allocate Bulk I/O Service.

   @endverbatim

   Bulk I/O FOM creation & initialization

   On receiving of bulk I/O FOP, FOM is created & initialized for respective
   FOP type. Then it is placed into FOM processing queue.
 */

/**
 * @defgroup io_foms Fop State Machines for IO FOPs
 *
 * Fop state machine for IO operations
 * @see fom
 * @see @ref DLD-bulk-server
 *
 * FOP state machines for various IO operations like
 * - Readv
 * - Writev
 *
 * @note Naming convention: For operation xyz, the FOP is named
 * as c2_fop_xyz, its corresponding reply FOP is named as c2_fop_xyz_rep
 * and FOM is named as c2_fom_xyz. For each FOM type, its corresponding
 * create, state and fini methods are named as c2_fom_xyz_create,
 * c2_fom_xyz_state, c2_fom_xyz_fini respectively.
 *
 *  @{
 */

#include "fop/fop.h"
#include "fop/fop_format.h"
#include "ioservice/io_fops.h"
#include "stob/stob.h"
#include "net/net.h"
#include "fop/fom.h"

/**
 * Since STOB I/O only launch io for single index vec, I/O service need
 * to launch multiple STOB I/O and wait for all to complete. I/O service
 * will put FOM execution into runqueue only after all STOB I/O complete.
 */
struct c2_stob_io_desc {
	/** Stob IO packet for the operation. */
        struct c2_stob_io        siod_stob_io;
};

/**
 * Object encompassing FOM for cob write
 * operation and necessary context data
 */
struct c2_io_fom {
	/** Generic c2_fom object. */
        struct c2_fom                    fcrw_gen;
        /** Number of desc io_fop desc list*/
        int                              fcrw_ndesc;
        /** index of net buffer descriptor under process*/
        int                              fcrw_curr_desc_index;
        /** index of index vector under process*/
        int                              fcrw_curr_ivec_index;
        /** no. of descriptor going to process */
        int                              fcrw_batch_size;
        /** Referance count for multiple async STOB I/O */
        int                              fcrw_stobio_ref; 
	/** Stob object on which this FOM is acting. */
        struct c2_stob		        *fcrw_stob;
	/** Stob IO packet for the operation. */
        struct c2_stob_io		 fcrw_st_io;
        /** rpc bulk load data*/
        struct c2_rpc_bulk               fcrw_bulk;
        /** network buffer list currently acuired by io service*/
        struct c2_tl                     fcrw_netbuf_list;
};

/**
 * The various phases for readv FOM.
 * complete FOM and reqh infrastructure is in place.
 */
enum c2_io_fom_readv_phases {
        FOPH_READ_BUF_GET = FOPH_NR + 1,
        FOPH_READ_BUF_GET_WAIT,
        FOPH_COB_READ_INIT,
        FOPH_COB_READ_WAIT,
        FOPH_ZERO_COPY_READ_INIT,
        FOPH_ZERO_COPY_READ_WAIT,
        FOPH_READ_BUF_RELEASE,
};

/**
 * The various phases for writev FOM.
 * complete FOM and reqh infrastructure is in place.
 */
enum c2_io_fom_writev_phases {
        FOPH_WRITE_BUF_GET = FOPH_NR + 1,
        FOPH_WRITE_BUF_GET_WAIT,
        FOPH_ZERO_COPY_WRITE_INIT,
        FOPH_ZERO_COPY_WRITE_WAIT,
        FOPH_COB_WRITE_INIT,
        FOPH_COB_WRITE_WAIT,
        FOPH_WRITE_BUF_RELEASE,
};

/** @} end of io_foms */

#endif /* __COLIBRI_IOSERVICE_IO_FOMS_H__ */
/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
