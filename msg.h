#ifndef __MSG_H__
#define __MSG_H__

#include "local.h"
#include "udp.h"
#include "packet.h"
#include "fwd.h"
#include <time.h>

typedef u16 msg_len_t;
typedef u8 msg_flag_t;
typedef u8 msg_data_t;

typedef enum {
    MSG_TYPE_PKT,
    MSG_TYPE_RELAY,
} msg_type_t;

typedef struct __attribute__((packed)) {
    msg_len_t       len; // length of message including this field
    msg_type_t      type : 2;
    msg_flag_t      flag : 6;
} msg_hdr_t;

typedef struct __attribute__((packed)) {
    msg_hdr_t       hdr;
    msg_data_t      data[];
} msg_t;

typedef struct __attribute__((packed)) {
    in_addr_t       src;
    in_port_t       port;
    seq_t           seq;
    struct timespec ts;
} relay_t;

typedef struct __attribute__((packed)) {
    msg_hdr_t       hdr;
    pkt_addr_t      src;
    relay_t         relay;
    msg_data_t      data[];
} relay_msg_t;

typedef struct __attribute__((packed)) {
    u32         magic;
    relay_t     relay;
    pkt_data_t  data[];
} relay_pkt_data_t;

typedef struct __attribute__((packed)) {
    msg_hdr_t       hdr;
    pkt_addr_t      src;
    msg_data_t      data[];
} pkt_msg_t;

#endif
