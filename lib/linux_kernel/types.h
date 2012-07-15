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
 * Original author: Nikita Danilov <nikita_danilov@xyratex.com>
 * Original creation date: 08/04/2010
 */

#ifndef __COLIBRI_LIB_LINUX_KERNEL_TYPES_H_
#define __COLIBRI_LIB_LINUX_KERNEL_TYPES_H_

#include <linux/types.h>

#define UINT8_MAX  ((uint8_t)(~((uint8_t) 0)))      /* 0xFF */
#define INT8_MIN   ((int8_t)(- (INT8_MAX) - 1))     /* 0x80 */
#define INT8_MAX   ((int8_t)((~(uint8_t)0) >> 1))   /* 0x7F */
#define UINT16_MAX ((uint16_t)(~((uint16_t) 0)))    /* 0xFFFF */
#define INT16_MIN  ((int16_t)(- (INT16_MAX) - 1))   /* 0x8000 */
#define INT16_MAX  ((int16_t)((~(uint16_t)0) >> 1)) /* 0x7FFF */
#define UINT32_MAX ((uint32_t)(~((uint32_t) 0)))    /* 0xFFFFFFFF */
#define INT32_MIN  ((int32_t)(- (INT32_MAX) - 1))   /* 0x80000000 */
#define INT32_MAX  ((int32_t)((~(uint32_t)0) >> 1)) /* 0x7FFFFFFF */
#define UINT64_MAX ((uint64_t)(~((uint64_t) 0)))    /* 0xFFFFFFFFFFFFFFFF */
#define INT64_MIN  ((int64_t)(- (INT64_MAX) - 1))   /* 0x8000000000000000 */
#define INT64_MAX  ((int64_t)((~(uint64_t)0) >> 1)) /* 0x7FFFFFFFFFFFFFFF */

/* __COLIBRI_LIB_LINUX_KERNEL_TYPES_H_ */
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
