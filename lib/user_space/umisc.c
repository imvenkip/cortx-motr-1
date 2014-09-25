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
 * Original author: Anatoliy Bilenko <Anatoliy_Bilenko@xyratex.com>
 * Original creation date: 03/09/2012
 */

#include <stdlib.h>		/* strtoull, strtoul */
#include <netdb.h>  /* gethostbyname_r */
#include <arpa/inet.h> /* inet_ntoa, inet_ntop */
#include <unistd.h> /* gethostname */
#include <errno.h>
#include "lib/misc.h"

M0_INTERNAL uint64_t m0_strtou64(const char *str, char **endptr, int base)
{
	return strtoull(str, endptr, base);
}

M0_INTERNAL uint32_t m0_strtou32(const char *str, char **endptr, int base)
{
	return strtoul(str, endptr, base);
}

/** Resolve a hostname to a stringified IP address */
M0_INTERNAL int m0_host_resolve(const char *name, char *buf, size_t bufsiz)
{
	int            i;
	int            rc = 0;
	struct in_addr ipaddr;

	if (inet_aton(name, &ipaddr) == 0) {
		struct hostent  he;
		char            he_buf[4096];
		struct hostent *hp;
		int             herrno;

		rc = gethostbyname_r(name, &he, he_buf, sizeof he_buf,
				     &hp, &herrno);
		if (rc != 0 || hp == NULL)
			return -ENOENT;
		for (i = 0; hp->h_addr_list[i] != NULL; ++i)
			/* take 1st IPv4 address found */
			if (hp->h_addrtype == AF_INET &&
			    hp->h_length == sizeof(ipaddr))
				break;
		if (hp->h_addr_list[i] == NULL)
			return -EPFNOSUPPORT;
		if (inet_ntop(hp->h_addrtype, hp->h_addr, buf, bufsiz) == NULL)
			rc = -errno;
	} else if (strlen(name) >= bufsiz) {
		rc = -ENOSPC;
	} else {
		strcpy(buf, name);
	}
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
