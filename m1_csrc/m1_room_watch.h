/* See COPYING.txt for license details. */
#ifndef M1_ROOM_WATCH_H
#define M1_ROOM_WATCH_H
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#define ROOM_WATCH_WIRE_SIZE 70U
typedef struct {
    char network[33], bssid[18];
    unsigned networks, peers, channel;
    int rssi;
    uint32_t uptime;
} room_watch_snapshot_t;
static inline bool room_watch_valid(const room_watch_snapshot_t *s) {
    if (!memchr(s->network,0,sizeof(s->network)) || !s->network[0] ||
        !memchr(s->bssid,0,sizeof(s->bssid)) || strlen(s->bssid)!=17 ||
        s->channel<1 || s->channel>14 || s->rssi < -127 || s->rssi>0 ||
        s->networks>4096 || s->peers>253) return false;
    for(unsigned i=0;i<17;i++) {
        char c=s->bssid[i];
        if (i%3==2 ? c!=':' : !((c>='0' && c<='9') || (c>='a' && c<='f') || (c>='A' && c<='F'))) return false;
    }
    for(unsigned i=0;s->network[i];i++) if((unsigned char)s->network[i]<32) return false;
    return true;
}
static inline bool room_watch_connected(const char *reply,room_watch_snapshot_t *s) {
    const char *line=strstr(reply,"+CWJAP:");
    return line && sscanf(line,"+CWJAP:\"%32[^\"]\",\"%17[^\"]\",%u,%d",
        s->network,s->bssid,&s->channel,&s->rssi)==4 && room_watch_valid(s);
}
static inline uint32_t room_watch_hash(const uint8_t *b,unsigned n) {
    uint32_t h=2166136261U;while(n--)h=(h^*b++)*16777619U;return h;
}
static inline void room_watch_put(uint8_t *b,uint32_t n,unsigned bytes) {
    for(unsigned i=0;i<bytes;i++)b[i]=(uint8_t)(n>>(8*i));
}
static inline uint32_t room_watch_get(const uint8_t *b,unsigned bytes) {
    uint32_t n=0;for(unsigned i=0;i<bytes;i++)n|=(uint32_t)b[i]<<(8*i);return n;
}
static inline bool room_watch_encode(const room_watch_snapshot_t *s,uint8_t out[ROOM_WATCH_WIRE_SIZE]) {
    if(!room_watch_valid(s))return false;
    memset(out,0,ROOM_WATCH_WIRE_SIZE);memcpy(out,"RSW1",4);
    memcpy(out+4,s->network,strlen(s->network));memcpy(out+37,s->bssid,17);
    room_watch_put(out+55,s->networks,2);room_watch_put(out+57,s->peers,2);
    room_watch_put(out+59,(uint16_t)(int16_t)s->rssi,2);out[61]=(uint8_t)s->channel;
    room_watch_put(out+62,s->uptime,4);room_watch_put(out+66,room_watch_hash(out,66),4);return true;
}
static inline bool room_watch_decode(const uint8_t *b,unsigned size,room_watch_snapshot_t *s) {
    if(size!=ROOM_WATCH_WIRE_SIZE || memcmp(b,"RSW1",4) || room_watch_get(b+66,4)!=room_watch_hash(b,66))return false;
    room_watch_snapshot_t row={0};memcpy(row.network,b+4,33);memcpy(row.bssid,b+37,18);
    row.networks=room_watch_get(b+55,2);row.peers=room_watch_get(b+57,2);
    row.rssi=(int16_t)room_watch_get(b+59,2);row.channel=b[61];row.uptime=room_watch_get(b+62,4);
    if(!room_watch_valid(&row))return false;*s=row;return true;
}
static inline bool room_watch_same_network(const room_watch_snapshot_t *a,const room_watch_snapshot_t *b) {
    for(unsigned i=0;i<17;i++) {
        char x=a->bssid[i],y=b->bssid[i];
        if(x>='A' && x<='F')x+=32;if(y>='A' && y<='F')y+=32;
        if(x!=y)return false;
    }
    return !strcmp(a->network,b->network);
}
void room_watch_open(void);
#endif
