//
// Copyright(C) 2005-2014 Simon Howard
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either version 2
// of the License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// DESCRIPTION:
//     Networking module for the PS Vita, which uses the console's own
//     UDP sockets. It is the counterpart of the net_sdl module used by the
//     desktop builds: the netgame layer only ever talks to it through
//     net_module_t, so nothing above this file knows the difference.
//

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include <psp2/net/net.h>
#include <psp2/net/netctl.h>
#include <psp2/sysmodule.h>

#include "doomtype.h"
#include "i_system.h"
#include "m_argv.h"
#include "m_misc.h"
#include "net_defs.h"
#include "net_io.h"
#include "net_packet.h"
#include "net_vita.h"
#include "z_zone.h"

//
// NETWORKING
//

#define DEFAULT_PORT 2342
#define PACKET_BUFFER 1500

/* The stack keeps its own memory, taken from the application: this is the
   pool sceNetInit() hands out of. */
#define NET_STACK_MEMORY (128 * 1024)
static unsigned char net_memory[NET_STACK_MEMORY] __attribute__((aligned(4096)));
static boolean net_stack_ready = false;

static boolean initted = false;
static int port = DEFAULT_PORT;
static int udpsocket = -1;
static unsigned char recvbuffer[PACKET_BUFFER];

typedef struct
{
    net_addr_t net_addr;
    SceNetSockaddrIn vita_addr;
} addrpair_t;

static addrpair_t **addr_table;
static int addr_table_size = -1;

/* ------------------------------------------------------------------ *
 * The network stack                                                 *
 * ------------------------------------------------------------------ */

/* Started on first use: a single player game never touches the network. */
boolean NET_VITA_EnsureStack(void)
{
    SceNetInitParam param;

    if (net_stack_ready)
        return true;

    if (sceSysmoduleLoadModule(SCE_SYSMODULE_NET) < 0)
    {
        printf("NET_VITA: cannot load the network module; is Wi-Fi on?\n");
        return false;
    }

    memset(&param, 0, sizeof(param));
    param.memory = net_memory;
    param.size = sizeof(net_memory);
    param.flags = 0;

    if (sceNetInit(&param) < 0)
    {
        printf("NET_VITA: sceNetInit failed\n");
        return false;
    }

    sceNetCtlInit();
    net_stack_ready = true;

    return true;
}

void NET_VITA_GetLocalAddress(char *buffer, int buffer_len)
{
    SceNetCtlInfo info;

    buffer[0] = '\0';

    if (!NET_VITA_EnsureStack()
     || sceNetCtlInetGetInfo(SCE_NETCTL_INFO_GET_IP_ADDRESS, &info) < 0)
    {
        return;
    }

    M_snprintf(buffer, buffer_len, "%s", info.ip_address);
}

/* One socket for both roles: a client needs no bind (the kernel picks a
   port), a server binds the port the players connect to. */
static int NET_VITA_OpenSocket(boolean bind_port)
{
    SceNetSockaddrIn addr;
    int one = 1;
    int sock;

    sock = sceNetSocket("chexcoop", SCE_NET_AF_INET, SCE_NET_SOCK_DGRAM, 0);

    if (sock < 0)
        return -1;

    if (bind_port)
    {
        memset(&addr, 0, sizeof(addr));
        addr.sin_len = sizeof(addr);
        addr.sin_family = SCE_NET_AF_INET;
        addr.sin_port = sceNetHtons((unsigned short) port);
        addr.sin_addr.s_addr = SCE_NET_INADDR_ANY;

        if (sceNetBind(sock, (SceNetSockaddr *) &addr, sizeof(addr)) < 0)
        {
            sceNetSocketClose(sock);
            return -1;
        }
    }

    /* Never block the game loop waiting for a packet, and let the discovery
       broadcast out. */
    sceNetSetsockopt(sock, SCE_NET_SOL_SOCKET, SCE_NET_SO_NBIO, &one, sizeof(one));
    sceNetSetsockopt(sock, SCE_NET_SOL_SOCKET, SCE_NET_SO_BROADCAST, &one, sizeof(one));

    return sock;
}

/* ------------------------------------------------------------------ *
 * The address table                                                 *
 * ------------------------------------------------------------------ */

static void NET_VITA_InitAddrTable(void)
{
    addr_table_size = 16;

    addr_table = Z_Malloc(sizeof(addrpair_t *) * addr_table_size,
                          PU_STATIC, 0);
    memset(addr_table, 0, sizeof(addrpair_t *) * addr_table_size);
}

static boolean AddressesEqual(SceNetSockaddrIn *a, SceNetSockaddrIn *b)
{
    return a->sin_addr.s_addr == b->sin_addr.s_addr
        && a->sin_port == b->sin_port;
}

/* Finds an address by searching the table.  If the address is not found,
   it is added to the table. */

static net_addr_t *NET_VITA_FindAddress(SceNetSockaddrIn *addr)
{
    addrpair_t *new_entry;
    int empty_entry = -1;
    int i;

    if (addr_table_size < 0)
    {
        NET_VITA_InitAddrTable();
    }

    for (i=0; i<addr_table_size; ++i)
    {
        if (addr_table[i] != NULL
         && AddressesEqual(addr, &addr_table[i]->vita_addr))
        {
            return &addr_table[i]->net_addr;
        }

        if (empty_entry < 0 && addr_table[i] == NULL)
            empty_entry = i;
    }

    // Was not found in list.  We need to add it.

    // Is there any space in the table? If not, increase the table size

    if (empty_entry < 0)
    {
        addrpair_t **new_addr_table;
        int new_addr_table_size;

        // after reallocing, we will add this in as the first entry
        // in the new block of memory

        empty_entry = addr_table_size;

        // allocate a new array twice the size, init to 0 and copy
        // the existing table in.  replace the old table.

        new_addr_table_size = addr_table_size * 2;
        new_addr_table = Z_Malloc(sizeof(addrpair_t *) * new_addr_table_size,
                                  PU_STATIC, 0);
        memset(new_addr_table, 0, sizeof(addrpair_t *) * new_addr_table_size);
        memcpy(new_addr_table, addr_table,
               sizeof(addrpair_t *) * addr_table_size);
        Z_Free(addr_table);
        addr_table = new_addr_table;
        addr_table_size = new_addr_table_size;
    }

    // Add a new entry

    new_entry = Z_Malloc(sizeof(addrpair_t), PU_STATIC, 0);

    new_entry->vita_addr = *addr;
    new_entry->net_addr.handle = &new_entry->vita_addr;
    new_entry->net_addr.module = &net_vita_module;

    addr_table[empty_entry] = new_entry;

    return &new_entry->net_addr;
}

static void NET_VITA_FreeAddress(net_addr_t *addr)
{
    int i;

    for (i=0; i<addr_table_size; ++i)
    {
        if (addr == &addr_table[i]->net_addr)
        {
            Z_Free(addr_table[i]);
            addr_table[i] = NULL;
            return;
        }
    }

    I_Error("NET_VITA_FreeAddress: Attempted to remove an unused address!");
}

static boolean NET_VITA_InitClient(void)
{
    int p;

    if (initted)
        return true;

    //!
    // @category net
    // @arg <n>
    //
    // Use the specified UDP port for communications, instead of
    // the default (2342).
    //

    p = M_CheckParmWithArgs("-port", 1);
    if (p > 0)
        port = atoi(myargv[p+1]);

    if (!NET_VITA_EnsureStack())
        I_Error("NET_VITA_InitClient: the console network is not available");

    udpsocket = NET_VITA_OpenSocket(false);

    if (udpsocket < 0)
    {
        I_Error("NET_VITA_InitClient: Unable to open a socket!");
    }

    initted = true;

    return true;
}

static boolean NET_VITA_InitServer(void)
{
    int p;

    if (initted)
        return true;

    p = M_CheckParmWithArgs("-port", 1);
    if (p > 0)
        port = atoi(myargv[p+1]);

    if (!NET_VITA_EnsureStack())
        I_Error("NET_VITA_InitServer: the console network is not available");

    udpsocket = NET_VITA_OpenSocket(true);

    if (udpsocket < 0)
    {
        I_Error("NET_VITA_InitServer: Unable to bind to port %i", port);
    }

    initted = true;

    return true;
}

static void NET_VITA_SendPacket(net_addr_t *addr, net_packet_t *packet)
{
    SceNetSockaddrIn to;

    if (addr == &net_broadcast_addr)
    {
        memset(&to, 0, sizeof(to));
        to.sin_len = sizeof(to);
        to.sin_family = SCE_NET_AF_INET;
        to.sin_port = sceNetHtons((unsigned short) port);
        to.sin_addr.s_addr = SCE_NET_INADDR_BROADCAST;
    }
    else
    {
        to = *((SceNetSockaddrIn *) addr->handle);
    }

    if (udpsocket < 0)
        return;

    sceNetSendto(udpsocket, packet->data, (unsigned int) packet->len, 0,
                 (SceNetSockaddr *) &to, sizeof(to));
}

static boolean NET_VITA_RecvPacket(net_addr_t **addr, net_packet_t **packet)
{
    SceNetSockaddrIn from;
    unsigned int fromlen;
    int received;

    if (udpsocket < 0)
        return false;

    fromlen = sizeof(from);
    memset(&from, 0, sizeof(from));

    received = sceNetRecvfrom(udpsocket, recvbuffer, sizeof(recvbuffer), 0,
                              (SceNetSockaddr *) &from, &fromlen);

    if (received <= 0)
        return false;

    // The packet has to be copied out: this struct is read one packet at a
    // time and the next call overwrites it.

    *packet = NET_NewPacket(received);
    memcpy((*packet)->data, recvbuffer, received);
    (*packet)->len = received;

    // Address

    *addr = NET_VITA_FindAddress(&from);

    return true;
}

void NET_VITA_AddrToString(net_addr_t *addr, char *buffer, int buffer_len)
{
    SceNetSockaddrIn *vita_addr;
    uint32_t host;
    uint16_t port;

    vita_addr = (SceNetSockaddrIn *) addr->handle;
    host = sceNetNtohl(vita_addr->sin_addr.s_addr);
    port = sceNetNtohs(vita_addr->sin_port);

    M_snprintf(buffer, buffer_len, "%i.%i.%i.%i",
               (host >> 24) & 0xff, (host >> 16) & 0xff,
               (host >> 8) & 0xff, host & 0xff);

    // If we are using the default port we just need to show the IP address,
    // but otherwise we need to include the port.

    if (port != DEFAULT_PORT)
    {
        char portbuf[10];
        M_snprintf(portbuf, sizeof(portbuf), ":%i", port);
        M_StringConcat(buffer, portbuf, buffer_len);
    }
}

/* "1.2.3.4" or "1.2.3.4:2342". Names are not resolved: a console has no DNS
   worth the wait, and the launcher hands over the address it was told. */
static boolean NET_VITA_ParseAddress(const char *text, uint32_t *host, int *out_port)
{
    unsigned int a, b, c, d;
    int count;
    const char *colon;

    count = sscanf(text, "%u.%u.%u.%u", &a, &b, &c, &d);

    if (count != 4 || a > 255 || b > 255 || c > 255 || d > 255)
        return false;

    *host = (a << 24) | (b << 16) | (c << 8) | d;

    colon = strchr(text, ':');
    *out_port = colon != NULL ? atoi(colon + 1) : port;

    return true;
}

net_addr_t *NET_VITA_ResolveAddress(char *address)
{
    SceNetSockaddrIn addr;
    uint32_t host;
    int addr_port;

    if (address == NULL)
        return NULL;

    if (!NET_VITA_ParseAddress(address, &host, &addr_port))
        return NULL;

    memset(&addr, 0, sizeof(addr));
    addr.sin_len = sizeof(addr);
    addr.sin_family = SCE_NET_AF_INET;
    addr.sin_port = sceNetHtons((unsigned short) addr_port);
    addr.sin_addr.s_addr = sceNetHtonl(host);

    return NET_VITA_FindAddress(&addr);
}

// Complete module

net_module_t net_vita_module =
{
    NET_VITA_InitClient,
    NET_VITA_InitServer,
    NET_VITA_SendPacket,
    NET_VITA_RecvPacket,
    NET_VITA_AddrToString,
    NET_VITA_FreeAddress,
    NET_VITA_ResolveAddress,
};
