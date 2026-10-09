//
// net_query_stub.c
//
// The netgame layer can look for a server through a master server on the
// internet (net_query.c in Chocolate Doom), which draws its own window with
// the textscreen library and is of no use to a console playing on the local
// network. What it does provide is a handful of symbols the server and the
// play loop call, so those are here and answer "nothing found": a game hosted
// with -server is joined by address, not by advertisement.
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either version 2
// of the License, or (at your option) any later version.
//

#include <stdio.h>

#include "doomtype.h"
#include "i_system.h"
#include "net_defs.h"
#include "net_query.h"

int NET_StartLANQuery(void)
{
    printf("NET_StartLANQuery: not available in this build\n");
    return 0;
}

int NET_StartMasterQuery(void)
{
    printf("NET_StartMasterQuery: not available in this build\n");
    return 0;
}

void NET_LANQuery(void)
{
    I_Error("The game data browser is not available in this build.\n"
            "Host a game on one console and join it by address on the other.");
}

void NET_MasterQuery(void)
{
    I_Error("The internet server browser is not available in this build.\n"
            "Host a game on one console and join it by address on the other.");
}

void NET_QueryAddress(char *addr)
{
    (void) addr;
    I_Error("Querying a server by address is not available in this build.");
}

net_addr_t *NET_FindLANServer(void)
{
    printf("NET_FindLANServer: not available in this build\n");
    return NULL;
}

int NET_Query_Poll(net_query_callback_t callback, void *user_data)
{
    (void) callback;
    (void) user_data;
    return 6;
}

net_addr_t *NET_Query_ResolveMaster(net_context_t *context)
{
    (void) context;
    return NULL;
}

void NET_Query_AddToMaster(net_addr_t *master_addr)
{
    (void) master_addr;
}

boolean NET_Query_CheckAddedToMaster(boolean *result)
{
    *result = false;
    return false;
}

void NET_Query_MasterResponse(net_packet_t *packet)
{
    (void) packet;
}
