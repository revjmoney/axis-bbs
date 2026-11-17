// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// ESP-IDF's mDNS query API, as the board's modem search calls it. Not used on Linux (plan 1 is solo; the V3 is
// reached by serial): a search logs once and never starts, so applyNetwork() never waits for one.
#pragma once
#include <cstddef>
#include <cstdint>

#define ESP_IPADDR_TYPE_V4 0
#define ESP_IPADDR_TYPE_V6 6
#define MDNS_TYPE_A 0x0001
#define MDNS_TYPE_PTR 0x000C
#define MDNS_TYPE_TXT 0x0010
#define MDNS_TYPE_AAAA 0x001C
#define MDNS_TYPE_SRV 0x0021
#define MDNS_TYPE_ANY 0x00FF

typedef struct {
    uint32_t addr;  // network byte order
} esp_ip4_addr_t;
typedef struct {
    uint32_t addr[4];
    uint8_t  zone;
} esp_ip6_addr_t;
typedef struct {
    union {
        esp_ip6_addr_t ip6;
        esp_ip4_addr_t ip4;
    } u_addr;
    uint8_t type;
} esp_ip_addr_t;

typedef struct mdns_ip_addr_s {
    esp_ip_addr_t          addr;
    struct mdns_ip_addr_s* next;
} mdns_ip_addr_t;

typedef struct {
    const char* key;
    const char* value;
} mdns_txt_item_t;

typedef struct mdns_result_s {
    struct mdns_result_s* next;
    char*                 instance_name;
    char*                 service_type;
    char*                 proto;
    char*                 hostname;
    uint16_t              port;
    mdns_txt_item_t*      txt;
    uint8_t*              txt_value_len;
    size_t                txt_count;
    mdns_ip_addr_t*       addr;
} mdns_result_t;

typedef struct mdns_search_once_s mdns_search_once_t;
typedef void (*mdns_query_notify_t)(mdns_search_once_t* search);

mdns_search_once_t* mdns_query_async_new(const char* name, const char* service, const char* proto, uint16_t type,
                                         uint32_t timeoutMs, size_t maxResults, mdns_query_notify_t notify);  // hostnet.cpp
inline bool mdns_query_async_get_results(mdns_search_once_t*, uint32_t, mdns_result_t** results) {
    if (results) *results = nullptr;
    return false;
}
inline bool mdns_query_async_delete(mdns_search_once_t*) { return true; }
inline void mdns_query_results_free(mdns_result_t*) {}
