#ifndef __COLIBRI_RPC_SESSION_H_

#define __COLIBRI_RPC_SESSION_H_

/**
@page rpc-session rpc session implementation

@section def Definition

	A session is a dynamically created, long-lived server object created and
destroyed by a client requests. It function is to maintain the server's state
relative to the connection(s) belonging to a client instance.

	Slot is dynamically created server object. Slot created by a client request and
attached to active client session. Its function is to maintain the order of
requests execution. A slot contains a sequence ID and the cached
reply corresponding to the request sent with that sequence ID.

	Slots incorporates into slot table.

	Slot ID is index in slot table.

@section sessionfunct Functional specification


session module create to create a code bases on description from RFC XXXX.
and have a two parts.

\li client part - responsible to make a stimulus to create and destroy sessions,
send (or resend) request to replier.

\li server part - responsible to handle incoming request, create or destroy
session bases on client requests.

Communication between these parts a processed via RPC messages.

@section sessionlogspec Logical specification


@section flow State flow

@ref rpc-types
@ref rpc-session-cli
@ref rpc-session-srv
@ref rpc-lib
@ref rpc-compound

*/

#endif
