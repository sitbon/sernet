#ifndef __PKT_H__
#define __PKT_H__

#include "local.h"
#include <time.h>

#define PKT_ERROR       (u8)UINT16_MAX
#define PKT_MAGIC_RELAY (u32)0xdeadbeef

typedef u16 pkt_len_t;
typedef u8  pkt_fid_t;
typedef u8  pkt_data_t;

typedef enum {
    PKT_TYPE_AT          = 0x08,
    PKT_TYPE_AT_QPV      = 0x09,
    PKT_TYPE_TX_REQ      = 0x10,
    PKT_TYPE_TX_REQ_EXP  = 0x11,
    PKT_TYPE_REM_CMD_REQ = 0x17,
    PKT_TYPE_AT_RSP      = 0x88,
    PKT_TYPE_MDM_STAT    = 0x8A,
    PKT_TYPE_TX_STAT     = 0x8B,
    PKT_TYPE_RX_IND      = 0x90,
    PKT_TYPE_RX_IND_EXP  = 0x91,
    PKT_TYPE_NODE_ID_IND = 0x95,
    PKT_TYPE_REM_CMD_RSP = 0x97
} pkt_type_t;

typedef struct __attribute__((packed)) {
    u8 addr[8];
} pkt_addr_t;

typedef struct __attribute__((packed)) {
    pkt_len_t   len; // packet size including length field
    pkt_data_t  raw[0];
    pkt_type_t  type : 8;
} pkt_hdr_t;

typedef struct __attribute__((packed)) {
    pkt_hdr_t   hdr;
    pkt_data_t  data[];
} pkt_t;

typedef struct __attribute__((packed)) {
    pkt_hdr_t   hdr;
    pkt_fid_t   fid;
    pkt_addr_t  dst;
    u8          rsv[2];
    u8          bcr;
    u8          txo;
    pkt_data_t  data[];
} tx_pkt_t;

typedef struct __attribute__((packed)) {
    pkt_hdr_t   hdr;
    pkt_addr_t  src;
    u8          rsv[2];
    u8          rxo;
    pkt_data_t  data[];
} rx_pkt_t;

pkt_len_t pkt_decode(const void *src, pkt_len_t len, pkt_t *dst);
pkt_len_t pkt_encode(const pkt_t *src, void *dst);

#endif
