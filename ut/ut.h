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
 * Original author: Dmitriy Chumak <dmitriy_chumak@xyratex.com>
 * Original creation date: 11/23/2011
 */

#ifndef __COLIBRI_UT_UT_H__
#define __COLIBRI_UT_UT_H__

/**
   @defgroup colibri-ut Colibri UT library
   @brief Common unit test library

   The intent of this library is to include all code, which could be potentially
   useful for several UTs and thus can be shared, avoiding duplication of
   similar code.

   This library is linked as colibri module and not as colibri -ut library. This
   allows to use it in both - UTs and in standalone programs which don't use CUnit.

   Purpose of this header file is to include all other headers under ut/

   @{
*/

#include "ut/rpc.h"
#include "ut/cs_fop_foms.h"
#include "ut/cs_service.h"
#include "ut/cs_test_fops_u.h"

/**
   @} colibri-ut end group
*/

#endif /* __COLIBRI_UT_UT_H__ */

