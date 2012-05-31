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
 * Original author: Trupti Patil <trupti_patil@xyratex.com>
 * Original creation date: 11/16/2011
 */

#ifndef __COLIBRI_LAYOUT_LINEAR_ENUM_H__
#define __COLIBRI_LAYOUT_LINEAR_ENUM_H__

/**
 * @defgroup linear_enum Linear Enumeration Type.
 *
 * A layout with linear enumeration type stores a linear formula that is
 * used to enumerate all the component object identifiers.
 *
 * @{
 */

/* import */
#include "layout/layout.h"

/* export */
struct c2_layout_linear_enum;

/**
 * Attributes specific to Linear enumeration type.
 * These attributes are part of c2_layout_linear_enum which is in-memory layout
 * enumeration object and are stored in the Layout DB as well.
 */
struct c2_layout_linear_attr {
	/** Number of elements present in the enumeration. */
	uint32_t   lla_nr;

	/** Constant A used in the linear equation A + idx * B. */
	uint32_t   lla_A;

	/** Constant B used in the linear equation A + idx * B. */
	uint32_t   lla_B;
};

/**
 * Extension of generic c2_layout_enum for a linear enumeration type.
 */
struct c2_layout_linear_enum {
	/** Super class. */
	struct c2_layout_enum        lle_base;

	struct c2_layout_linear_attr lle_attr;

	uint64_t                     lla_magic;
};

int c2_linear_enum_build(struct c2_layout_domain *dom,
			 uint32_t nr, uint32_t A, uint32_t B,
			 struct c2_layout_linear_enum **out);

extern struct c2_layout_enum_type c2_linear_enum_type;

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
