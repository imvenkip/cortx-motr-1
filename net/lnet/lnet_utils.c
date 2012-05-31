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
 * Original author: Manish Honap <Manish_Honap@xyratex.com>
 * Original creation date: 05/30/2012
 */

#include "lib/errno.h"
#include "lib/memory.h"
#include "net/lnet/lnet.h"

/**
   @addtogroup LNetUtils
   Helper subroutine to convert localhost address to LNI format

   If Colibri registered LNet NID is 192.168.172.130\@tcp then, This function
   converts address of type 127.0.0.1\@tcp:12345:34:1 to
   192.168.172.130\@tcp:12345:34:1 format.

   @param ndom[In] Initialized network domain
   @param ep_addr[In-Out] Converted End Point address
 */
int c2_lut_lhost_lnet_conv(struct c2_net_domain *ndom, char *ep_addr)
{
	int           rc = 0;
	char * const *ifaces;
	char         *xport;
	int           i;
	int           j;
	char          cl_addr[C2_NET_LNET_XEP_ADDR_LEN];
	char         *t_addr;
	char         *hold;

	C2_ALLOC_ARR(t_addr, C2_NET_LNET_XEP_ADDR_LEN);
	hold = t_addr;
	strncpy(t_addr, ep_addr, C2_NET_LNET_XEP_ADDR_LEN);
	if (strstr(ep_addr, "127.0.0.1") != NULL) {
		c2_net_lnet_ifaces_get(ndom, &ifaces);
		C2_ASSERT(ifaces != NULL);
		/* Make t_addr to point to string past @ */
		strsep(&t_addr, "@");
		/* Extract transport type in xport, t_addr points past : */
		xport = strsep(&t_addr, ":");
		if (xport == NULL) {
			rc = -EINVAL;
			goto free_addr;
		}
		for (i = 0, j = 0; ifaces[i] != NULL; ++i) {
			if (strstr(ifaces[i], xport) == NULL)
				continue;
			else {
				snprintf(cl_addr, C2_NET_LNET_XEP_ADDR_LEN,
					 "%s:%s", ifaces[i], t_addr);
				break;
			}
		}
		c2_net_lnet_ifaces_put(ndom, &ifaces);
		/* If no matching transport type was found */
		if (strlen(cl_addr) < strlen(ep_addr)) {
			rc = -EINVAL;
			goto free_addr;
		}
		memcpy(ep_addr, cl_addr, C2_NET_LNET_XEP_ADDR_LEN);
	}

free_addr:
	c2_free(hold);
	return rc;
}

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
