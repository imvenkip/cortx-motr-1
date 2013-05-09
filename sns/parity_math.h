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
 * Original author: Anatoliy Bilenko <Anatoliy_Bilenko@xyratex.com>
 * Original creation date: 10/19/2010
 * Revision       : Anup Barve <Anup_Barve@xyratex.com>
 * Revision date  : 06/14/2012
 */

#pragma once

#ifndef __MERO_SNS_PARITY_MATH_H__
#define __MERO_SNS_PARITY_MATH_H__

#include "lib/adt.h"
#include "lib/vec.h"
#include "lib/bitmap.h"
#include "lib/tlist.h"
#include "matvec.h"
#include "ls_solve.h"

/**
   @defgroup parity_math Parity Math Component

   A parity math component is a part of Mero core and serving
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
enum m0_parity_cal_algo {
        M0_PARITY_CAL_ALGO_XOR,
        M0_PARITY_CAL_ALGO_REED_SOLOMON,
	M0_PARITY_CAL_ALGO_NR
};

enum m0_sns_ir_block_status {
	M0_SI_BLOCK_ALIVE,
	M0_SI_BLOCK_FAILED,
	M0_SI_BLOCK_RESTORED,
};

/**
 * Every member of a parity-group is called as a block. During incremental
 * recovery m0_sns_ir_block holds that information associated with a block
 * which is relevant for recovery. This involves block-index
 * within a parity-group, a pointer to information held by the block, a pointer
 * to recovery-matrix associated with the block and so on.
 */
struct m0_sns_ir_block {
	/* Index of a block within parity group. */
	uint32_t		     sib_idx;
	/* Data from a block is submitted to incremental-recovery-module using
	 * struct m0_bufvec. sib_addr holds address for the same.
	 */
	struct m0_bufvec            *sib_addr;
	/* Whenever a block fails, sib_bitmap holds block indices of
	 * blocks required for its recovery.
	 */
	struct m0_bitmap	     sib_bitmap;
	/* Column associated with the block within
	 * m0_parity_math::pmi_data_recovery_mat. This field is meaningful
	 * when status of a block is M0_SI_BLOCK_ALIVE.
	 */
	uint32_t		     sib_data_recov_mat_col;
	/* Row associated with the block within its own recovery matrix.
	 * The field is meaningful when status of a block is M0_SI_BLOCK_FAILED.
	 */
	uint32_t		     sib_recov_mat_row;
	/* Indicates whether a block is available, failed or restored. */
	enum m0_sns_ir_block_status  sib_status;
};

/**
   Holds information about system configuration i.e., data and parity units
   data blocks and failure flags.
 */
struct m0_parity_math {
	enum m0_parity_cal_algo	     pmi_parity_algo;

	uint32_t		     pmi_data_count;
	uint32_t		     pmi_parity_count;
	/* structures used for parity calculation and recovery */
	struct m0_vector	     pmi_data;
	struct m0_vector	     pmi_parity;
	/* Vandermonde matrix */
	struct m0_matrix	     pmi_vandmat;
	/* Submatrix of Vandermonde matrix used to compute parity. */
	struct m0_matrix	     pmi_vandmat_parity_slice;
	/* structures used for non-incremental recovery */
	struct m0_matrix	     pmi_sys_mat;
	struct m0_vector	     pmi_sys_vec;
	struct m0_vector	     pmi_sys_res;
	struct m0_linsys	     pmi_sys;
};

/* Holds information essential for incremental recovery. */
struct m0_sns_ir {
	uint32_t		si_data_nr;
	uint32_t		si_parity_nr;
	uint32_t		si_failed_data_nr;
	uint32_t		si_alive_nr;
	/* Array holding all blocks */
	struct m0_sns_ir_block *si_blocks;
	/* Vandermonde matrix used during RS encoding */
	struct m0_matrix	si_vandmat;
	/* Recovery matrix for failed data blocks */
	struct m0_matrix	si_data_recovery_mat;
	/* Recovery matrix for failed parity blocks. This is same as
	 * math::pmi_vandmat_parity_slice.
	 */
	struct m0_matrix	si_parity_recovery_mat;
};

/**
   Initialization of parity math algorithms.
   Fills '*math' with appropriate values.
   @param data_count - count of SNS data units used in system.
   @param parity_count - count of SNS parity units used in system.
 */
M0_INTERNAL int m0_parity_math_init(struct m0_parity_math *math,
				    uint32_t data_count, uint32_t parity_count);

/**
   Deinitializaton of parity math algorithms.
   Frees all memory blocks allocated by m0_parity_math_init().
 */
M0_INTERNAL void m0_parity_math_fini(struct m0_parity_math *math);

/**
   Calculates parity block data.
   @param[in]  data - data block, treated as uint8_t block with b_nob elements.
   @param[out] parity - parity block, treated as uint8_t block with
                        b_nob elements.
   @pre m0_parity_math_init() succeeded.
 */
M0_INTERNAL void m0_parity_math_calculate(struct m0_parity_math *math,
					  struct m0_buf *data,
					  struct m0_buf *parity);

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
M0_INTERNAL void m0_parity_math_diff(struct m0_parity_math *math,
				     struct m0_buf *old,
				     struct m0_buf *new,
				     struct m0_buf *parity, uint32_t index);

/**
   Parity block refinement iff one data word of one data unit had changed.
   @param data[in] - data block, treated as uint8_t block with b_nob elements.
   @param parity[out] - parity block, treated as uint8_t block with
                        b_nob elements.
   @param data_ind_changed[in] - index of data unit recently changed.
   @pre m0_parity_math_init() succeeded.
 */
M0_INTERNAL void m0_parity_math_refine(struct m0_parity_math *math,
				       struct m0_buf *data,
				       struct m0_buf *parity,
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
   @pre m0_parity_math_init() succeded.
 */
M0_INTERNAL void m0_parity_math_recover(struct m0_parity_math *math,
					struct m0_buf *data,
					struct m0_buf *parity,
					struct m0_buf *fail);

/**
 * Recovers data or parity units partially or fully depending on the parity
 * calculation algorithm, given the failure index.
 * @param math - math context.
 * @param data - data block, treated as uint8_t block with b_nob elements.
 * @param parity - parity block, treated as uint8_t block with b_nob elements.
 * @param failure_index - Index of the failed block.
   @pre m0_parity_math_init() succeded.
 */
M0_INTERNAL void m0_parity_math_fail_index_recover(struct m0_parity_math *math,
						   struct m0_buf *data,
						   struct m0_buf *parity,
						   const uint32_t
						   failure_index);

/**
 * XORs the source and destination buffers and stores the output in destination
 * buffer.
 * @param dest - destination buffer, treated as uint8_t block with
 *               b_nob elements, containing the output of src XOR dest.
 * @param src - source buffer, treated as uint8_t block with b_nob elements.
 */
M0_INTERNAL void m0_parity_math_buffer_xor(struct m0_buf *dest,
					   const struct m0_buf *src);

/**
 * @defgroup incremental_recovery Incremental recovery APIs
 * @{
 *  @section incremental_recovery-highlights Highlights
 *  - Algorithm is distributed.
 *  - Algorithm is not symmetric for data and parity blocks.
 *  - Vandermonde matrix is used as an encoding matrix for Reed-Solomon
 *    encoding.
 *  @section incremental_recovery-discussion Discussion
 *  After encountering a failure, SNS client initializes the incremental
 *  recovery module by calling m0_sns_ir_init(). Followed by that it reports
 *  failed blocks with m0_sns_ir_failure_register().
 *  Once all failed bocks are registered, a call to m0_sns_ir_mat_compute()
 *  is placed. During this initialization the recovery module constructs
 *  a recovery matrix associated with failed blocks.
 *
 *  Once initialization is over, client passes available blocks to recovery
 *  module using the function m0_sns_ir_recover(). This function uses the fact
 *  that a lost block can be represented as a linear combination of alive
 *  blocks.  It keeps accumulating data from input alive blocks, as SNS client
 *  passes it.  Various parameters to m0_sns_ir_recover() tell the recovery
 *  routine how to use the incoming alive block for recovery. We will see how
 *  m0_sns_ir_recover() works using a simple example. Suppose we have four
 *  blocks in a parity group, b0 to b3, of which two have failed
 *  (say b0 and b1). Assume that these blocks satisfy following equations:
 *
 *  @code
 *    b0 = u0.b2 + u1.b3
 *    b1 = u2.b2 + u3.b3
 *  @endcode
 *  Here each u_{i} is a  constant coefficient. When SNS client submits the
 *  block b2 to m0_sns_ir_recover(), it updates b0 and b1 as below:
 *
 *  @code
 *    b0 = u0.b2
 *    b1 = u2.b2
 *  @endcode
 *
 *  Next, when block b3 gets submitted, m0_sns_ir_recover() adds its
 *  contribution cumulatively.
 *
 *  @code
 *  b0 += u1.b3
 *  b1 += u3.b3
 *  @endcode
 *
 *  We call this mode of recovery as incremental recovery, where failed blocks
 *  get recovered incrementally for each input block. When this algorithm works
 *  on multiple nodes, it works in two phases. In the first phase each
 *  node computes incremental contribution towards all lost blocks using blocks
 *  available on it. After this, nodes share their incrementally recovered
 *  blocks with spare nodes for various lost blocks. Spare node then adds
 *  contributions sent by other nodes with its own contribution to restore
 *  a lost block.
 *
 *  In all, there are three use-cases as below
 *	 -# All failed blocks are data blocks.
 *	 -# All failed blocks are parity blocks.
 *	 -# Failed blocks are mixture of data and parity blocks.
 *
 *  It is worth noting that recovery of a lost parity block requires all data
 *  blocks. Hence in the third case above, we first require to recover all lost
 *  data blocks and then use recovered data blocks for restoring lost parity
 *  blocks. In order to achieve this, incremental-recovery module not only
 *  recovers lost data blocks on their respective spare nodes but also on spare
 *  nodes for lost parity blocks.
 *
 *  SNS client being unaware of dependencies between lost and available blocks,
 *  submits all available blocks to recovery module. When contribution by all
 *  available blocks is received at a spare node for a lost block, then
 *  SNS client marks recovery of that lost block as complete.
 *
 *  @section incremental_recovery-use-case Use-case
 *  This section covers in detail how incremental recovery module can be used
 *  in various scenarios.
 *  Consider the case of six data blocks and two parity blocks, distributed
 *  over four nodes. Assume following mapping between nodes and data/parity
 *  blocks. Ni represents the i^th node, pi represents the i^th parity block
 *  and di represents the i^th data block. Xi represent the i^th spare node.
 *
    @verbatim
			 ____________________
			*		     *
			*  d0, p0     --> N0 *
			*  d1, p1     --> N1 *
			*  d2, d3, d5 --> N2 *
			*  d4         --> N3 *
			*                 X0 *
			*	          X1 *
			*____________________*
    @endverbatim
 *
 * Suppose blocks d2 and p1 are lost. Let us assume that recovery of d2 is
 * mapped to X0 and that of p1 is mapped to X1. In the first phase each node
 * incrementally recovers d2 and p1 locally, except the node N3. Since N3 has
 * only a single block, d4, it communicates it with X0 and X1 without going
 * through any of the operations that incremental recovery involves.
 * All other nodes initiate the recovery process using following sequence of
 * API's:
 *
 * @code
 * struct m0_sns_ir	 ir;
 * struct m0_parity_math math;
 * :
 * :
 * ret = m0_sns_ir_init(&math, &ir);
 * if (ret != 0)
 *	 goto handle_error;
 * // Register the failure of block d2 and provide in-memory location for its
 * // recovery.
 * ret = m0_sns_ir_failure_register(bufvec_d2, d2, &ir);
 * if (ret != 0)
 *	goto handle_error;
 * // Register the failure of block p1 and provide in-memory location for its
 * // recovery.
 * ret = m0_sns_ir_failure_register(bufvec_p1, p1, &ir);
 * if (ret != 0)
 *	goto handle_error;
 * // Construct recovery matrix for lost blocks.
 * ret = m0_sns_ir_mat_compute(&ir);
 * if (ret != 0)
 *	goto handle_error;
 * @endcode
 *
 * We will now consider calls at N0, X0, and X1. Generalizing these calls for
 * other nodes is fairly easy and we will not discuss it here.
 * Node N0 incrementally recovers d2 using d0 and p0, and sends it to X0 and
 * X1. Followed by this it incrementally recovers p1 using d0 and p0, and sends
 * it to X1. With every input block, recovery module expects a bitmap holding
 * indices of blocks that contributed in the input block. Whenever any data or
 * parity block is submitted to recovery module, it utilizes it for incremental
 * recovery of all failed blocks that are registered. When input block is a
 * linear combination of subset of available blocks then its use is restricted
 * for only one failed block. In such a case recovery module expects index of
 * the failed block as an input.
 *
 * @code
 * // Incrementally recovering d2 and p1 using d0.
 * m0_bitmap_set(bitmap_d0, d0, true);
 * m0_sns_ir_recover(ir, bufvec_d0, bitmap_d0, do_not_care);
 * // SNS client updates bitmaps associated with accumulator buffers for d2 and
 * // p1.
 * m0_bitmap_set(bitmap_d2, d0, true);
 * m0_bitmap_set(bitmap_p1, d0, true);
 * // Incrementally recovering d2 and p1 using p0.
 * m0_bitmap_set(bitmap_p0, p0, true);
 * m0_sns_ir_recover(ir, bufvec_p0, bitmap_p0, do_not_care);
 * // SNS client updates bitmap associated with accumulator buffers for d2 and
 * // p1.
 * m0_bitmap_set(bitmap_d2, p0, true);
 * m0_bitmap_set(bitmap_p1, p0, true);
 * @endcode
 *
 * It is worth noting that although recovery of a parity block is not
 * dependent on other parity block, SNS client submits p0 for recovering
 * p1. This is because SNS client is unaware of dependencies between available
 * and failed blocks. Recovery module detects such redundant submissions
 * internally, and proceeds with no-operation.
 *
 * Once local computations are over, N0 sends bufvec_d2 along with bitmap_d2 to
 * X0 and X1. It then sends bufvec_p1 and bitmap_p1 to X1.
 *
 * At node X0, after receiving a pair (bufvec_d2, bitmap_d2)  form N0, a
 * a call is placed to m0_sns_ir_recover() with following arguments.
 *
 * @code
 * m0_sns_ir_recover(ir, bufvec_d2, bitmap_d2, d2);
 * @endcode
 *
 * Since bufvec_d2 from N0 is a linear combination of blocks d0 and p0,
 * recovery module can not use it for recovery of all registered failed blocks.
 * Hence it expects the index of a failed block for which bufvec_d2 can be
 * used. X0 maintains its own accumulator buffer for  incrementally recovered
 * d2. The above call to m0_sns_ir_recover() cumulatively adds copy of
 * bufvec_d2 received from N0 to X0's accumulator buffer. X0 then updates
 * bitmap for the same.
 * At X1, when it receives (bufvec_d2, bitmap_d2) and (bufvec_p1, bitmap_p1)
 * from N0 it places calls as follows:
 *
 * @code
 * m0_sns_ir_recover(ir, bufvec_d2, bitmap_d2, d2);
 * m0_sns_ir_recover(ir, bufvec_p1, bitmap_p1, p1);
 * @endcode
 * When contribution from all blocks required for recovery of d2 is received,
 * m0_sns_ir_recover() internally uses d2 for incrementally recovering p1.
 * When all spare nodes receive all available blocks, then recovery gets over.
 * When recovery gets over, node places a call to m0_sns_ir_fini().
 **/

/**
 * Marks a failed block and populates the m0_parity_math_block structure
 * associated with the failed block accordingly.
 * @pre  block_idx < total_blocks
 * @pre  total failures are less than maximum supported failures.
 * @param[in] recov_addr    address at which failed block is to be restored.
 * @param[in] failed_index  index of the failed block in a parity group.
 * @param     ir	    holds recovery_matrix and other data for recovery.
 * @retval    0		    on success.
 * @retval    -EDQUOT       recovery is not possible, as number failed units
 *			    exceed number of parity units.
 */
M0_INTERNAL int m0_sns_ir_failure_register(struct m0_bufvec *recov_addr,
					   uint32_t failed_index,
					   struct m0_sns_ir *ir);

/**
 * Populates the structure m0_sns_ir with fields relevant for recovery.
 * @param[in]  math
 * @param[out] ir
 * @retval     0       on success
 * @retval     -ENOMEM when it fails to allocate array of m0_sns_ir_block or
 *		       when initialization of bitmap fails.
 */
M0_INTERNAL int m0_sns_ir_init(const struct m0_parity_math *math,
			       struct m0_sns_ir *ir);
/**
 * Computes data-recovery matrix. Populates dependency bitmaps for failed
 * blocks.
 * @retval     0      on success.
 * @retval    -ENOMEM on failure to acquire memory.
 * @retval    -EDOM   when input matrix is singular.
 */
M0_INTERNAL int m0_sns_ir_mat_compute(struct m0_sns_ir *ir);

/**
 * Computes the lost data incrementally using an incoming block.
 * @pre ergo(m0_bitmap_set_nr >  1, failed_index < total_blocks)
 * @param     ir	    holds recovery matrix, and destination address for
 *			    a failed block(s) to be recovered.
 * @param[in] bufvec        input bufvec to be used for recovery. This can be
 *			    one of the available data/parity blocks or
 *			    a linear combination of a subset of them.
 * @param[in] bitmap        indicates which of the available data/parity
 *			    blocks have contributed towards the input
 *			    bufvec.
 * @param[in] failed_index  index of a failed block for which the input bufvec
 *			    is to be used.
 */
M0_INTERNAL void m0_sns_ir_recover(struct m0_sns_ir *ir,
				   struct m0_bufvec *bufvec,
				   const struct m0_bitmap *bitmap,
				   uint32_t failed_index);

M0_INTERNAL void m0_sns_ir_fini(struct m0_sns_ir *ir);
/** @} end group Incremental recovery APIs */

/** @} end group parity_math */

/* __MERO_SNS_PARITY_MATH_H__  */
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
