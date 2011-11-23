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

#ifndef __COLIBRI_LAYOUT_LINEAR_ENUM_H__
#define __COLIBRI_LAYOUT_LINEAR_ENUM_H__

#include "layout/layout.h"

/**
   @defgroup linear_enum Linear Enumeration Type.

   A layout with linear enumeration type stores a formula that is
   used to enumerate all the component object identifiers. Thus, this
   type of a layout does not store the component object identifiers.
   @{
*/

/* import */

/* export */

/**
   Extension of generic c2_layout_enum for a linear enumeration type.
 */
struct c2_layout_linear_enum {
	/** super class */
	struct c2_layout_enum         lline_enum;

	const struct c2_layout_linear_formula *lline_form;
	const struct c2_layout_linear_parameter *lline_actuals;
};

struct c2_layout_linear_formula {
	const struct c2_uint128                    llinf_id;
	const struct c2_layout_linear_formula_ops *llinf_ops;
};

struct c2_layout_linear_formula_ops {
	int (*llinfo_subst)(const struct c2_layout_linear_formula *form,
			    uint16_t nr,
			    const struct c2_layout_linear_parameter *actuals,
			    struct c2_layout **out);
};

struct c2_layout_linear_parameter {
	const struct c2_layout_parameter_type *llinp_type;
	const void                            *llinp_value;
};

struct c2_layout_linear_parameter_type {
	const char *llinpt_name;
	int       (*llinpt_convert)(const struct c2_layout_linear_parameter *other,
				    struct c2_layout_linear_parameter *out);
};

extern const struct c2_layout_enum_type c2_layout_linear_enum_type;
extern const struct c2_layout_linear_formula c2_linear_NKP_formula;

/** @} end group linear_enum */

/* __COLIBRI_LAYOUT_LINEAR_ENUM_H__ */
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
