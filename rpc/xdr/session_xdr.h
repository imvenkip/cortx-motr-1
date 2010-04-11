#ifndef _SESSION_XDR_H_

#define _SESSION_XDR_H_

/* the xdr functions */

extern  bool_t xdr_c2_session_cmd (XDR *, c2_session_cmd*);
extern  bool_t xdr_session_id (XDR *, session_id*);

extern  bool_t xdr_session_create_arg (XDR *, session_create_arg*);
extern  bool_t xdr_session_create_out (XDR *, session_create_out*);
extern  bool_t xdr_session_create_ret (XDR *, session_create_ret*);
extern  bool_t xdr_session_destroy_arg (XDR *, session_destroy_arg*);
extern  bool_t xdr_session_destroy_ret (XDR *, session_destroy_ret*);
extern  bool_t xdr_c2_session_adjust_in (XDR *, c2_session_adjust_in*);
extern  bool_t xdr_c2_session_adjust_rep (XDR *, c2_session_adjust_rep*);
extern  bool_t xdr_c2_session_adjust_out (XDR *, c2_session_adjust_out*);
extern  bool_t xdr_c2_session_compound_op (XDR *, c2_session_compound_op*);
extern  bool_t xdr_session_sequence_args (XDR *, session_sequence_args*);
extern  bool_t xdr_compound_op_arg (XDR *, compound_op_arg*);
extern  bool_t xdr_compound_args (XDR *, compound_args*);
extern  bool_t xdr_session_sequence_reply (XDR *, session_sequence_reply*);
extern  bool_t xdr_c2_session_resop (XDR *, c2_session_resop*);
extern  bool_t xdr_c2_compound_reply (XDR *, c2_compound_reply*);

#endif