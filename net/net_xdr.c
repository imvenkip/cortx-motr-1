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
 * Original author: Alexey Lyashkov <Alexey_Lyashkov@xyratex.com>,
 *                  Nikita Danilov <Nikita_Danilov@xyratex.com>
 * Original creation date: 04/11/2010
 */

#include <rpc/types.h>
#include <rpc/xdr.h>

#include "lib/cdefs.h"
#include "net/net.h"
#include "net/xdr.h"

/**
   @addtogroup netDep Networking (Deprecated Interfaces)

   @{
 */

bool c2_xdr_service_id (void *x, struct c2_service_id *objp)
{
	XDR *xdrs = x;

	return xdr_vector(xdrs, (char *)objp->si_uuid,
			  ARRAY_SIZE(objp->si_uuid),
			  sizeof (char), (xdrproc_t) xdr_char);
}

/** @} end of net deprecated group */
