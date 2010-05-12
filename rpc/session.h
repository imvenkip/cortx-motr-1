#ifndef __COLIBRI_RPC_SESSION_H_

#define __COLIBRI_RPC_SESSION_H_

/**
@page rpc-session rpc session implementation

@section def Definition

	A session is a dynamically created, long-lived server object created and
destroyed by a client requests. It function is to maintain the server's state
relative to the connection(s) belonging to a client instance.

	Slot is dynamically created server object. Slot created by a client request and
attached to active client session. It function is to maintain the order of
requests execution. A slot contains a sequence ID and the cached
reply corresponding to the request sent with that sequence ID.

	Slots incorporates into slot table.

	Slot ID is index in slot table.

@section sessionfunct Functional specification


session module createated to flow description from RFC XXXX.
and have a two parts.

\li client part - responsible to make a stimulus to create and destroy sessions,
send (or resend) request to replier.
@ref rpc-session-cli

\li server part - responsible to handle incoming request, create or destroy
session bases on client requests.
@ref rpc-session-srv

Communication between these parts a processed via RPC messages. Format of messages
is described in rpc types document. @ref rpc-types

To reduce count of RPC's and protect order of excecution number of operations merged in
single compound RPC.
@ref rpc-compound


@section sessionlogspec Logical specification


@section flow State flow

@ref rpc-lib

*/

#endif
