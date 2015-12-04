#include "local.h"
#include "udp.h"
#include <time.h>

typedef u16 pkt_len_t;
typedef u8 pkt_flag_t;
typedef u8 pkt_data_t;

typedef enum {
    PKT_TYPE_RAW,
    PKT_TYPE_RELAY
} pkt_type_t;

#define PKT_TYPE_BIT_LEN   (2)
#define PKT_FLAG_BIT_LEN   (8-PKT_TYPE_BIT_LEN)

typedef struct __attribute__((packed)) {
    pkt_len_t       len;
    pkt_type_t      type : PKT_TYPE_BIT_LEN;
    pkt_flag_t      flag : PKT_FLAG_BIT_LEN;
} pkt_hdr_t;

typedef struct __attribute__((packed)) {
    pkt_hdr_t       hdr;
    pkt_data_t      data[];
} pkt_t;

typedef struct __attribute__((packed)) {
    pkt_hdr_t       hdr;
    in_addr_t       addr;
    in_port_t       port;
    struct timespec ts;
} relay_pkt_t;
