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
 * Original author: Carl Braganza <carl_braganza@xyratex.com>
 * Original creation: 10/08/2012
 */

/* This file is designed to be included by addb/addb.c */

#include "lib/types.h"

#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

/**
   @ingroup addb_pvt
   @{
 */

/** path to read kmod uuid parameter */
static const char *kmod_uuid_file = "/sys/module/m0mero/parameters/node_uuid";

/** default node uuid which can be used instead of a "real" one, which is
 *  obtained from kernel module; this can be handy for some utility applications
 *  which don't need full functionality of libmero.so, so they can trick ADDB by
 *  providing some fake uuid */
static char default_node_uuid[M0_UUID_STRLEN + 1] =
		"00000000-0000-0000-0000-000000000000"; /* nil UUID */

/** flag, which specify whether to use a "real" node uuid or a default one */
static bool use_default_node_uuid = false;

void m0_addb_kmod_uuid_file_set(const char *path)
{
	kmod_uuid_file = path;
}

void m0_addb_node_uuid_string_set(char *uuid)
{
	use_default_node_uuid = true;
	if (uuid != NULL) {
		strncpy(default_node_uuid, uuid, M0_UUID_STRLEN);
		default_node_uuid[M0_UUID_STRLEN] = '\0';
	}
}

/**
   Construct the node UUID in user space by reading our kernel module's
   node_uuid parameter.
 */
static int addb_node_uuid_string_get(char buf[M0_UUID_STRLEN + 1])
{
	int fd;
	int rc = 0;

	if (use_default_node_uuid) {
		strncpy(buf, default_node_uuid, M0_UUID_STRLEN);
		buf[M0_UUID_STRLEN] = '\0';
	} else {
		fd = open(kmod_uuid_file, O_RDONLY);
		if (fd < 0)
			return -EINVAL;
		if (read(fd, buf, M0_UUID_STRLEN) == M0_UUID_STRLEN) {
			rc = 0;
			buf[M0_UUID_STRLEN] = '\0';
		} else
			rc = -EINVAL;
		close(fd);
	}

	return rc;
}

enum { ADDB_PROC_CTX_MASK = 0xffffffUL };

/**
  Create the process container context.
 */
static void addb_ctx_proc_ctx_create(void)
{
	pid_t pid = getpid();

	addb_proc_ctx_fields[0] = (uint64_t)addb_init_time;
	addb_proc_ctx_fields[1] = (uint64_t)pid;
	m0_addb_proc_ctx.ac_type   = &m0_addb_ct_process;
	m0_addb_proc_ctx.ac_id     = (addb_init_time & ~ADDB_PROC_CTX_MASK) |
		(pid & ADDB_PROC_CTX_MASK);
	m0_addb_proc_ctx.ac_parent = &m0_addb_node_ctx;
	++m0_addb_node_ctx.ac_cntr;
	m0_addb_proc_ctx.ac_depth  = m0_addb_node_ctx.ac_depth + 1;
	m0_addb_proc_ctx.ac_magic  = M0_ADDB_CTX_MAGIC;
}

/** @} end group addb_pvt */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
