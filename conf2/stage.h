/* -*- C -*- */
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
 * Original creation date: 18-Apr-2013
 */

#pragma once
#ifndef __MERO_CONF2_STAGE_H__
#define __MERO_CONF2_STAGE_H__

#include "conf2/pit.h"

/**
 * @defgroup XXX
 *
 * @{
 */

/* File descriptor (opaque pointer). */
struct m0_stage_fd;

/**
 * Staging area.
 *
 * Staging area is a context in which all staging operations are
 * performed.
 */
struct m0_stage {
	/**
	 * SHA-1 of the head of the DAG being staged.
	 *
	 * ->s_head corresponds to a tree object.
	 */
	unsigned char s_head[20];

	/**
	 * Current working directory.
	 *
	 * The concept of working directory is used in the resolution
	 * of relative paths.
	 *
	 * ->s_pwd is an absolute path from ->s_head to a tree object.
	 * ->s_pwd is never NULL, though it can be an empty string.
	 */
	char         *s_pwd;

	/** Open file descriptors. */
	struct m0_tl  s_fds;

	/** Open directories. */
	struct m0_tl  s_dirs;

	/** Error code. */
	int           s_err;

	/**
	 * SHA-1 of the commit, which was the HEAD of the DAG at the
	 * moment when this m0_stage was initialised.
	 *
	 * ->s_parent corresponds to a commit object.
	 */
	unsigned char s_parent[20];
};

/** Re-initialises staging area, forgetting uncommitted changes. */
M0_INTERNAL int m0_stage_reset(struct m0_stage *sa);

/**
 * Commits staged changes to the DAG of pit objects.
 *
 * Creates new commit object and makes it the HEAD of the DAG.
 */
M0_INTERNAL int m0_stage_commit(struct m0_stage *sa);

/**
 * Changes working directory of the staging area.
 *
 * @retval -ENOENT   Cannot follow the path.
 * @retval -ENOTDIR  Path destination is not a tree object.
 */
M0_INTERNAL int m0_stage_chdir(struct m0_stage *sa, const char *path);

enum m0_stage_mode {
	M0_SM_RDONLY,
	M0_SM_RDWR
};

/**
 * Opens a blob object.
 *
 * @returns an opaque file descriptor that can be passed to
 * m0_stage_read() or m0_stage_write().
 * @returns NULL in case of error, setting sa->s_err correspondingly.
 *
 * Opened object should be closed with m0_stage_close().
 *
 * WARNING: An attempt to m0_stage_open() a tree object results in
 *          -ENOTDIR error.
 *
 * @code
 * int f(struct m0_stage *sa, const char *path)
 * {
 *         struct m0_stage_fd *fd;
 *
 *         fd = m0_stage_open(sa, path, M0_SM_RDONLY);
 *         if (fd == NULL)
 *                 return sa->s_err;
 *         // Success.
 *         return m0_stage_close(sa, fd);
 * }
 * @endcode
 *
 * @post  equi(retval == NULL, sa->s_err != 0)
 */
M0_INTERNAL struct m0_stage_fd *
m0_stage_open(struct m0_stage *sa, const char *path, enum m0_stage_mode mode);

/**
 * Closes a blob object opened with m0_stage_open().
 *
 * @returns -EBADF if `fd' is not a valid file descriptor.
 */
M0_INTERNAL int m0_stage_close(struct m0_stage *sa, struct m0_stage_fd *fd);

/**
 * Update `pbuf' and `count' so that they refer to contents of the
 * blob referred to by `fd'.
 */
M0_INTERNAL int m0_stage_read(const struct m0_stage *sa,
			      const struct m0_stage_fd *fd,
			      const uint8_t **pbuf, size_t *count);

/** Creates new blob object from the data provided by caller. */
M0_INTERNAL int m0_stage_write(const struct m0_stage *sa,
			       const struct m0_stage_fd *fd, void *buf,
			       size_t count);

M0_INTERNAL int m0_stage_remove(struct m0_stage *sa, const char *path);

/* XXX TODO: m0_stage_symlink(), m0_stage_readlink(). */

M0_INTERNAL int m0_stage_mkdir(struct m0_stage *sa, const char *path);

/* Directory descriptor (opaque pointer). */
struct m0_stage_dir;

/** Opens directory, referred to by `path'. */
M0_INTERNAL struct m0_stage_dir *m0_stage_opendir(struct m0_stage *sa,
						  const char *path);

/** Type of directory entry. */
enum m0_stage_dirent_type {
	M0_DT_DIR, /**< directory (a tree object) */
	M0_DT_REG, /**< regular file (a blob object) */
	M0_DT_LNK  /**< symbolic link (a blob, containing path) */
};

/** Directory entry. */
struct m0_stage_dirent {
	enum m0_stage_dirent_type sde_type; /**< type of file */
	const char               *sde_name; /**< filename */
};

/**
 * Reads next directory entry.
 *
 * @retval 1      Next directory entry has been read into *entry.
 * @retval 0      End of the directory is reached (*entry is not unchanged).
 * @retval -Exxx  Error.
 */
M0_INTERNAL int m0_stage_readdir(struct m0_stage_dir *dir,
				 struct m0_stage_dirent *entry);

/* XXX FUTURE
 * Possible enhancements --- m0_stage_{rewind,tell,seek}dir(). */

/**
 * Closes directory, opened with m0_stage_opendir().
 *
 * @returns -EBADF if `dir' is not a valid directory descriptor.
 */
M0_INTERNAL int m0_stage_closedir(struct m0_stage *sa,
				  struct m0_stage_dir *dir);

/** @} end of XXX group */
#endif /* __MERO_CONF2_STAGE_H__ */
