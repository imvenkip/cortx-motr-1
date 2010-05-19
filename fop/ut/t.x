
typedef uint64_t c2_bindex_t ;
struct c2_fid {
	uint64_t  f_seq;
	uint64_t  f_oid;
} ;
struct iovec {
	uint8_t  iv_buf<>;
} ;
struct fop_write {
	struct c2_fid  wr_fid;
	c2_bindex_t  wr_offset;
	iovec  wr_iovec;
} ;
