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
 * Original author: Madhavrao Vemuri <madhav_vemuri@xyratex.com>
 * Original creation date: 12/10/2011
 */

#include "lib/chan.h"
#include "lib/tlist.h"
#include "lib/memory.h"
#include "lib/thread.h"
#include "lib/misc.h" /* C2_SET0 */
#include <unistd.h>
#include <stdio.h>
enum {
	BUF_SIZE = 64
};
struct c2_chan buf_chan;
struct c2_mutex list_lock;
static int list_size;
struct c2_io_buf {
	struct c2_tlink ib_link;
	uint64_t        ib_magic;
	void	       *ib_addr;
};
struct io_buf_vec {
	int    count ;
	void **addr;
};

enum {
	/* Hex ASCII value of "iob_link" */
	IO_BUF_LINK_MAGIC = 0x696f625f6c696e6b,
	/* Hex ASCII value of "iob_head" */
	IO_BUF_HEAD_MAGIC = 0x696f625f68656164,
};

static const struct c2_tl_descr io_buf_descr =
		    C2_TL_DESCR("io_buf_descr",
		    struct c2_io_buf, ib_link, ib_magic,
		    IO_BUF_LINK_MAGIC, IO_BUF_HEAD_MAGIC);

static struct c2_tl buf_head;

int c2_buf_prealloc(int nr_buf,int block_size)
{
	struct c2_io_buf *io_buf = NULL;
	int i;
	int buf_size;
	buf_size = (block_size < BUF_SIZE) ? BUF_SIZE : block_size;
	list_size = nr_buf;
	c2_tlist_init(&io_buf_descr, &buf_head);
	c2_chan_init(&buf_chan);
	c2_mutex_init(&list_lock);
	for (i = 0; i < nr_buf; i++) {
		C2_ALLOC_PTR(io_buf);
		c2_tlink_init(&io_buf_descr, io_buf);
		C2_ASSERT(!c2_tlink_is_in(&io_buf_descr, io_buf));
		io_buf->ib_addr = (void *) c2_alloc_aligned(buf_size,0);
		c2_tlist_add_tail(&io_buf_descr, &buf_head, io_buf);
	}
	return 0;
}

int c2_buf_get(int nr_buf,struct io_buf_vec * io_buf_vec){
	int i,len;
	struct c2_io_buf *io_buf = NULL;
	struct c2_clink buf_link;
	c2_clink_init(&buf_link, NULL);
	c2_clink_add(&buf_chan, &buf_link);
	c2_mutex_lock(&list_lock);
	len = list_size;
	c2_mutex_unlock(&list_lock);
	if(nr_buf >= len) {
	printf("WAIT :list_size %d nr_buf %d\n",list_size,nr_buf);
		c2_chan_wait(&buf_link);
	}
	c2_mutex_lock(&list_lock);
	list_size = list_size - nr_buf;
	c2_mutex_unlock(&list_lock);
	printf("GET :list_size %d nr_buf %d\n",list_size,nr_buf);
	io_buf_vec->count = nr_buf;
	C2_ALLOC_ARR(io_buf_vec->addr, nr_buf);
	for(i = 0; i < nr_buf; i++) {
		c2_mutex_lock(&list_lock);
		io_buf = c2_tlist_head(&io_buf_descr, &buf_head);
		c2_tlist_del(&io_buf_descr, io_buf);
		c2_mutex_unlock(&list_lock);
		c2_tlink_fini(&io_buf_descr, io_buf);
		io_buf_vec->addr[i] = io_buf->ib_addr;
		c2_free(io_buf);
	}
	c2_clink_del(&buf_link);
	c2_clink_fini(&buf_link);
	return 0;
}

int c2_buf_put(struct io_buf_vec *io_buf_vec)
{
	int i;
	struct c2_io_buf *io_buf = NULL;
	for (i = 0; i < io_buf_vec->count; i++){
		C2_ALLOC_PTR(io_buf);
		io_buf->ib_addr = io_buf_vec->addr[i];
		c2_tlink_init(&io_buf_descr, io_buf);
		C2_ASSERT(!c2_tlink_is_in(&io_buf_descr, io_buf));
		c2_mutex_lock(&list_lock);
		c2_tlist_add_tail(&io_buf_descr, &buf_head, io_buf);
		c2_mutex_unlock(&list_lock);
	}
	if (list_size > 0)
		c2_chan_signal(&buf_chan);
	c2_free(io_buf_vec->addr);
	c2_mutex_lock(&list_lock);
	list_size = list_size + io_buf_vec->count;
	c2_mutex_unlock(&list_lock);
	printf("PUT :list_size %d nr_buf %d\n",list_size,io_buf_vec->count);
	return 0;
}


int c2_buf_finalize(int nr_buf)
{
	struct c2_io_buf *io_buf = NULL;
	c2_chan_fini(&buf_chan);
	c2_tlist_for(&io_buf_descr, &buf_head, io_buf) {
		c2_free(io_buf->ib_addr);
		c2_tlist_del(&io_buf_descr, io_buf);
		c2_tlink_fini(&io_buf_descr, io_buf);
		c2_free(io_buf);
	}
	c2_tlist_endfor;
	c2_tlist_fini(&io_buf_descr, &buf_head);
	return 0;
}

void buffers_get_put(int rc);
int main()
{

	int rc;
	struct c2_thread        *client_thread;
	int			 nr_client_threads = 20;
	int i;
	rc = c2_buf_prealloc(1000,128);
	C2_ASSERT(rc == 0);
	C2_ALLOC_ARR(client_thread, nr_client_threads);
	for (i = 0; i < nr_client_threads; i++) {
		C2_SET0(&client_thread[i]);
		rc = C2_THREAD_INIT(&client_thread[i], int,
				     NULL, &buffers_get_put,
					0, "client_%d", i);
		C2_ASSERT(rc == 0);
	}
	for (i = 0; i < nr_client_threads; i++) {
		c2_thread_join(&client_thread[i]);
	}
	rc = c2_buf_finalize(1000);
	C2_ASSERT(rc == 0);
	return rc;

};

void buffers_get_put(int rc)
{
	struct io_buf_vec io_buf;
	rc = c2_buf_get(100,&io_buf);
	sleep(1);
	C2_ASSERT(rc == 0);
	rc = c2_buf_put(&io_buf);
	C2_ASSERT(rc == 0);
}
