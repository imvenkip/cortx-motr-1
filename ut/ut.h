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
 * Original author: Dmitriy Chumak <dmitriy_chumak@xyratex.com>
 * Original creation date: 11/23/2011
 */

#pragma once

#ifndef __MERO_UT_UT_H__
#define __MERO_UT_UT_H__

#include "fop/fom.h"

/**
   @defgroup mero-ut Mero UT library
   @brief Common unit test library

   The intent of this library is to include all code, which could be potentially
   useful for several UTs and thus can be shared, avoiding duplication of
   similar code.

   This library is linked as mero module and not as mero -ut library. This
   allows to use it in both - UTs and in standalone programs which don't use CUnit.

   @{
*/

int m0_ut_init(void);
void m0_ut_fini(void);

void m0_ut_fom_phase_set(struct m0_fom *fom, int phase);

/**
   @} mero-ut end group
*/

#endif /* __MERO_UT_UT_H__ */
