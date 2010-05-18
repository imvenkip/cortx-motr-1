#ifndef __COLIBRI_XDR_COMMON_H__

#define __COLIBRI_XDR_COMMON_H__

/** 
 the xdr functions 

 each function is converted one type from network to local representations.
 function must return true iif conversion successuly 
*/


bool c2_xdr_session_id(void *, struct c2_session_id *);

#endif
