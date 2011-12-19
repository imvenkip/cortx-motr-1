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
 * Original author: Trupti Patil <trupti_patil@xyratex.com>
 * Original creation date: 11/16/2011
 */

#ifndef __COLIBRI_LAYOUT_FORMULA_ENUM_H__
#define __COLIBRI_LAYOUT_FORMULA_ENUM_H__

/**
   @defgroup formula_enum Formula Enumeration Type.

   A layout with formula enumeration type stores a formula that is
   used to enumerate all the component object identifiers.

   @{
*/

/* import */
#include "layout/layout.h"

/* export */
struct c2_layout_formula_enum;

/**
   Extension of generic c2_layout_enum for a formula enumeration type.
 */
struct c2_layout_formula_enum {
	/** super class */
	struct c2_layout_enum				 lfe_enum;

};


extern const struct c2_layout_enum_type c2_formula_enum_type;

/** @} end group formula_enum */

/* __COLIBRI_LAYOUT_FORMULA_ENUM_H__ */
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
