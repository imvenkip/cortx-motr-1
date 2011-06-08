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
 * Original author: Subhash Arya
 * Original creation date: 06/02/2011
 */

/*
 * xdr_rec.c, Implements TCP/IP based XDR streams with a "record marking"
 * layer above tcp (for rpc's use).
 *
 * Copyright (c) 2010, Oracle America, Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 *       copyright notice, this list of conditions and the following
 *       disclaimer in the documentation and/or other materials
 *       provided with the distribution.
 *     * Neither the name of the "Oracle America, Inc." nor the names of its
 *       contributors may be used to endorse or promote products derived
 *       from this software without specific prior written permission.
 *
 *   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 *   "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 *   LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 *   FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 *   COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 *   INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 *   DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
 *   GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 *   INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 *   WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 *   NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 *   OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * These routines interface XDRSTREAMS to a tcp/ip connection.
 * There is a record marking layer between the xdr stream
 * and the tcp transport level.  A record is composed on one or more
 * record fragments.  A record fragment is a thirty-two bit header followed
 * by n bytes of data, where n is contained in the header.  The header
 * is represented as a htonl(u_long).  The high order bit encodes
 * whether or not the fragment is the last fragment of the record
 * (1 => fragment is last, 0 => more fragments to follow.
 * The other 31 bits encode the byte length of the fragment.
 *
 * For colibri, the htonl() macros in the xdr various operation vectors
 * (xdrrec_ops) have been replaced with c2_le() for little-endianness
 * of the data over the wire.
*/
#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <rpc/rpc.h>
#include <libintl.h>
#include "xdr/xdr_rec.h"

#ifdef USE_IN_LIBIO
# include <wchar.h>
# include <libio/iolibio.h>
#endif

#define htonl(x) c2_le(x)
#define ntohl(x) c2_le(x)

#define MIN_MSG_SIZE 100
#define DEFAULT_MSG_SIZE 4000

/* Internal functions */

static u_int fix_buf_size(u_int);
static bool_t skip_input_bytes(struct c2_xdr_rec_strm *, long);
static bool_t flush_out(struct c2_xdr_rec_strm *, bool_t);
static bool_t set_input_fragment(struct c2_xdr_rec_strm *);
static bool_t get_input_bytes(struct c2_xdr_rec_strm *, caddr_t, int);

const struct xdr_ops c2_xdrrec_ops = {
        xdrrec_getlong,
        xdrrec_putlong,
        xdrrec_getbytes,
        xdrrec_putbytes,
        xdrrec_getpos,
        xdrrec_setpos,
        xdrrec_inline,
        xdrrec_destroy,
        xdrrec_getint32,
        xdrrec_putint32
};

/**
   The routine xdrrec_create() provides an XDR stream interface that allows
   for a bidirectional, arbitrarily long sequence of records. The contents of
   the records are meant to be data in XDR form. The stream's primary use is
   for interfacing RPC to TCP connections.

   @param sendsize      - The size of send buffer.

   @param recvsize      - The size of recv buffer.

   @param xdrs          - The XDR stream handle.

   @param tcp_handle    - The opaque transport handle.

   @param readit        - Procedure for reading from an XDR record stream.

   @param writeit       - Procedure for writing into an XDR record stream.
 */
void xdrrec_create(XDR *xdrs, u_int sendsize,
                   u_int recvsize, caddr_t tcp_handle,
                   int (*readit)(char *, char *, int),
                   int (*writeit)(char *, char *, int))
{
          struct c2_xdr_rec_strm *rstrm = (struct c2_xdr_rec_strm *) mem_alloc
                                          (sizeof(struct c2_xdr_rec_strm));
          caddr_t tmp;
          char *buf;

          sendsize = fix_buf_size(sendsize);
          recvsize = fix_buf_size(recvsize);
          buf = mem_alloc(sendsize + recvsize + BYTES_PER_XDR_UNIT);

          if (rstrm == NULL || buf == NULL) {
                mem_free(rstrm, sizeof(struct c2_xdr_rec_strm));
                mem_free(buf, sendsize + recvsize + BYTES_PER_XDR_UNIT);
                /*
                 *  This is bad.  Should rework xdrrec_create to
                 *  return a handle, and in this case return NULL
                 */
                 return;
        }
        /*
         * Adjust sizes and allocate buffer quad byte aligned
         */
        rstrm->sendsize = sendsize;
        rstrm->recvsize = recvsize;
        rstrm->the_buffer = buf;
        tmp = rstrm->the_buffer;
        if((size_t)tmp % BYTES_PER_XDR_UNIT)
                tmp += BYTES_PER_XDR_UNIT - (size_t)tmp % BYTES_PER_XDR_UNIT;
        rstrm->out_base = tmp;
        rstrm->in_base = tmp + sendsize;
        xdrs->x_ops = (struct xdr_ops *)&c2_xdrrec_ops;
        xdrs->x_private = (caddr_t)rstrm;
        rstrm->tcp_handle = tcp_handle;
        rstrm->readit = readit;
        rstrm->writeit = writeit;
        rstrm->out_finger = rstrm->out_boundry = rstrm->out_base;
        rstrm->frag_header = (u_int32_t *)rstrm->out_base;
        rstrm->out_finger += 4;
        rstrm->out_boundry += sendsize;
        rstrm->frag_sent = FALSE;
        rstrm->in_size = recvsize;
        rstrm->in_boundry = rstrm->in_base;
        rstrm->in_finger = (rstrm->in_boundry += recvsize);
        rstrm->fbtbc = 0;
        rstrm->last_frag = TRUE;
}

/*
 * The routines defined below are the xdr ops which will go into the
 * xdr handle filled in by xdrrec_create. Refer rpc/xdr.h for details.
 */
bool_t xdrrec_getlong(XDR *xdrs, long *lp)
{
        struct c2_xdr_rec_strm *rstrm = (struct c2_xdr_rec_strm *)
					 xdrs->x_private;
        int32_t *buflp = (int32_t *)rstrm->in_finger;
        int32_t mylong;

         /* first try the inline, fast case */
         if (rstrm->fbtbc >= BYTES_PER_XDR_UNIT &&
              rstrm->in_boundry - (char *) buflp >= BYTES_PER_XDR_UNIT) {
                *lp = (int32_t)ntohl(*buflp);
                rstrm->fbtbc -= BYTES_PER_XDR_UNIT;
                rstrm->in_finger += BYTES_PER_XDR_UNIT;
          } else {
                if (!xdrrec_getbytes(xdrs, (caddr_t) & mylong,
                       BYTES_PER_XDR_UNIT))
                        return FALSE;
                *lp = (int32_t)ntohl(mylong);
          }
          return TRUE;
}

bool_t xdrrec_putlong(XDR *xdrs, const long *lp)
{
        struct c2_xdr_rec_strm *rstrm = (struct c2_xdr_rec_strm *)
					 xdrs->x_private;
        int32_t *dest_lp = (int32_t *)rstrm->out_finger;

        if ((rstrm->out_finger += BYTES_PER_XDR_UNIT) > rstrm->out_boundry) {
               /*
                * this case should almost never happen so the code is
                * inefficient
                */
                rstrm->out_finger -= BYTES_PER_XDR_UNIT;
                rstrm->frag_sent = TRUE;
                if (!flush_out(rstrm, FALSE))
                        return FALSE;
                dest_lp = (int32_t *)rstrm->out_finger;
                rstrm->out_finger += BYTES_PER_XDR_UNIT;
        }
        *dest_lp = htonl(*lp);
        return TRUE;
}

bool_t xdrrec_getbytes(XDR *xdrs, caddr_t addr, u_int len)
{
        struct c2_xdr_rec_strm *rstrm = (struct c2_xdr_rec_strm *)
					 xdrs->x_private;
        u_int current;

        while (len > 0) {
                current = rstrm->fbtbc;
                if(current == 0) {
                        if (rstrm->last_frag)
                                return FALSE;
                        if (!set_input_fragment(rstrm))
                                return FALSE;
                        continue;
                }
                current = (len < current) ? len : current;
                if (!get_input_bytes(rstrm, addr, current))
                        return FALSE;
                addr += current;
                rstrm->fbtbc -= current;
                len -= current;
        }
        return TRUE;
}

bool_t xdrrec_putbytes(XDR *xdrs, const char *addr, u_int len)
{
        struct c2_xdr_rec_strm *rstrm = (struct c2_xdr_rec_strm *)
					 xdrs->x_private;
        u_int current;

        while (len > 0) {
                current = rstrm->out_boundry - rstrm->out_finger;
                current = (len < current) ? len : current;
                memcpy(rstrm->out_finger, addr, current);
                rstrm->out_finger += current;
                addr += current;
                len -= current;
                if (rstrm->out_finger == rstrm->out_boundry && len > 0) {
                        rstrm->frag_sent = TRUE;
                        if (!flush_out(rstrm, FALSE))
                                return FALSE;
                }
        }
        return TRUE;
}

u_int xdrrec_getpos(const XDR *xdrs)
{
        struct c2_xdr_rec_strm *rstrm = (struct c2_xdr_rec_strm *)
					 xdrs->x_private;
        long pos;

        pos = lseek((int)(long) rstrm->tcp_handle, (long) 0, 1);
        if (pos != -1)
                switch (xdrs->x_op) {
                case XDR_ENCODE:
                        pos += rstrm->out_finger - rstrm->out_base;
                        break;
                case XDR_DECODE:
                        pos -= rstrm->in_boundry - rstrm->in_finger;
                        break;
                default:
                        pos = (u_int) - 1;
                        break;
        }
        return (u_int)pos;
}

bool_t xdrrec_setpos(XDR *xdrs, u_int pos)
{
        struct c2_xdr_rec_strm *rstrm = (struct c2_xdr_rec_strm *)
					 xdrs->x_private;
        u_int currpos = xdrrec_getpos(xdrs);
        int delta = currpos - pos;
        caddr_t newpos;

        if ((int)currpos != -1)
                switch (xdrs->x_op) {
                case XDR_ENCODE:
                        newpos = rstrm->out_finger - delta;
                         if (newpos > (caddr_t)rstrm->frag_header &&
                             newpos < rstrm->out_boundry) {
                                rstrm->out_finger = newpos;
                                return TRUE;
                        }
                        break;
                 case XDR_DECODE:
                        newpos = rstrm->in_finger - delta;
                        if ((delta < (int) (rstrm->fbtbc)) &&
                        (newpos <= rstrm->in_boundry) &&
                        (newpos >= rstrm->in_base)) {
                                rstrm->in_finger = newpos;
                                rstrm->fbtbc -= delta;
                                return TRUE;
                        }
                        break;
                default:
                        break;
                }
        return FALSE;
}

int32_t* xdrrec_inline(XDR *xdrs, u_int len)
{
        struct c2_xdr_rec_strm *rstrm = (struct c2_xdr_rec_strm *)
					 xdrs->x_private;
        int32_t *buf = NULL;

        switch (xdrs->x_op) {

        case XDR_ENCODE:
                if ((rstrm->out_finger + len) <= rstrm->out_boundry) {
                        buf = (int32_t *) rstrm->out_finger;
                        rstrm->out_finger += len;
                }
                break;
        case XDR_DECODE:
                if ((len <= rstrm->fbtbc) &&
                   ((rstrm->in_finger + len) <= rstrm->in_boundry)) {
                        buf = (int32_t *) rstrm->in_finger;
                        rstrm->fbtbc -= len;
                        rstrm->in_finger += len;
                }
                break;
        default:
                break;
        }
        return buf;
}

void xdrrec_destroy(XDR *xdrs)
{
        struct c2_xdr_rec_strm *rstrm = (struct c2_xdr_rec_strm *)
					 xdrs->x_private;

        mem_free (rstrm->the_buffer, rstrm->sendsize + rstrm->recvsize
                  + BYTES_PER_XDR_UNIT);
        mem_free ((caddr_t) rstrm, sizeof(struct c2_xdr_rec_strm));
}

bool_t xdrrec_getint32(XDR *xdrs, int32_t *ip)
{
        struct c2_xdr_rec_strm *rstrm = (struct c2_xdr_rec_strm *)
					 xdrs->x_private;
        int32_t *bufip =(int32_t *)rstrm->in_finger;
        int32_t mylong;

        /* first try the inline, fast case */
        if (rstrm->fbtbc >= BYTES_PER_XDR_UNIT &&
        rstrm->in_boundry - (char *) bufip >= BYTES_PER_XDR_UNIT) {
                *ip = ntohl(*bufip);
                rstrm->fbtbc -= BYTES_PER_XDR_UNIT;
                rstrm->in_finger += BYTES_PER_XDR_UNIT;
        } else {
                if (!xdrrec_getbytes(xdrs, (caddr_t) &mylong,
                BYTES_PER_XDR_UNIT))
                        return FALSE;
                *ip = ntohl(mylong);
        }
        return TRUE;
}

bool_t xdrrec_putint32(XDR *xdrs, const int32_t *ip)
{
        struct c2_xdr_rec_strm *rstrm = (struct c2_xdr_rec_strm *)
					 xdrs->x_private;
        int32_t *dest_ip = (int32_t *)rstrm->out_finger;

        if ((rstrm->out_finger += BYTES_PER_XDR_UNIT) > rstrm->out_boundry) {
               /*
                * This case should almost never happen so the code is
                * inefficient
                */
                rstrm->out_finger -= BYTES_PER_XDR_UNIT;
                rstrm->frag_sent = TRUE;
                if (!flush_out(rstrm, FALSE))
                        return FALSE;
                dest_ip = (int32_t *)rstrm->out_finger;
                rstrm->out_finger += BYTES_PER_XDR_UNIT;
        }
        *dest_ip = htonl(*ip);
        return TRUE;
}

/* Exported routines to manage xdr records */

/**
   Before reading (deserializing from the stream, one should always call
   this procedure to guarantee proper record alignment.
 */
bool_t xdrrec_skiprecord(XDR *xdrs)
{
        struct c2_xdr_rec_strm *rstrm = (struct c2_xdr_rec_strm *)
					 xdrs->x_private;

        while (rstrm->fbtbc > 0 || (!rstrm->last_frag)) {
                if (!skip_input_bytes(rstrm, rstrm->fbtbc))
                        return FALSE;
                rstrm->fbtbc = 0;
                if ((!rstrm->last_frag) && (!set_input_fragment(rstrm)))
                        return FALSE;
        }
        rstrm->last_frag = FALSE;
        return TRUE;
}

/**
   Lookahead function.Returns TRUE if there is no more input in the buffer
   after consuming the rest of the current record.
 */
bool_t xdrrec_eof(XDR *xdrs)
{
        struct c2_xdr_rec_strm *rstrm = (struct c2_xdr_rec_strm *)
					 xdrs->x_private;

        while (rstrm->fbtbc > 0 || (!rstrm->last_frag)) {
                if (!skip_input_bytes(rstrm, rstrm->fbtbc))
                        return TRUE;
                rstrm->fbtbc = 0;
                if ((!rstrm->last_frag) && (!set_input_fragment(rstrm)))
                        return TRUE;
        }
        if (rstrm->in_finger == rstrm->in_boundry)
                return TRUE;
        return FALSE;
}

/**
   The client must tell the package when an end-of-record has occurred.
   The second parameter tells whether the record should be flushed to the
   (output) tcp stream.  (This lets the package support batched or
   pipelined procedure calls.)  TRUE => immediate flush to tcp connection.
 */
bool_t xdrrec_endofrecord(XDR *xdrs, bool_t sendnow)
{
        struct c2_xdr_rec_strm *rstrm = (struct c2_xdr_rec_strm *)
					 xdrs->x_private;
        u_long len;

        if (sendnow || rstrm->frag_sent
        || rstrm->out_finger + BYTES_PER_XDR_UNIT >= rstrm->out_boundry) {
                rstrm->frag_sent = FALSE;
                 return flush_out(rstrm, TRUE);
        }
        len = (rstrm->out_finger - (char *)rstrm->frag_header
        - BYTES_PER_XDR_UNIT);
        *rstrm->frag_header = htonl((u_long)len | LAST_FRAG);
        rstrm->frag_header = (u_int32_t *)rstrm->out_finger;
        rstrm->out_finger += BYTES_PER_XDR_UNIT;
        return TRUE;
}


/**
   Internal useful routines
 */
static bool_t flush_out(struct c2_xdr_rec_strm *rstrm, bool_t eor)
{
        u_long eormask = (eor == TRUE) ? LAST_FRAG : 0;
        u_long len = (rstrm->out_finger - (char *)rstrm->frag_header
                     - BYTES_PER_XDR_UNIT);

        *rstrm->frag_header = htonl(len | eormask);
        len = rstrm->out_finger - rstrm->out_base;
        if ((*(rstrm->writeit))(rstrm->tcp_handle, rstrm->out_base, (int)len)
        != (int)len)
                return FALSE;
        rstrm->frag_header = (u_int32_t *)rstrm->out_base;
        rstrm->out_finger = (caddr_t)rstrm->out_base + BYTES_PER_XDR_UNIT;
        return TRUE;
}

/**
   This routine knows nothing about records!  Only about input buffers
*/
static bool_t fill_input_buf(struct c2_xdr_rec_strm *rstrm)
{
        caddr_t where;
        size_t i;
        int len;

        where = rstrm->in_base;
        i = (size_t)rstrm->in_boundry % BYTES_PER_XDR_UNIT;
        where += i;
        len = rstrm->in_size - i;
        if ((len = (*(rstrm->readit))(rstrm->tcp_handle, where, len)) == -1)
                return FALSE;
        rstrm->in_finger = where;
        where += len;
        rstrm->in_boundry = where;
        return TRUE;
}

static bool_t get_input_bytes(struct c2_xdr_rec_strm *rstrm, caddr_t addr,
			      int len)
{
        int current;

        while (len > 0){
                current = rstrm->in_boundry - rstrm->in_finger;
                if (current == 0)
                {
                        if (!fill_input_buf(rstrm))
                        return FALSE;
                        continue;
                }
                current = (len < current) ? len : current;
                memcpy(addr, rstrm->in_finger, current);
                rstrm->in_finger += current;
                addr += current;
                len -= current;
        }
        return TRUE;
}

/**
   Next two bytes of the input stream are treated as a header
*/
static bool_t set_input_fragment(struct c2_xdr_rec_strm *rstrm)
{
        uint32_t header;

        if (!get_input_bytes(rstrm, (caddr_t)&header, BYTES_PER_XDR_UNIT))
                return FALSE;
        header = ntohl(header);
        rstrm->last_frag = ((header & LAST_FRAG) == 0) ? FALSE : TRUE;
        /*
         * Sanity check. Try not to accept wildly incorrect fragment
         * sizes. Unfortunately, only a size of zero can be identified as
         * 'wildely incorrect', and this only, if it is not the last
         * fragment of a message. Ridiculously large fragment sizes may look
         * wrong, but we don't have any way to be certain that they aren't
         * what the client actually intended to send us. Many existing RPC
         * implementations may sent a fragment of size zero as the last
         * fragment of a message.
         */
        if (header == 0)
                return FALSE;
        rstrm->fbtbc = header & ~LAST_FRAG;
        return TRUE;
}

/**
  Consumes input bytes; knows nothing about records!
*/
static bool_t skip_input_bytes(struct c2_xdr_rec_strm *rstrm, long cnt)
{
        int current;

        while (cnt > 0) {
                current = rstrm->in_boundry - rstrm->in_finger;
                if (current == 0) {
                        if (!fill_input_buf(rstrm))
                                return FALSE;
                        continue;
                }
                current = (cnt < current) ? cnt : current;
                rstrm->in_finger += current;
                cnt -= current;
        }
        return TRUE;
}

static u_int fix_buf_size(u_int s)
{
        if (s < MIN_MSG_SIZE)
                s = DEFAULT_MSG_SIZE;
        return RNDUP(s);
}

