#ifndef C2_XDR_REC_H_
#define C2_XDR_REC_H_
/**
   @page le XDR Little Endian library.

   @section Overview

   XDR is inherently big endian. A majority of processors now are little
   endian, and we would save processor and bus cycles by not converting the XDR
   data from LE to BE and then back to LE.

   @section req Requirements.

   For colibri, we need to implement little-endianness in XDR for both kernel
   and user layer code.

   @section defn Definitions.

   - XDR Stream : An instance of struct XDR ( see usr/include/rpc/xdr.h ) which
   contains operation to be applied to that stream ( ENCODE, DECODE, FREE), an
   operations vector for the particular implementation of xdr.

   - XDR operation vectors: These operation vectors are part of XDR stream
   and these are initialized to the various XDR operations based on the XDR
   implementations. It is in these ops where the actual conversions of
   little endian to big endian and vice versa occur. Refer xdr_mem.c, xdr_rec.c
   and xdr_stdio.c of glibc sunrpc source code for more details.

    @section background Background

   - A fop format describes fop instance. Each fop instance data
   structure is described as a tree of fields. Leaves of this tree are fields
   of "atomic" types (void, byte, u32 and u64) and non-leaf nodes are
   "aggregation" types ( record, union, sequence or typedef).

   - Currently the fields of atomic types use the following standard
   XDR filters ( generic XDR routines ) for conversion :

	void  - xdr_void (....)
	byte -  xdr_char (....)
	uint32 - xdr_uint32_t (.....)
	uint64 - xdr_uint64_t (.....)

	- Each of the aggregation fields is composed of childs and the
	corresponding XDR filters are used for conversion for each child based
	on their types.

	- li In addition to the above, xdr_bytes( ... ), xdr_array ( ... ) and
	xdr_u_int ( ... ) filters have been used in 'union' and 'sequence'
	aggregation fields.


   @section spec Functional and Logical Specifications

   @subsection Kernel-layer-XDR
   For the kernel layer little endianness, we replace htonl() and ntohl() with
   inbuilt linux kernel macros cpu_to_le32() and le32_to_cpu()

   @subsection User-Layer-XDR

   - An XDR stream is obtained by calling the appropriate creation routine which
     creates an instance of an XDR object/handle that is tailored to the
     specific properties of the stream.

   - Streams currently exist for  deserialization of data to or from standard
     I/O FILE streams ( xdr_stdio.c ), record streams ( xdr_rec.c ) and memory
     ( xdr_mem.c ).

   - For colibri, currently, we use sunrpc over TCP ( though this might change
     soon ).The XDR stream is created by the sunrpc by internally calling
     xdrrec_create() function defined in xdr_rec.c. On the client side the call
     sequence is : clnttcp_create () --> xdrrec_create().

   - In future, once the use of sunrpc is deprecated in colibri,
     we can use xdrrec_create to register our own functions tailored to the
     specific transport used ( eg: RDMA ).

   - The various  xdr operation vectors would be initialized
     to the functions defined in c2_xdr_rec.c. The actual conversion
     of little endian to big endian and vice versa takes place in
     these functions. The operation vectors defined in c2_xdr_rec.c are :-

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

    - For making the XDR library as little endian, we need to replace the
      htonl and ntohl macros in the various xdr_ops defined in xdr_rec.c
      ( from the glibc rpc source ) with the c2_le().The rewritten xdr_rec.c
      with our little endian macros would be compiled to build a
      "xdr-little-endian"library. The various operation vectors shown above
      would be exported by our library.

    - The symbols that would be exported by this library would be
      internally invoked by the various xdr filters used in colibri fops.
      For eg: xdr_uint32_t ( ... ) will invoke the xdrrec_getlong ( ... )
      and xdrrec_putlong ( ... ) for decoding and encoding from and to an
      XDR stream. Similarly xdr_bytes ( ... ) internally will invoke
      xdrrec_getbytes( ... ) and xdr_putbytes(...) exported from our library.

    - In future, once the use of sunrpc is deprecated in colibri, we can use
      xdrrec_create to register our own functions tailored to the specific
      transport used ( eg: RDMA ).
*/
#include <rpc/xdr.h>
#include <rpc/rpc.h>

#define LAST_FRAG (1UL << 31)
#define MCALL_MSG_SIZE 24

/**
   A record is composed of one or more record fragments.A record fragment is a
   two-byte header followed by zero to 2**32-1 bytes.  The header is treated as
   a long unsigned The low order 31 bits are a byte count of the fragment.
   The highest order bit is a boolean:1 => this fragment is the last
   fragment of the record, 0 => this fragment is followed by more fragment(s).
   The fragment/record machinery is not general;  it is constructed to meet the
   needs of xdr and rpc based on tcp
*/
struct c2_xdr_rec_strm {
        /** The opaque tcp connection handle which is passed as a parameter
        to readit and writeit functions */
        caddr_t         tcp_handle;
        /** Data to be serialized/deserialized goes here */
        caddr_t         the_buffer;
        /*
        * out-going bits
        */
        /** Procedure to write the serialized data onto the tcp stream */
        int             (*writeit)(char *, char *, int);
        /** output buffer (points to frag header) */
        caddr_t         out_base;
        /** next output position */
        caddr_t         out_finger;
        /** can write data up to this address */
        caddr_t         out_boundry;
        /** beginning of curren fragment */
        u_int32_t       *frag_header;
        /** true if buffer sent in middle of record */
        bool_t          frag_sent;
        /*
         * in-coming bits
         */
        /** Procedure to read incoming data from the tcp stream */
        int             (*readit) (char *, char *, int);
        /** fixed size of the input buffer */
        u_long          in_size;
        /** reading will start from this address */
        caddr_t         in_base;
        /** location of next byte to be had */
        caddr_t         in_finger;
        /** can read up to this location */
        caddr_t         in_boundry;
        /** fragment bytes to be consumed  */
        long            fbtbc;
        /** True if this is the last fragment in the record */
        bool_t          last_frag;
        /** The send buffer size */
        u_int           sendsize;
        /** The receive buffer size */
        u_int           recvsize;
};

/**
   These are the operation vectors which are defined on an XDR record based
   stream - refer rpc/xdr.h
*/
bool_t xdrrec_getlong (XDR *, long *);
bool_t xdrrec_putlong (XDR *, const long *);
bool_t xdrrec_getbytes (XDR *, caddr_t, u_int);
bool_t xdrrec_putbytes (XDR *, const char *, u_int);
u_int xdrrec_getpos (const XDR *);
bool_t xdrrec_setpos (XDR *, u_int);
int32_t *xdrrec_inline (XDR *, u_int);
void xdrrec_destroy (XDR *);
bool_t xdrrec_getint32 (XDR *, int32_t *);
bool_t xdrrec_putint32 (XDR *, const int32_t *);

const struct xdr_ops c2_xdrrec_ops;

/**
   Convert the incoming 4 byte data to little endian format. This inline func
   will replace the existing htonl() and ntohl() macrosin colibri's XDR library
   On most of the CPUs today, this will just return the passed data without any
   conversion, thus savinga few processor and bus cycles spent on converting
   le to be and then back to le.The BIG_ENDIAN option is available just in case
   we want to run colibri in BIG_ENDIAN machines.

   @param x -> integer to be converted

   @retval the passed parameter in little endian format.
*/

static inline uint32_t c2_le (uint32_t x)
{
#if BYTE_ORDER == LITTLE_ENDIAN
	return x;

#elif BYTE_ORDER == BIG_ENDIAN
	return __bswap_32 (x);
#else
#error "What system are you on?"
#endif
}

#endif /* C2_NET_XDR_REC_H_ */

