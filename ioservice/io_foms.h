/* -*- C -*- */
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
   The data structure @ref c2_io_fom_cob_rw maintain required context data.

   - Pointer to generic FOM structure<br>
     This holds the information about generic FOM (e.g. its generic states,
     type, locality, reply FOP, file operation log etc.)
   - Total number of descriptor for bulk data transfer requested.
   - Current network buffer descriptor index
   - Current index vector list index
   - Batch size for bulk I/O processing.
   - Actual data transferd.
   - STOB identifier<br>
     Storage object identifier which tells the actual device on which I/O
     operation intended.
   - List of STOB operation vector<br>
     This holds the information required for async STOB I/O (e.g. data segments,
     operations, channel to signal on completion etc.)
   - List acquired network buffers pointer<br>
     zero-copy fills this buffers in bulk I/O case.

   @subsection DLD-bulk-server-fspec-if Interfaces
   Bulk I/O Service will be implemented as read FOM and write FOM. Since
   request handler processes FOM, each FOM needs to define its operations:

   Bulk I/O FOM type operations:

   @verbatim
   c2_io_fom_cob_rw_create()    Request handler uses this interface to
                                create I/O FOM.
   @endverbatim

   Bulk I/O FOM operations :

   @verbatim
   c2_io_fom_cob_rw_locality_get()   Request handler uses this interface to
                                     get the locality for this I/O FOM.
   c2_io_fom_cob_rw_state()          Request handler uses this interface to
                                     execute next state of I/O FOM.
   c2_io_fom_cob_rw_fini()           Request handler uses this interface after
                                     I/O FOM finishes its execution.
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

struct c2_fid;
struct c2_fop_file_fid;
struct c2_io_fom_cob_rw;

enum {
        C2_STOB_IO_DESC_LINK_MAGIC     = 0x53544f42492f4f,
        C2_STOB_IO_DESC_HEAD_MAGIC     = 0x73746f62692f6f,
        C2_NET_BUFFER_HEAD_MAGIC_IOFOM = 0x696f666f6d,
};

/**
 * Since STOB I/O only launch io for single index vec, I/O service need
 * to launch multiple STOB I/O and wait for all to complete. I/O service
 * will put FOM execution into runqueue only after all STOB I/O complete.
 */
struct c2_stob_io_desc {
	/** Magic to verify sanity of struct c2_stob_io_desc */
	uint64_t		 siod_magic;
	/** Stob IO packet for the operation. */
        struct c2_stob_io        siod_stob_io;
        /** Linkage into c2_io_fom_cob_rw::fcrw_stobio_list */
        struct c2_tlink          siod_linkage;
        struct c2_fom_callback   siod_fcb;
};

/**
 * Object encompassing FOM for cob I/O
 * operation and necessary context data
 */
struct c2_io_fom_cob_rw {
	/** Generic c2_fom object. */
        struct c2_fom                    fcrw_gen;
        /** Number of desc io_fop desc list*/
        int                              fcrw_ndesc;
        /** index of net buffer descriptor under process*/
        int                              fcrw_curr_desc_index;
        /** index of index vector under process */
        int                              fcrw_curr_ivec_index;
        /** no. of descriptor going to process */
        int                              fcrw_batch_size;
        /** Number of bytes successfully transferred. */
        c2_bcount_t                      fcrw_count;
        /** Number of STOB I/O launched */
        int                              fcrw_num_stobio_launched;
        /** Pointer to buffer pool refered by FOM */
        struct c2_net_buffer_pool       *fcrw_bp;
	/** Stob object on which this FOM is acting. */
        struct c2_stob		        *fcrw_stob;
	/** Stob IO packets for the operation. */
        struct c2_tl                     fcrw_stio_list;
        /** rpc bulk load data*/
        struct c2_rpc_bulk               fcrw_bulk;
        /** Start time for FOM. */
        c2_time_t                        fcrw_fom_start_time;
        /** Start time for FOM specific phase. */
        c2_time_t                        fcrw_phase_start_time;
        /** network buffer list currently acquired by io service*/
        struct c2_tl                     fcrw_netbuf_list;
};

/**
 * The various phases for I/O FOM.
 * complete FOM and reqh infrastructure is in place.
 */
enum c2_io_fom_cob_rw_phases {
        C2_FOPH_IO_FOM_BUFFER_ACQUIRE = C2_FOPH_NR + 1,
        C2_FOPH_IO_FOM_BUFFER_WAIT,
        C2_FOPH_IO_STOB_INIT,
        C2_FOPH_IO_STOB_WAIT,
        C2_FOPH_IO_ZERO_COPY_INIT,
        C2_FOPH_IO_ZERO_COPY_WAIT,
        C2_FOPH_IO_BUFFER_RELEASE,
};

/**
 * State transition information.
 */
struct c2_io_fom_cob_rw_state_transition {
        /** Current phase of I/O FOM */
        int         fcrw_st_current_phase;
        /** Function which executes current phase */
        int         (*fcrw_st_state_function)(struct c2_fom *);
        /** Next phase in which FOM is going to execute */
        int         fcrw_st_next_phase_again;
        /** Next phase in which FOM is going to wait */
        int         fcrw_st_next_phase_wait;
        /** Description of phase */
        const char *fcrw_st_desc;
};

extern const struct c2_fom_type_ops c2_io_cob_rw_type_ops;

/**
 * Returns string representing ioservice name given a fom.
 */
const char *c2_io_fom_cob_rw_service_name(struct c2_fom *fom);

/**
 * Function to map the on-wire FOP format to in-core FOP format.
 * @param in Input on-wire fid fop format.
 * @param out Output in-core fid fop format.
 */
void io_fom_cob_rw_fid_wire2mem(struct c2_fop_file_fid *in,
                                struct c2_fid *out);

/**
 * Maps given fid to corresponding stob id.
 * @param in Input in-core fid.
 * @param out Output stob id.
 */
void io_fom_cob_rw_fid2stob_map(const struct c2_fid *in,
                                struct c2_stob_id *out);
void io_fom_cob_rw_stob2fid_map(const struct c2_stob_id *in,
                                struct c2_fid *out);

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
