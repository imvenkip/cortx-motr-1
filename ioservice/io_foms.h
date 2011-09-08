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
 */

#ifndef __COLIBRI_IOSERVICE_IO_FOMS_H__
#define __COLIBRI_IOSERVICE_IO_FOMS_H__

/**
 * @defgroup io_foms Fop State Machines for various FOPs
 *
 * Fop state machine for IO operations
 * @see fom
 * @ref https://docs.google.com/a/xyratex.com/Doc?docid=0AQaCw6YRYSVSZGZmMzV6NzJfMTNkOGNjZmdnYg&hl=en
 *
 * FOP state machines for various IO operations like
 * COB Readv
 * COB Writev
 *
 * All operation specific code will be executed in a single phase
 * for now. It will be decomposed into more granular phases
 * when FOM and reqh infrastructure is in place.
 *
 * @note Naming convention: For operation xyz, the fop is named
 * as c2_fop_xyz, its corresponding reply fop is named as c2_fop_xyz_rep
 * and fom is named as c2_fom_xyz. For each fom type, its corresponding
 * create, state and fini methods are named as c2_fom_xyz_create,
 * c2_fom_xyz_state, c2_fom_xyz_fini respectively.
 *
 *  @{
 */

#include "fop/fop.h"
#include "fop/fop_format.h"
#include "ioservice/io_fops.h"
#include "stob/stob.h"

/**
 * Object encompassing FOM for cob write
 * operation and necessary context data
 */
struct c2_io_fom_cob_rwv {
	/** Generic c2_fom object. */
        struct c2_fom                    fcrw_gen;
	/** Stob object on which this FOM is acting. */
        struct c2_stob		        *fcrw_stob;
	/** Stob IO packet for the operation. */
        struct c2_stob_io		 fcrw_st_io;
};

/**
 * The various phases for readv FOM.
 * Not used as of now. Will be used once the
 * complete FOM and reqh infrastructure is in place.
 */
enum c2_io_fom_cob_rwv_phases {
	FOPH_COB_IO = FOPH_NR + 1,
	FOPH_COB_IO_WAIT,
};

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
