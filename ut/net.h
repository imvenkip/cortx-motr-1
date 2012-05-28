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
 * Original author: Dmitriy Chumak <dmitriy_chumak@xyratex.com>
 * Original creation date: 12/22/2011
 */

#ifndef __COLIBRI_UT_NET_H__
#define __COLIBRI_UT_NET_H__

#ifndef __KERNEL__
#include <errno.h>      /* ENOENT */
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>  /* inet_aton */
#include <netdb.h>      /* gethostbyname_r */
#include <stdio.h>      /* fprintf */
#include <string.h>     /* strlen */

/**
   Resolve hostname into a dotted quad.  The result is stored in buf.
 */
int canon_host(const char *hostname, char *buf, size_t bufsiz);

#endif /* __KERNEL__ */

#endif /* __COLIBRI_UT_NET_H__ */

