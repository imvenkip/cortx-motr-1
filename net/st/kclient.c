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
 * Original author: Huang Hua <hua_huang@xyratex.com>
 * Original creation date: 05/26/2010
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <netdb.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/mount.h>
#include <arpa/inet.h>
#include <rpc/types.h>
#include <rpc/xdr.h>
#include <rpc/auth.h>
#include <rpc/rpc.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "lib/errno.h"
#include "net/ksunrpc/ksunrpc.h"

#define UT_PROC_NAME "ksunrpc-ut"

static int fill_ipv4_sockaddr(const char *hostname, struct sockaddr_in *addr)
{
	struct hostent *hp;
	addr->sin_family = AF_INET;

	if (inet_aton(hostname, &addr->sin_addr))
		return 0;
	if ((hp = gethostbyname(hostname)) == NULL) {
		fprintf(stderr, "can't get address for %s\n",
				hostname);
		return -1;
	}
	if (hp->h_length > sizeof(struct in_addr)) {
		fprintf(stderr, "got bad hp->h_length\n");
		hp->h_length = sizeof(struct in_addr);
	}
	memcpy(&addr->sin_addr, hp->h_addr, hp->h_length);
	return 0;
}

int main(int argc, char **argv)
{
	struct ksunrpc_service_id id;
	char *hostname;
	int port;
	struct sockaddr_in sa;
	int ret;
	int i;
	int fd;

	if (argc != 3) {
		fprintf(stderr, "%s servername port\n", argv[0]);
		return -1;
	}

	hostname = argv[1];
	port = atoi(argv[2]);

	strcpy(id.ssi_host, hostname);
	ret = fill_ipv4_sockaddr(id.ssi_host, &sa);
	if (ret < 0) {
		fprintf(stderr, "failed to resolve %s\n", hostname);
		return -1;
	}

	id.ssi_sockaddr = sa;
	id.ssi_sockaddr.sin_port = htons(port);
	id.ssi_sockaddr.sin_family = AF_INET;
	id.ssi_addrlen  = strlen(hostname) + 1; /* this is used as the length of hostname */
	id.ssi_port = htons(port);

	for (i = 0; i < id.ssi_addrlen; i ++)
		printf("%c", hostname[i]);
	printf("\nlen=%d\n", id.ssi_addrlen);

	printf("addr = %x\n", sa.sin_addr.s_addr);
	printf("port = %x\n", sa.sin_port);

	fd = open("/proc/" UT_PROC_NAME, O_WRONLY);
	if (fd < 0) {
		fprintf(stderr, "failed to open %s:%d\n", "/proc/" UT_PROC_NAME, errno);
		return -1;
	}
	ret = write(fd, &id, sizeof id);
	close(fd);

	printf("kernel sunrpc ut: ret = %d\n'dmesg' to see more.\n", ret);
	return 0;
}
