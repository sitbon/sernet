#include "local.h"
#include "packet.h"

#include <stdio.h>
#include <string.h>


#define FRAME_BYTE_SYNC               (u8)0x7e
#define FRAME_BYTE_ESC                (u8)0x7d
#define FRAME_BYTE_ESC_XOR            (u8)0x20
#define FRAME_BYTE_XON                (u8)0x11
#define FRAME_BYTE_XOFF               (u8)0x13

#define FRAME_BYTE_NEEDS_ESCAPE(b)    ((b)==FRAME_BYTE_SYNC||(b)==FRAME_BYTE_ESC||(b)==FRAME_BYTE_XON||(b)==FRAME_BYTE_XOFF)

#define FRAME_LEN_MIN                 (pkt_len_t)4

static pkt_len_t frame_decode(const char *path, const buf_t *src, buf_t *dst, pkt_len_t *len)
{
    pkt_len_t slen = *len, plen = 0, i = 0, j;
    u16 fcs = 0;
    bool esc = false;
    buf_t b = 0;

    *len = 0;

    if (slen < FRAME_LEN_MIN) goto done;
    if (*src != FRAME_BYTE_SYNC) {
        for (i = 1; i < slen; i++) {
            if (src[i] == FRAME_BYTE_SYNC) {
                slen -= i;
                fprintf(stderr, "[%s]\tsync correction: -%i (%i->%i)\n", path, i, slen + i, slen);
                memcpy((void *)src, &src[i], slen);
                goto synced;
            }
        }

        fprintf(stderr, "[%s]\tsync not corrected: trashing %i bytes\n", path, i);
        goto done;
    }

    synced:
    for (i = 1, j = 0; i < slen; i++) {
        b = src[i];

        if (esc) {
            esc = false;
            b ^= FRAME_BYTE_ESC_XOR;
        }
        else if (b == FRAME_BYTE_ESC) {
            esc = true;
            continue;
        }

        if (j > 1) {
            if ((j-2) < plen) {
                dst[j-2] = b;
                fcs += b;
            }
        }
        else {
            if (j == 0) plen = b << 8;
            else {
                plen |= b;
                if (slen < plen + FRAME_LEN_MIN) { goto clear; }
                *len = plen;
            }
        }

        j++;
    }

    if (esc) goto clear;
    if (j != plen + FRAME_LEN_MIN - 1) goto clear;

    fcs = (buf_t)0xff - (buf_t)fcs;
    if (fcs != b) goto clear;

    done:
    return i;
    clear:
    *len = 0;
    return 0;
    fail:
    return PKT_ERROR;
}

static pkt_len_t frame_encode(const buf_t *src, pkt_len_t len, buf_t *dst)
{
    pkt_len_t clen = FRAME_LEN_MIN + len;
    u16 fcs_sum = 0;
    u8 b;

    *dst++ = FRAME_BYTE_SYNC;
    const u8 len_msb = (u8)((len >> 8) & 0xff);
    const u8 len_lsb = (u8)(len & 0xff);

    if (FRAME_BYTE_NEEDS_ESCAPE(len_msb)) {
        *(u8 *)dst++ = FRAME_BYTE_ESC;
        *(u8 *)dst++ = len_msb ^ FRAME_BYTE_ESC_XOR;
        clen++;
    } else {
        *(u8 *)dst++ = len_msb;
    }

    if (FRAME_BYTE_NEEDS_ESCAPE(len_lsb)) {
        *(u8 *)dst++ = FRAME_BYTE_ESC;
        *(u8 *)dst++ = len_lsb ^ FRAME_BYTE_ESC_XOR;
        clen++;
    } else {
        *(u8 *)dst++ = len_lsb;
    }

    while (len) {
        b = *(u8 *)src++;
        fcs_sum += b;

        if (FRAME_BYTE_NEEDS_ESCAPE(b)) {
            clen++;
            *(u8 *)dst++ = FRAME_BYTE_ESC;
            b ^= FRAME_BYTE_ESC_XOR;
        }

        *(u8 *)dst++ = b;
        len--;
    }

    *(u8 *)dst++ = (u8)0xFF - (u8)fcs_sum;
    return clen;
}

pkt_len_t pkt_decode(const char *path, const void *src, pkt_len_t len, pkt_t *dst)
{
    dst->hdr.len = len;
    const pkt_len_t res = frame_decode(path, src, dst->hdr.raw, &dst->hdr.len);

    if (res > 0 && res <= len && dst->hdr.len > 0) {
        dst->hdr.len += sizeof(pkt_len_t);
        pkt_len_t tlen;

        switch (dst->hdr.type) {
            case PKT_TYPE_RX_IND:
                tlen = sizeof(rx_pkt_t);
                break;
            case PKT_TYPE_TX_REQ:
                tlen = sizeof(tx_pkt_t);
                break;
            default:
                dst->hdr.len = 0;
                return res;
        }

        if (dst->hdr.len < tlen) {
            fprintf(stderr, "[%s]\tuart rx underflow: len=%i need>=%i\n", path, dst->hdr.len, tlen);
            dst->hdr.len = 0;
        }
    }

    return res;
}

pkt_len_t pkt_encode(const pkt_t *src, void *dst)
{
    return frame_encode(src->hdr.raw, src->hdr.len - (pkt_len_t)sizeof(pkt_len_t), dst);
}
