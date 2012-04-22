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
 * Original creation date: 07/09/2010
 */

#ifndef __COLIBRI_LAYOUT_LAYOUT_H__
#define __COLIBRI_LAYOUT_LAYOUT_H__

/* import */
#include "lib/cdefs.h"
#include "lib/vec.h"

/**
   @defgroup layout Layouts.

   @{
 */

struct c2_layout_id;
struct c2_layout_formula;
struct c2_layout_parameter;
struct c2_layout_parameter_type;
struct c2_layout;
struct c2_layout_ops;
struct c2_layout_type;

struct c2_layout_ops;
struct c2_layout_formula_ops;

struct c2_layout_formula {
	const struct c2_layout_type        *lf_type;
	const struct c2_uint128             lf_id;
	const struct c2_layout_formula_ops *lf_ops;
};

struct c2_layout_formula_ops {
	int (*lfo_subst)(const struct c2_layout_formula *form, uint16_t nr,
			 const struct c2_layout_parameter *actuals,
			 struct c2_layout **out);
};

struct c2_layout {
	const struct c2_layout_type      *l_type;
	const struct c2_layout_formula   *l_form;
	const struct c2_layout_parameter *l_actuals;
	struct c2_uint128                 l_id;
	const struct c2_layout_ops       *l_ops;
};

void c2_layout_init(struct c2_layout *lay);
void c2_layout_fini(struct c2_layout *lay);

struct c2_layout_parameter {
	const struct c2_layout_parameter_type *lp_type;
	const void                            *lp_value;
};

struct c2_layout_parameter_type {
	const char *lpt_name;
	int       (*lpt_convert)(const struct c2_layout_parameter *other,
				 struct c2_layout_parameter *out);
};

struct c2_layout_ops {
};

struct c2_layout_type {
	const char  *lt_name;
	bool       (*lt_equal)(const struct c2_layout *l0,
			       const struct c2_layout *l1);
};

int  c2_layouts_init(void);
void c2_layouts_fini(void);

/** @} end group layout */

/* __COLIBRI_LAYOUT_LAYOUT_H__ */
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
