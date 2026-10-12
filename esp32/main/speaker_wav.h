// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
typedef struct { uint32_t rate; unsigned channels; size_t offset, bytes; } speaker_wav_t;
static inline uint32_t speaker_le32(const uint8_t *p) {
    return (uint32_t)p[0] | (uint32_t)p[1]<<8 | (uint32_t)p[2]<<16 | (uint32_t)p[3]<<24;
}
static inline uint16_t speaker_le16(const uint8_t *p) { return p[0] | (uint16_t)p[1]<<8; }
static inline bool speaker_parse_wav(const uint8_t *p, size_t n, speaker_wav_t *w) {
    if (n < 12 || memcmp(p,"RIFF",4) || memcmp(p+8,"WAVE",4)) return false;
    size_t end = (size_t)speaker_le32(p+4) + 8;
    if (end < 12 || end > n) return false;
    bool fmt = false, data = false;
    memset(w, 0, sizeof(*w));
    for (size_t pos=12; pos+8 <= end;) {
        size_t len=speaker_le32(p+pos+4), off=pos+8;
        if (len > end-off) return false;
        if (!memcmp(p+pos,"fmt ",4)) {
            if (fmt || len<16 || speaker_le16(p+off)!=1 || speaker_le16(p+off+14)!=16) return false;
            w->channels=speaker_le16(p+off+2); w->rate=speaker_le32(p+off+4);
            if ((w->channels!=1 && w->channels!=2) || w->rate<8000 || w->rate>48000 ||
                speaker_le16(p+off+12)!=w->channels*2 ||
                speaker_le32(p+off+8)!=w->rate*w->channels*2) return false;
            fmt=true;
        } else if (!memcmp(p+pos,"data",4)) {
            if (data) return false;
            w->offset=off; w->bytes=len; data=true;
        }
        size_t padded=len+(len&1);
        if (padded > end-off) return false;
        pos=off+padded;
    }
    return fmt && data && w->bytes>0 && w->bytes%(w->channels*2)==0;
}
