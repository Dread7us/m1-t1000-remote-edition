/* See COPYING.txt for license details. */
#ifndef M1_WIFI_CLIENT_LAB_H
#define M1_WIFI_CLIENT_LAB_H
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#define CLIENT_LAB_LIMIT 16U
#define CLIENT_LAB_ROW_SIZE 78U
typedef struct {char name[25],station[18],ap[18],ip[16];uint8_t channel;} wifi_client_profile_t;
static inline bool client_mac_valid(const char *mac) {
    if(!mac || strlen(mac)!=17)return false;
    unsigned first=0,nonzero=0;
    for(unsigned i=0;i<17;i++){
        if(i%3==2){if(mac[i]!=':')return false;continue;}
        unsigned char c=(unsigned char)tolower((unsigned char)mac[i]);
        if(!isxdigit(c))return false;unsigned n=c<='9'?c-'0':c-'a'+10;
        if(i<2)first=(first<<4)|n;nonzero|=n;
    }return nonzero && !(first&1U);
}
static inline bool client_mac_equal(const char *a,const char *b) {
    for(unsigned i=0;i<17;i++)if(tolower((unsigned char)a[i])!=tolower((unsigned char)b[i]))return false;
    return true;
}
static inline bool client_private_ip(const char *ip) {
    if(!ip || strlen(ip)>15)return false;if(!ip[0])return true;
    for(const char *p=ip;*p;p++)if(!isdigit((unsigned char)*p) && *p!='.')return false;
    unsigned a,b,c,d;int end=0;
    return sscanf(ip,"%u.%u.%u.%u%n",&a,&b,&c,&d,&end)==4 && !ip[end] && a<=255 && b<=255 && c<=255 && d>0 && d<255 &&
        (a==10 || (a==172 && b>=16 && b<=31) || (a==192 && b==168));
}
static inline bool client_profile_valid(const wifi_client_profile_t *p) {
    if(!memchr(p->name,0,25) || !memchr(p->station,0,18) || !memchr(p->ap,0,18) || !memchr(p->ip,0,16) || !p->name[0] || !client_mac_valid(p->station) || !client_mac_valid(p->ap) ||
        client_mac_equal(p->station,p->ap) || p->channel<1 || p->channel>14 || !client_private_ip(p->ip))return false;
    for(const char *s=p->name;*s;s++)if((unsigned char)*s<32 || (unsigned char)*s==127)return false;
    return true;
}
static inline bool client_test_command(const wifi_client_profile_t *p,char *out,unsigned capacity) {
    if(!client_profile_valid(p))return false;
    int n=snprintf(out,capacity,"AT+M1DEAUTH=\"%s\",%u,\"%s\",5\r\n",p->ap,p->channel,p->station);
    return n>0 && (unsigned)n<capacity;
}
static inline bool client_attempts_reported(const char *reply,unsigned expected) {
    const char *line=strstr(reply,"+M1DEAUTH:");unsigned attempts;int end=0;
    return line && sscanf(line,"+M1DEAUTH:%u%n",&attempts,&end)==1 && attempts==expected &&
        (line[end]=='\r' || line[end]=='\n' || line[end]==0);
}
unsigned wifi_client_lab_status_json(char *out,unsigned capacity);
void wifi_client_lab_open(void);
#endif
