/*
 * net_vita.h
 *
 * The Vita UDP transport for the netgame layer, the counterpart of the
 * SDL_net module the desktop builds use.
 */

#ifndef NET_VITA_H
#define NET_VITA_H

#include "net_defs.h"

extern net_module_t net_vita_module;

/* Brings the network stack up on its own. A single player game never touches
   the network, so the stack is only started when something asks for an
   address; false means the console has no network to play over. */
boolean NET_VITA_EnsureStack(void);

/* The address of this console on the local network, as text, or an empty
   string while the console has no address yet. Used by the launcher to show
   the address the other players have to join. */
void NET_VITA_GetLocalAddress(char *buffer, int buffer_len);

#endif /* #ifndef NET_VITA_H */
