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
 * Original creation date: 01/29/2012
 */

#ifndef __COLIBRI_UT_IOREDIRECT_H__
#define __COLIBRI_UT_IOREDIRECT_H__

#include <stdio.h>  /* FILE, fpos_t */

/**
 * Associates one of the standard streams (stdin, stdout, stderr) with a file
 * pointed by 'path' argument.
 */
void redirect_std_stream(FILE *std_stream, const char *path, int *fd,
			 fpos_t *pos, FILE **new_stream);

/**
 * Restores standard stream from file descriptor and stream position, which were
 * saved earlier by redirect_std_stream().
 */
void restore_std_stream(FILE *std_stream, int std_fd, int fd, fpos_t *pos);

#endif /* __COLIBRI_UT_IOREDIRECT_H__ */

