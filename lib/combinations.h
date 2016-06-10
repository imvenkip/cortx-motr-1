/* -*- C -*- */
/*
 * COPYRIGHT 2016 XYRATEX TECHNOLOGY LIMITED, A SEAGATE COMPANY
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
 * Original author: Madhavrao Vemuri <madhav.vemuri@seagate.com>
 *                  Nachiket Sahasrabudhe <nachiket.sahasrabuddhe@seagate.com>
 * Original creation date: 10/06/2016
 */

#pragma once

#ifndef __MERO_LIB_COMBINATIONS_H__
#define __MERO_LIB_COMBINATIONS_H__

/**
 * @addtogroup comb combinations
 *
 * For a given alphabet 'A', having length 'N' containing sorted and no
 * duplicate elements, combinations are build in lexoicographical order using
 * Lehmer code.
 * Then given a combination 'X' it's index can be returned.
 * Also given a index and length of combination, combination can be returned.
 *
 * For more information on this issue visit
 * <a href="https://drive.google.com/a/seagate.com/file/d/0BxJP-hCBgo5OcU9td2Fhc2xHek0/view"> here </a>
 */

/**
 * Returns combination index of 'x' in combinations of alphabet 'A'.
 *
 * @param 'N' length of alphabet 'A' ordered elements.
 * @param 'K' lenght of combination.
 */
M0_INTERNAL int m0_combination_index(int N, int K, int *x);

/**
 * Returns the combination array 'x' for a given combination index.
 *
 * @param N is length of ordered elements.
 * @param K is length of combination.
 * @param index index of combination 'x'
 */
M0_INTERNAL void m0_index_combination(int N, int K, int index, int *x);

/**
 * Returns factorial of n.
 */
M0_INTERNAL uint64_t m0_fact(uint64_t n);

/**
 * Returns n choose r.
 */
M0_INTERNAL uint32_t m0_ncr(uint64_t n, uint64_t r);

/** @} end of comb group */
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
