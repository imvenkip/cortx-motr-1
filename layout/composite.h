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

#ifndef __COLIBRI_LAYOUT_COMPOSITE_H__
#define __COLIBRI_LAYOUT_COMPOSITE_H__

/**
   @defgroup composite Composite Layout Type.

   Composite layout. Composite layout is made up of multiple sub-layouts. Each
   sub-layout needs to be read to obtain the overall layout details providing
   all the COB identifiers.

   @{
 */

/* import */
#include "lib/mutex.h"	/* struct c2_mutex */
#include "db/extmap.h"	/* struct c2_emap */

#include "layout/layout.h"

/* export */
struct c2_composite_layout;

/**
   Extension of generic c2_layout for a composite layout.
 */
struct c2_composite_layout {
	/** super class */
	struct c2_layout          cl_layout;

	/** List of sub-layouts owned by this composite layout */
	struct c2_tl              cl_sub_layouts;

	/** Lock to protect the list of sub-layouts */
	struct c2_mutex           cl_sub_layouts_mutex;
};

extern const struct c2_layout_type c2_composite_layout_type;

/** @} end group composite */

/* __COLIBRI_LAYOUT_COMPOSITE_H__ */
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
