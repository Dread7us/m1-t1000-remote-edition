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

#define WATCH_PREF_SIZE 12U
typedef struct {uint8_t interval,alerts,sensitivity;} room_watch_preferences_t;
#define ROOM_WATCH_DEFAULTS {1,1,1}
static inline bool room_watch_preferences_valid(const room_watch_preferences_t *p) {
    return p->interval<4 && p->alerts<3 && p->sensitivity<3;
}
static inline uint32_t room_watch_interval_ms(const room_watch_preferences_t *p) {
    const unsigned minutes[]={1,2,5,10};return minutes[p->interval<4?p->interval:1]*60000U;
}
static inline bool room_watch_prefs_encode(const room_watch_preferences_t *p,uint8_t *b) {
    if(!room_watch_preferences_valid(p))return false;
    memset(b,0,WATCH_PREF_SIZE);memcpy(b,"RWP1",4);b[4]=p->interval;b[5]=p->alerts;b[6]=p->sensitivity;
    room_watch_put(b+8,room_watch_hash(b,8),4);return true;
}
static inline bool room_watch_prefs_decode(const uint8_t *b,unsigned n,room_watch_preferences_t *p) {
    if(n!=WATCH_PREF_SIZE || memcmp(b,"RWP1",4) || b[7] || room_watch_get(b+8,4)!=room_watch_hash(b,8))return false;
    room_watch_preferences_t q={b[4],b[5],b[6]};
    if(!room_watch_preferences_valid(&q))return false;*p=q;return true;
}
typedef struct {uint8_t on[7],off[7],latched;} room_watch_filter_t;
static inline uint8_t room_watch_changes(const room_watch_snapshot_t *row,const room_watch_snapshot_t *base,
                                         const room_watch_preferences_t *p) {
    if(!room_watch_valid(row) || !room_watch_valid(base) || !room_watch_same_network(row,base))return 128;
    const unsigned counts[]={2,4,8},signals[]={8,12,18};
    unsigned sensitivity=p->sensitivity<3?p->sensitivity:1;
    int dn=(int)row->networks-(int)base->networks,dp=(int)row->peers-(int)base->peers,ds=row->rssi-base->rssi;
    unsigned count=counts[sensitivity],signal=signals[sensitivity];uint8_t bits=0;
    if(dn>=(int)count)bits|=1;if(dn<=-(int)count)bits|=2;
    if(dp>=(int)count)bits|=4;if(dp<=-(int)count)bits|=8;
    if(ds>=(int)signal)bits|=16;if(ds<=-(int)signal)bits|=32;
    if(row->channel!=base->channel)bits|=64;return bits;
}
static inline uint8_t room_watch_confirm(room_watch_filter_t *filter,uint8_t bits) {
    uint8_t events=0;
    if(bits&128){memset(filter->on,0,7);memset(filter->off,0,7);return 0;}
    for(unsigned i=0;i<7;i++) {
        uint8_t bit=(uint8_t)(1U<<i);
        if(bits&bit){filter->off[i]=0;if(filter->on[i]<2)filter->on[i]++;
            if(filter->on[i]==2 && !(filter->latched&bit)){filter->latched|=bit;events|=bit;}}
        else {filter->on[i]=0;if(filter->off[i]<2)filter->off[i]++;
            if(filter->off[i]==2)filter->latched&=(uint8_t)~bit;}
    }return events;
}
static inline bool room_watch_due(uint32_t now,uint32_t finished,uint32_t interval) {
    return (uint32_t)(now-finished)>=interval;
}
unsigned room_watch_status_json(char *out,unsigned capacity);
void room_watch_open(void);
#endif
