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
 * Original author: Dmitriy Chumak <dmitriy_chumak@xyratex.com>
 * Original creation date: 18-Jul-2012
 */

#pragma once

#ifndef __MERO_XCODE_XCODE_ATTR_H__
#define __MERO_XCODE_XCODE_ATTR_H__

/**
 * @addtogroup xcode
 * @{
 */

/**
 * Set xcode attribute on a struct or strucut's field. This sets a special gcc
 * __attribute__ which is ignored by gcc during compilation, but which is then
 * used by gccxml and gccxml2xcode to generate xcode data.
 *
 * Please, refer to gccxml2xcode documentation for more details.
 */
#define M0_XC_ATTR(name, val) __attribute__((gccxml("xc_" name, val)))

/**
 * Shortened versions of M0_XC_ATTR to specifiy m0_xcode_aggr types.
 */
#define M0_XCA_RECORD   M0_XC_ATTR("atype","M0_XA_RECORD")
#define M0_XCA_SEQUENCE M0_XC_ATTR("atype","M0_XA_SEQUENCE")
#define M0_XCA_ARRAY    M0_XC_ATTR("atype","M0_XA_ARRAY")
#define M0_XCA_UNION    M0_XC_ATTR("atype","M0_XA_UNION")

#define M0_XCA_ENUM     M0_XC_ATTR("enum","nonce")

/**
 * Shortened versions of M0_XC_ATTR for TAG and OPAQUE attributes.
 */
#define M0_XCA_OPAQUE(value)   M0_XC_ATTR("opaque",value)
#define M0_XCA_TAG(value)      M0_XC_ATTR("tag",value)

/** @} end of xcode group */

/* __MERO_XCODE_XCODE_ATTR_H__ */
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
