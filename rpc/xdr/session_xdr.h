#ifndef _SESSION_XDR_H_

#define _SESSION_XDR_H_

/* the xdr functions */

extern  bool xdr_c2_session_cmd (XDR *, enum c2_session_cmd*);
extern  bool xdr_session_id (XDR *, struct session_id*);

extern  bool xdr_session_create_arg (XDR *, struct session_create_arg*);
extern  bool xdr_session_create_out (XDR *, struct session_create_out*);
extern  bool xdr_session_create_ret (XDR *, struct session_create_ret*);
extern  bool xdr_session_destroy_arg (XDR *, struct session_destroy_arg*);
extern  bool xdr_session_destroy_ret (XDR *, struct session_destroy_ret*);
extern  bool xdr_c2_session_adjust_in (XDR *, struct c2_session_adjust_in*);
extern  bool xdr_c2_session_adjust_rep (XDR *, struct c2_session_adjust_rep*);
extern  bool xdr_c2_session_adjust_out (XDR *, struct c2_session_adjust_out*);
extern  bool xdr_c2_session_compound_op (XDR *, struct c2_session_compound_op*);
extern  bool xdr_session_sequence_args (XDR *, struct session_sequence_args*);
extern  bool xdr_compound_op_arg (XDR *, struct compound_op_arg*);
extern  bool xdr_compound_args (XDR *, struct compound_args*);
extern  bool xdr_session_sequence_reply (XDR *, struct session_sequence_reply*);
extern  bool xdr_c2_session_resop (XDR *, struct c2_session_resop*);
extern  bool xdr_c2_compound_reply (XDR *, struct c2_compound_reply*);

#endif
