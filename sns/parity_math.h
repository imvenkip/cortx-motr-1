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
 * Original author: Anatoliy Bilenko <Anatoliy_Bilenko@xyratex.com>
 * Original creation date: 10/19/2010
 * Revision       : Anup Barve <Anup_Barve@xyratex.com>
 * Revision date  : 06/14/2012
 */

#pragma once

#ifndef __COLIBRI_SNS_PARITY_MATH_H__
#define __COLIBRI_SNS_PARITY_MATH_H__

#include "lib/adt.h"
#include "matvec.h"
#include "ls_solve.h"

/**
   @defgroup parity_math Parity Math Component

   A parity math component is a part of Colibri core and serving
   several purposes:
   @li Provide algorithms for calculation of SNS parity units (checksums)
       for given data units;
   @li Provide algorithms for quick update of parity units in case of minor
       data changes;
   @li Provide algorithms for SNS repair (recovery) in case of failure.
   @{
*/

/**
 * Parity calculation type indicating various algorithms of parity calculation.
 */
enum c2_parity_cal_algo {
        C2_PARITY_CAL_ALGO_XOR,
        C2_PARITY_CAL_ALGO_REED_SOLOMON,
	C2_PARITY_CAL_ALGO_NR
};

/**
   Holds information about system configuration i.e., data and parity units data
   blocks and failure flags.
 */
struct c2_parity_math {
	enum c2_parity_cal_algo pmi_parity_algo;

	uint32_t                pmi_data_count;
	uint32_t                pmi_parity_count;

	/* structures used for parity calculation and recovery. */
	struct c2_vector        pmi_data;
	struct c2_vector        pmi_parity;
	struct c2_matrix        pmi_vandmat;
	struct c2_matrix        pmi_vandmat_parity_slice;

	/* structures used for recovery. */
	struct c2_matrix        pmi_sys_mat;
	struct c2_vector        pmi_sys_vec;
	struct c2_vector        pmi_sys_res;
	struct c2_linsys        pmi_sys;
};

/**
   Initialization of parity math algorithms.
   Fills '*math' with appropriate values.
   @param data_count - count of SNS data units used in system.
   @param parity_count - count of SNS parity units used in system.
 */
int  c2_parity_math_init(struct c2_parity_math *math,
			 uint32_t data_count, uint32_t parity_count);

/**
   Deinitializaton of parity math algorithms.
   Frees all memory blocks allocated by c2_parity_math_init().
 */
void c2_parity_math_fini(struct c2_parity_math *math);

/**
   Calculates parity block data.
   @param data[in] - data block, treated as uint8_t block with b_nob elements.
   @param parity[out] - parity block, treated as uint8_t block with
                        b_nob elements.
   @pre c2_parity_math_init() succeeded.
 */
void c2_parity_math_calculate(struct c2_parity_math *math,
			      struct c2_buf *data,
			      struct c2_buf *parity);

/**
 * Calculates parity in a differential manner.
 * @pre math != NULL && old != NULL && new != NULL && parity != NULL &&
 *      index < math->pmi_parity_count
 * @param old    Old version of data block.
 * @param new    New version of data block.
 * @param parity Parity block.
 * @param index  Index of data unit in parity group for which old and new
 * versions are sent.
 */
void c2_parity_math_diff(struct c2_parity_math *math,
			 struct c2_buf         *old,
			 struct c2_buf         *new,
			 struct c2_buf         *parity,
			 uint32_t               index);

/**
   Parity block refinement iff one data word of one data unit had changed.
   @param data[in] - data block, treated as uint8_t block with b_nob elements.
   @param parity[out] - parity block, treated as uint8_t block with
                        b_nob elements.
   @param data_ind_changed[in] - index of data unit recently changed.
   @pre c2_parity_math_init() succeeded.
 */
void c2_parity_math_refine(struct c2_parity_math *math,
			   struct c2_buf *data,
			   struct c2_buf *parity,
			   uint32_t data_ind_changed);

/**
   Recovers data or parity units' data words from single or multiple errors.
   @param data[inout] - data block, treated as uint8_t block with
			b_nob elements.
   @param parity[inout] - parity block, treated as uint8_t block with
                          b_nob elements.
   @param fail[in] - block with flags, treated as uint8_t block with
                     b_nob elements, if element is '1' then data or parity
                     block with given index is treated as broken.
   @pre c2_parity_math_init() succeded.
 */
void c2_parity_math_recover(struct c2_parity_math *math,
			    struct c2_buf *data,
			    struct c2_buf *parity,
			    struct c2_buf *fail);

/**
 * Recovers data or parity units partially or fully depending on the parity
 * calculation algorithm, given the failure index.
 * @param math - math context.
 * @param data - data block, treated as uint8_t block with b_nob elements.
 * @param parity - parity block, treated as uint8_t block with b_nob elements.
 * @param failure_index - Index of the failed block.
   @pre c2_parity_math_init() succeded.
 */
void c2_parity_math_fail_index_recover(struct c2_parity_math *math,
		                       struct c2_buf *data,
				       struct c2_buf *parity,
				       const uint32_t failure_index);


/**
 * XORs the source and destination buffers and stores the output in destination
 * buffer.
 * @param src - source buffer, treated as uint8_t block with b_nob elements.
 * @param dest - destination buffer, treated as uint8_t block with
 *               b_nob elements, containing the output of src XOR dest.
 */
void c2_parity_math_buffer_xor(const struct c2_buf *src, struct c2_buf *dest);

/** @} end group parity_math */

/* __COLIBRI_SNS_PARITY_MATH_H__  */
#endif

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
