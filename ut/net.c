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
 * Original author: Carl Braganza <Carl_Braganza@us.xyratex.com>,
 *                  Dave Cohrs <Dave_Cohrs@us.xyratex.com>
 * Original creation date: 04/12/2011
 */

#include "ut/net.h"
#include "net/lnet/lnet.h"

#ifndef __KERNEL__
int canon_host(const char *hostname, char *buf, size_t bufsiz)
{
	int                i;
	int		   rc = 0;
	struct in_addr     ipaddr;

	/* c2_net_end_point_create requires string IPv4 address, not name */
	if (inet_aton(hostname, &ipaddr) == 0) {
		struct hostent he;
		char he_buf[4096];
		struct hostent *hp;
		int herrno;

		rc = gethostbyname_r(hostname, &he, he_buf, sizeof he_buf,
				     &hp, &herrno);
		if (rc != 0) {
			fprintf(stderr, "Can't get address for %s\n",
				hostname);
			return -ENOENT;
		}
		for (i = 0; hp->h_addr_list[i] != NULL; ++i)
			/* take 1st IPv4 address found */
			if (hp->h_addrtype == AF_INET &&
			    hp->h_length == sizeof(ipaddr))
				break;
		if (hp->h_addr_list[i] == NULL) {
			fprintf(stderr, "No IPv4 address for %s\n",
				hostname);
			return -EPFNOSUPPORT;
		}
		if (inet_ntop(hp->h_addrtype, hp->h_addr, buf, bufsiz) ==
		    NULL) {
			fprintf(stderr, "Cannot parse network address for %s\n",
				hostname);
			rc = -errno;
		}
	} else {
		if (strlen(hostname) >= bufsiz) {
			fprintf(stderr, "Buffer size too small for %s\n",
				hostname);
			return -ENOSPC;
		}
		strcpy(buf, hostname);
	}
	return rc;
}
#endif /* __KERNEL__ */

static void get_xport(const char *ep_addr, char *xport)
{
	int   i = 0;
	char  addr[C2_NET_LNET_XEP_ADDR_LEN];
	char *ptr = addr;

	strncpy(addr, ep_addr, C2_NET_LNET_XEP_ADDR_LEN);
	while (*ptr != '@') ptr++;
	ptr++;
	while (*ptr != ':')
		xport[i++] = *ptr++;
	xport[i] = '\0';
}

void c2_lut_lhost_lnet_conv(struct c2_net_domain *ndom, char *ep_addr)
{
	char * const *ifaces;
	char          xport[10] = {0};
	char         *rem_netw;
	int           i;
	int           j;
	char          cl_addr[C2_NET_LNET_XEP_ADDR_LEN];

	if (strstr(ep_addr, "127.0.0.1") != NULL) {
		c2_net_lnet_ifaces_get(ndom, &ifaces);
		C2_ASSERT(ifaces != NULL);
		get_xport(ep_addr, xport);
		rem_netw = strchr(ep_addr, ':');
		for (i = 0, j = 0; ifaces[i] != NULL; ++i) {
			if (strstr(ifaces[i], xport) == NULL)
				continue;
			else {
				snprintf(cl_addr, C2_NET_LNET_XEP_ADDR_LEN,
					 "%s%s", ifaces[i], rem_netw);
				break;
			}
		}
		c2_net_lnet_ifaces_put(ndom, &ifaces);
		memcpy(ep_addr, cl_addr, C2_NET_LNET_XEP_ADDR_LEN);
	}
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
