#ifndef __COLIBRI_SESSION_XDR_H__

#define __COLIBRI_SESSION_XDR_H__

/** 
 the xdr functions 

 each function is converted one type from network to local representations.
 function must return true iif conversion successuly 
*/


struct c2_session_create_arg;
extern  bool c2_xdr_session_create_arg (void *, struct c2_session_create_arg *);

struct c2_session_create_ret;
extern  bool c2_xdr_session_create_ret (void *, struct c2_session_create_ret *);

struct c2_session_destroy_arg;
extern  bool c2_xdr_session_destroy_arg (void *, struct c2_session_destroy_arg *);

struct c2_session_destroy_ret;
extern  bool c2_xdr_session_destroy_ret (void *, struct c2_session_destroy_ret *);

#endif
