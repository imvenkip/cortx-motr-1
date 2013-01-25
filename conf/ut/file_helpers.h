/* -*- c -*- */
/*
 * COPYRIGHT 2013 XYRATEX TECHNOLOGY LIMITED
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
 * Original author: Valery V. Vorotyntsev <valery_vorotyntsev@xyratex.com>
 * Original creation date: 28-Dec-2012
 */
#pragma once
#ifndef __MERO_CONF_UT_FILE_HELPERS_H__
#define __MERO_CONF_UT_FILE_HELPERS_H__

#include <stddef.h>  /* size_t */

#define _QUOTE(s) #s
#define QUOTE(s) _QUOTE(s)

/**
 * Returns absolute path to given file in conf/ut directory.
 * M0_CONF_UT_DIR is defined in conf/ut/Makefile.am.
 */
#define M0_CONF_UT_PATH(name) QUOTE(M0_CONF_UT_DIR) "/" name

/**
 * Reads contents of file into a buffer.
 *
 * @param path  Name of file to read.
 * @param dest  Buffer to read into.
 * @param sz    Size of `dest'.
 */
M0_INTERNAL int m0_ut_file_read(const char *path, char *dest, size_t sz);

#endif /* __MERO_CONF_UT_FILE_HELPERS_H__ */
