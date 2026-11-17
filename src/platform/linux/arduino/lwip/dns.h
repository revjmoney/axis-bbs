// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// lwIP's DNS lookup, as the board's code calls it. A dotted quad answers at once (ERR_OK); a name is looked up with
// getaddrinfo on a short-lived thread (ERR_INPROGRESS) and the callback runs later from the main loop, between two
// loop() turns (hostnetPoll in hostnet.h). Body in hostnet.cpp.
#pragma once
#include <cstdint>

typedef int8_t err_t;
enum { ERR_OK = 0, ERR_MEM = -1, ERR_TIMEOUT = -3, ERR_INPROGRESS = -5, ERR_VAL = -6, ERR_ARG = -16 };

typedef struct ip4_addr {
    uint32_t addr;  // network byte order
} ip4_addr_t;
typedef struct ip6_addr {
    uint32_t addr[4];
    uint8_t  zone;
} ip6_addr_t;
#define IPADDR_TYPE_V4 0U
#define IPADDR_TYPE_V6 6U
typedef struct ip_addr {
    union {
        ip6_addr_t ip6;
        ip4_addr_t ip4;
    } u_addr;
    uint8_t type;
} ip_addr_t;
#define IP_IS_V4(ipaddr) ((ipaddr)->type == IPADDR_TYPE_V4)
#define ip_2_ip4(ipaddr) (&((ipaddr)->u_addr.ip4))

typedef void (*dns_found_callback)(const char* name, const ip_addr_t* ipaddr, void* callback_arg);
err_t dns_gethostbyname(const char* hostname, ip_addr_t* addr, dns_found_callback found, void* callback_arg);
