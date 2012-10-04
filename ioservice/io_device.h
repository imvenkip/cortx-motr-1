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
 * Original author: Huang Hua <Hua_Huang@xyratex.com>
 * Original creation date: 28/06/2012
 */

#pragma once

#ifndef __COLIBRI_IOSERVICE_IO_DEVICE_H__
#define __COLIBRI_IOSERVICE_IO_DEVICE_H__

struct c2_fv_version;
struct c2_fv_updates;

/**
   @page io_calls_params_dld-fspec I/O calls Parameters Functional Specification

   - @ref io_calls_params_dld-fspec-ds
   - @ref io_calls_params_dldDFS "Detailed Functional Specification"

   @section DLD-fspec-ds Data Structures
   - c2_fop_cob_rw
   - c2_fop_cob_rw_reply
   - c2_fop_cob_common
   - c2_fop_cob_op_reply

   The followsing code is used to present the failure vector version which is
   tagged to every i/o request, and failure vector updates which is replied
   to clients or other services.

   @code

   DEF(c2_fv_version, RECORD,
          _(fvv_read, U64),
          _(fvv_write, U64));

   DEF(c2_fv_event, RECORD,
	_(fve_type, U32),
	_(fve_index, U32),
	_(fve_state, U32));

   DEF(c2_fv_updates, SEQUENCE,
          _(fvu_count, U32),
          _(fvu_events, struct c2_fv_event));

   @endcode

   The failure vector updates are transferred on network as a sequence of
   byte stream. The serialization and un-serialization is not designed in this
   document.

   The listed I/O requests will embed the failure vector version and the I/O
   replies will embed the failure vector updates. The failure updates will not
   exist for every reply. Actually for normal replies, they don't have the
   failure vector updates. Only then the I/O services have detected the failure
   vector version unmatched, a special error code is returned, along with the
   failure vector updates.

   @see @ref io_calls_params_dldDFS "Detailed Functional Specification"
   @see @ref poolmach "pool machine"
 */

/**
   @defgroup io_calls_params_dldDFS Detailed Functional Specification

   @see The @ref io_calls_params_dld "DLD of I/O calls Parameters" its
   @ref io_calls_params_dld-fspec "Functional Specification"

   @{
 */
struct c2_reqh;
struct c2_poolmach;

enum {
	/**
	 * i/o reply error code to indicate the client known failure vector
	 * version is mismatch with the server's.
	 */
	C2_IOP_ERROR_FAILURE_VECTOR_VERSION_MISMATCH = -1001
};

/**
 * Initializes the pool machine. This will create a shared reqh key
 * and call c2_poolmach_init() internally.
 */
int c2_ios_poolmach_init(struct c2_reqh *reqh);

/**
 * Gets the shared pool machine.
 */
struct c2_poolmach *c2_ios_poolmach_get(struct c2_reqh *reqh);

/**
 * Finializes the pool machine when it is no longer used.
 */
void c2_ios_poolmach_fini(struct c2_reqh *reqh);

/**
 * Pack the current server version and delta of failure vectors
 * into (reply) buffers.
 * @param pm the pool machine.
 * @param cli the client known version.
 * @param version [out] pack the server known version into this.
 * @param updates [out] pack events from @cli to @version into this buffer.
 */
int c2_ios_poolmach_version_updates_pack(struct c2_poolmach         *pm,
					 const struct c2_fv_version *cli,
					 struct c2_fv_version       *version,
					 struct c2_fv_updates       *updates);

/** @} */ /* io_calls_params_dldDFS end group */

#endif /*  __COLIBRI_IOSERVICE_IO_DEVICE_H__ */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 79
 *  scroll-step: 1
 *  End:
 */
