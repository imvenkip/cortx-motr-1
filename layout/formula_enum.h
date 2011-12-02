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
	struct c2_layout_enum			 	 lfe_enum;

	const struct c2_layout_formula			*lfe_form;
	const struct c2_layout_formula_parameter	*lfe_actuals;
};

struct c2_layout_formula {
	const struct c2_uint128			 	 lf_id;
	const struct c2_layout_formula_ops		*lf_ops;
};

struct c2_layout_formula_ops {
	int	(*lfo_subst)(const struct c2_layout_formula *form,
			     uint16_t nr,
			     const struct c2_layout_formula_parameter *actuals,
			     struct c2_layout **out);
};

struct c2_layout_formula_parameter {
	const struct c2_lay_parameter_type		*lfp_type;
	const void					*lfp_value;
};

struct c2_layout_formula_parameter_type {
	const char	*lfpt_name;

	int	(*lfpt_convert)(const struct c2_layout_formula_parameter *other,
				struct c2_layout_formula_parameter *out);
};


extern const struct c2_layout_enum_type c2_layout_formula_enum_type;
extern const struct c2_layout_formula c2_formula_NKP_formula;

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
