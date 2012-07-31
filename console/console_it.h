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
 * Original author: Dipak Dudhabhate <dipak_dudhabhate@xyratex.com>
 * Original creation date: 08/17/2011
 */

#ifndef __COLIBRI_CONSOLE_IT_H__
#define __COLIBRI_CONSOLE_IT_H__

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include "fop/fop_base.h" /* c2_fop_field_type */
#include "xcode/xcode.h"

/**
   @addtogroup console_it
   @{
 */

enum c2_cons_data_process_type {
	CONS_IT_INPUT,
	CONS_IT_OUTPUT,
	CONS_IT_SHOW
};

/**
 * @struct c2_cons_atom_ops
 *
 * @brief operation to get/set values of ATOM type (i.e. CHAR, U64 etc).
 */
struct c2_cons_atom_ops {
	void (*catom_val_get)(const struct c2_xcode_type *xct,
			      const char *name, void *data);
	void (*catom_val_set)(const struct c2_xcode_type *xct,
			      const char *name, void *data);
	void (*catom_val_show)(const struct c2_xcode_type *xct,
			       const char *name, void *data);
};

/**
 * @brief Iterate over FOP fields and prints the names.
 *
 * @param fop fop object.
 */
void c2_cons_fop_fields_show(struct c2_fop *fop);

/**
 * @brief Iterate over FOP for Input and output.
 *
 * @param fop Fop object
 * @param type Data processing operation type
 */
void c2_cons_fop_obj_input_output(struct c2_fop *fop,
				  enum c2_cons_data_process_type type);

/**
 * @brief Helper function for FOP input
 *
 * @param fop fop object.
 */
void c2_cons_fop_obj_input(struct c2_fop *fop);

/**
 * @brief Helper function for FOP output.
 *
 * @param fop fop object.
 */
void c2_cons_fop_obj_output(struct c2_fop *fop);

/** @} end of console_it */

/* __COLIBRI_CONSOLE_IT_H__ */
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

