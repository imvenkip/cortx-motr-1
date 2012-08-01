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
 * Original author: Nikita Danilov <nikita_danilov@xyratex.com>
 * Original creation date: 05/19/2010
 */

#pragma once

#ifndef __COLIBRI_FOP_FOP_FORMAT_DEF_H__
#define __COLIBRI_FOP_FOP_FORMAT_DEF_H__

/**
   @addtogroup fop

   @{
*/

/**
   @file fop_format_def.h

   Helper macros included before .ff files.
 */

#define DEF C2_FOP_FORMAT
#define _  C2_FOP_FIELD
#define _case C2_FOP_FIELD_TAG

#define U32 C2_FOP_TYPE_FORMAT_U32
#define U64 C2_FOP_TYPE_FORMAT_U64
#define BYTE C2_FOP_TYPE_FORMAT_BYTE
#define VOID C2_FOP_TYPE_FORMAT_VOID

#define RECORD FFA_RECORD
#define UNION FFA_UNION
#define SEQUENCE FFA_SEQUENCE
#define TYPEDEF FFA_TYPEDEF

/** @} end of fop group */

/* __COLIBRI_FOP_FOP_FORMAT_DEF_H__ */
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
