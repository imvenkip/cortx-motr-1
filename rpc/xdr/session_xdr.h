#ifndef __COLIBRI_SESSION_XDR_H__

#define __COLIBRI_SESSION_XDR_H__

/** 
 the xdr functions 

 each function is converted one type from network to local representations.
 function must return true iif conversion successuly 
*/


extern  bool xdr_session_id (void *, struct session_id *);

extern  bool xdr_session_create_arg (void *, struct session_create_arg *);
extern  bool xdr_session_create_out (void *, struct session_create_out *);
extern  bool xdr_session_create_ret (void *, struct session_create_ret *);
extern  bool xdr_session_destroy_arg (void *, struct session_destroy_arg *);
extern  bool xdr_session_destroy_ret (void *, struct session_destroy_ret *);

extern  bool xdr_c2_session_compound_op (void *, struct c2_session_compound_op*);
extern  bool xdr_session_sequence_args (void *, struct session_sequence_args*);
extern  bool xdr_compound_op_arg (void *, struct compound_op_arg *);
extern  bool xdr_compound_args (void *, struct compound_args *);
extern  bool xdr_session_sequence_reply (void *, struct session_sequence_reply *);
extern  bool xdr_c2_session_resop (void *, struct c2_session_resop *);
extern  bool xdr_c2_compound_reply (void *, struct c2_compound_reply *);

#endif
