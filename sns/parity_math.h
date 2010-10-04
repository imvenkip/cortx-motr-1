#ifndef __COLIBRI_SNS_PARITY_MATH_H__ 
#define __COLIBRI_SNS_PARITY_MATH_H__

#include "lib/adt.h"
#include "matvec.h"
#include "ls_solve.h"

/**
   @defgroup parity_math Parity Math Component
   
   A parity math component is a part of Colibri core and serving several purposes:
   @li Provide algorithms for calculation of SNS parity units (checksums) for given data units;
   @li Provide algorithms for quick update of parity units in case of minor data changes;
   @li Provide algorithms for SNS repair (recovery) in case of failure.   
   @{
*/

/* Implementation of c2_parity_math */
struct c2_parity_math_impl;

/**
   Holds information about system configuration i.e., data and parity units data
   blocks and failure flags.
 */
struct c2_parity_math {
	struct c2_parity_math_impl *pm_impl;
};


enum {
	/**
	   Returned if data and parity units are not broken
	 */
	C2_SNS_PARITY_MATH_RECOVERY_IS_NOT_NEDDED = 102
};

/**
   Initialization of parity math algorithms.
   Fills '*math' with appropriate values.
   @param data_count - count of SNS data units used in system
   @param parity_count - count of SNS parity units used in system
   @return 0 for success, -C2_SNS_PARITY_MATH_* codes or -ENOMEM for fail
 */
int  c2_parity_math_init(struct c2_parity_math *math,
			 uint32_t data_count, uint32_t parity_count);

/**
   Deinitializaton of parity math algorithms.
   Frees all memory blocks allocated by c2_parity_math_init()
 */
void c2_parity_math_fini(struct c2_parity_math *math);

/**
   Calculates parity block data.
   @param data[in] - data block, treated as uint8_t block with b_nob elements
   @param parity[out] - parity block, treated as uint8_t block with b_nob elements
   @pre c2_parity_math_init() succeeded
 */
void c2_parity_math_calculate(struct c2_parity_math *math,
			      struct c2_buf data[],
			      struct c2_buf parity[]);

/**
   Parity block refinement iff one data word of one data unit had changed.
   @param data[in] - data block, treated as uint8_t block with b_nob elements
   @param parity[out] - parity block, treated as uint8_t block with b_nob elements
   @param data_ind_changed[in] - index of data unit recently changed.
   @pre c2_parity_math_init() succeeded
 */
void c2_parity_math_refine(struct c2_parity_math *math,
			   struct c2_buf data[],
			   struct c2_buf parity[],
			   uint32_t data_ind_changed);

/**
   Recovers data or parity units' data words from single or multiple errors.
   @param data[inout] - data block, treated as uint8_t block with b_nob elements
   @param parity[inout] - parity block, treated as uint8_t block with b_nob elements
   @param fail[in] - array of flags, if element of this array is '1' then block is treated as broken
   @pre c2_parity_math_init() succeded
   @return 0 for success, -C2_SNS_PARITY_MATH_* codes or -ENOMEM for fail
 */
int  c2_parity_math_recover(struct c2_parity_math *math,
			    struct c2_buf data[],
			    struct c2_buf parity[],
			    struct c2_buf *fail);

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
