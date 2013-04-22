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
 * Original creation date: 22-Apr-2013
 */

#include "conf2/stage.h"

static char empty_str[] = "";

/** File (blob) descriptor. */
struct m0_stage_fd {
	/** Examples: "relative/path", "/absolute/path/to/blob". */
	char              *sf_path;

	enum m0_stage_mode sf_mode;

	/** Linkage to m0_stage::s_fds. */
	struct m0_tlink    sf_link;

	/** XXX DOCUMENTME */
	uint64_t           sf_magic;
};

M0_INTERNAL int m0_stage_chdir(struct m0_stage *sa, const char *path)
{
	/*
	 * - Return -ENOENT if the path does not exist.
	 * - Update sa->s_pwd.
	 */
	XXX;
}

M0_INTERNAL struct m0_stage_fd *
m0_stage_open(struct m0_stage *sa, const char *path, enum m0_stage_mode mode)
{
	*sa->s_err = 0;
	/*
	 *
	 * - Check the path:
	 *   - does not exist ==> -ENOENT;
	 *   - leads to a tree ==> -ENOTDIR.
	 * - Allocate new m0_stage_fd.
	 * - Insert it into sa->s_fds.
	 * - Return the pointer.
	 */
	XXX;
}

M0_INTERNAL int m0_stage_close(struct m0_stage *sa, struct m0_stage_fd *fd)
{
	/*
	 * - Return -EBADF if fd is not in sa->s_fds.
	 * - Release fd, freeing allocated memory.
	 * - Remove fd from sa->s_fds. Return 0.
	 */
	XXX;
}

M0_INTERNAL int m0_stage_read(const struct m0_stage *sa,
			      const struct m0_stage_fd *fd,
			      const uint8_t **pbuf, size_t *count)
{
	/*
	 * - Return -EBADF if fd is not in sa->s_fids.
	 * - Return -EISDIR if fd->sf_path leads to a tree object.
	 * - Updates *pbuf and *count so that they refer to the
	 *   contents of the blob object.
	 */
	XXX;
}

M0_INTERNAL int m0_stage_write(const struct m0_stage *sa,
			       const struct m0_stage_fd *fd,
			       void *buf, size_t count)
{
	/*
	 * - Return -EBADF if fd is not in sa->s_fids or is not open
	 *   for writing.
	 *
	 * - Walk the fd->sf_path, building a stack of ancestors ---
	 *   a list of (tree, name) tuples, where `tree' is SHA-1 of a
	 *   tree object and `name' is filename of a sub-object.
	 *   - Return -ENOENT if a path component does not exist.
	 *   - Return -ENOTDIR if a component used as a tree in path is
	 *     not, in fact, a tree.
	 *
	 * - Return -EISDIR if path destination is a tree object.
	 *
	 * - Create new blob object from the memory region of size
	 *   `count' pointed at by `buf'.
	 *
	 * - Create new tree objects -- the ancestors of newly
	 *   created blob -- with SHA-1 identifiers in the
	 *   corresponding tree entries recalculated:
	 *   - for each (old_tree, name), popped from `ancestors':
	 *     - if `tree' is in sa->s_dirs ==> return -EBUSY;
	 *     - create new_tree, copying contents from old_tree;
	 *     - update SHA-1 of the `name' entry in new_tree.
	 *
	 * - Update sa->s_head.
	 */
	XXX;
}

M0_INTERNAL int m0_stage_mkdir(struct m0_stage *sa, const char *path)
{
	/*
	 * - Walk the path, excluding the last element, and build a
	 *   stack of ancestors; see m0_stage_write() above.
	 *
	 * - Return -EEXIST if given path already exists (not
	 *   necessarily leading to a tree object).
	 *
	 * - Create new tree object --- a subdirectory.
	 *
	 * - Create new tree objects, representing "updated" ancestors;
	 *   see m0_stage_write() above.
	 *
	 * - Update sa->s_head.
	 */
	XXX;
}

/** Directory, opened with m0_stage_opendir(). */
struct m0_stage_dir {
	/** SHA-1 of the corresponding tree object. */
	unsigned char  sd_tree[20];

	/** Contents of the tree object. */
	const uint8_t *sd_buf;

	/** Size of the contents. */
	size_t         sd_len;

	/** Reading position (offset from ->sd_buf). */
	size_t         sd_pos;

	/** Linkage to m0_stage::s_dirs. */
	struct m0_link sd_link;

	/** XXX DOCUMENTME */
	uint64_t       sd_magic;
};

M0_INTERNAL struct m0_stage_dir *
m0_stage_opendir(struct m0_stage *sa, const char *path)
{
	/*
	 * - Make sure the path is valid and leads to a tree object.
	 * - Allocate m0_stage_dir and set its fields. (Note, that
	 *   ->sd_buf points directly at a tree object, memory is not
	 *   copied.)
	 * - Insert newly created m0_stage_dir into sa->s_dirs.
	 */
	XXX;
}

M0_INTERNAL int
m0_stage_readdir(struct m0_stage_dir *dir, struct m0_stage_dirent *entry)
{
	/*
	 * if (dir->sd_pos < dir->sd_len) {
	 *         fill *entry;
	 *         advance dir->sd_pos;
	 *         return 1;
	 * }
	 * if (dir->sd_pos == dir->sd_len)
	 *         return 0;
	 * return -EBADF;
	 */
	XXX;
}

M0_INTERNAL int
m0_stage_closedir(struct m0_stage *sa, struct m0_stage_dir *dir)
{
	/*
	 * - Return -EBADF if dir is not in sa->s_dirs.
	 * - Remove dir from sa->s_dirs. Return 0.
	 */
	XXX;
}
