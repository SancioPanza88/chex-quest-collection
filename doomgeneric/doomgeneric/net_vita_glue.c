//
// net_vita_glue.c
//
// Three pieces of Chocolate Doom live in files this build does not carry, and
// the netgame layer calls into all three:
//
//   * net_query.c  - looking for a game through a master server, which also
//                    draws its own window with the textscreen library. A
//                    console joins by address, so the calls answer "nothing
//                    found" instead.
//   * net_gui.c    - the same window while waiting for the other players to
//                    get ready. The waiting itself is done by the netgame
//                    layer, so what is left here is the loop: pump the client,
//                    pump the server, wait.
//   * net_dedicated.c - a server with no player and no screen. There is no way
//                    to ask for one from the launcher, so it is refused.
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either version 2
// of the License, or (at your option) any later version.
//

#include <stdio.h>

#include "doomtype.h"
#include "i_system.h"
#include "i_timer.h"
#include "net_client.h"
#include "net_defs.h"
#include "net_dedicated.h"
#include "net_query.h"
#include "net_server.h"

/* ------------------------------------------------------------------ *
 * Master server and LAN search (net_query.c)                          *
 * ------------------------------------------------------------------ */

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
    I_Error("The game browser is not available in this build.\n"
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

/* ------------------------------------------------------------------ *
 * Waiting for the game to start (net_gui.c)                           *
 * ------------------------------------------------------------------ */

/* A client that has connected sits here until the server says the game is
   starting: net_waiting_for_launch is cleared by the launch packet. Without a
   screen of its own the loop is only the two pumps and a short pause, which
   is what keeps the console responsive to a server that stops answering. */
void NET_WaitForLaunch(void)
{
    printf("Waiting for the other players...\n");

    while (net_waiting_for_launch)
    {
        NET_CL_Run();
        NET_SV_Run();

        if (!net_client_connected)
        {
            I_Error("Lost connection to server");
        }

        I_Sleep(10);
    }
}

/* ------------------------------------------------------------------ *
 * Dedicated server (net_dedicated.c)                                  *
 * ------------------------------------------------------------------ */

void NET_DedicatedServer(void)
{
    I_Error("-dedicated is not available in this build.");
}
