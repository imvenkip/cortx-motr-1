/**
   Encode/Decode functions for encoding/decoding atomic types into buffer
   vectors. The functionality is very similar to the XDR functions.
*/

enum bufvec_what {
	BUFVEC_ENCODE = 0,
	BUFVEC_DECODE = 1,
	};

int c2_bufvec_uint64(struct c2_bufvec_cursor *vc, uint64_t *val,
		     enum bufvec_what what);

int c2_bufvec_uint32(struct c2_bufvec_cursor *vc, uint32_t *val,
		     enum bufvec_what what);

int c2_bufvec_int(struct c2_bufvec_cursor *vc, int *val,
		     enum bufvec_what what);

int c2_bufvec_uchar(struct c2_bufvec_cursor *vc, unsigned char *val,
		     enum bufvec_what what);

int c2_bufvec_longint(struct c2_bufvec_cursor *vc, long *val,
		     enum bufvec_what what);

int c2_bufvec_float(struct c2_bufvec_cursor *vc, float *val,
		     enum bufvec_what what);

int c2_bufvec_fop(struct c2_bufvec_cursor *vc, struct c2_fop *fop,
		  enum bufvec_what what);



