/* SPDX-License-Identifier: GPL-3.0-only */
#define main upstream_settings_main
#include "settings_test.c"
#undef main
int main(void)
{
    assert(upstream_settings_main()==0);
    sks_t s={0};persist_t p={0};
    p.magic=PERSIST_MAGIC;p.favorites.factory[15][24]=0xA5;
    assert(settings_import(&p,sizeof p));
    memset(s.room,0xAB,sizeof s.room);
    usb_six_put(0,21);usb_six_put(5,22);usb_six_put(9,24);
    settings_export(&s);
    assert(sizeof s>=sizeof p+40 && s.room[16]==0xAB && s.room[31]==0xAB);
    usb_settings_defaults();assert(usb_six_get(0)!=21);
    assert(settings_import(&s,sizeof s)==1);
    assert(usb_six_get(0)==21 && usb_six_get(5)==22 && usb_six_get(9)==24);
    uint8_t archived[sizeof(sks_t)];memset(archived,0,sizeof archived);
    memcpy(archived,&p,sizeof p);uint32_t magic=0x50455238u;
    memcpy(archived,&magic,4);
    uint32_t routes[10]={USB_ROUTES_DEFAULT,0,0,0,0,0,0,0,0,0};
    routes[1]=4u|7u<<5|10u<<10|13u<<15|16u<<20|19u<<25;
    routes[9]=22u|23u<<5|24u<<10|21u<<15;
    memcpy(archived+sizeof p,routes,sizeof routes);
    assert(settings_import(archived,sizeof p+40)==2);
    assert(usb_six_get(0)==4&&usb_six_get(5)==19&&usb_six_get(6)==22&&usb_six_get(9)==21);
    settings_export(&s);usb_settings_defaults();assert(settings_import(&s,sizeof s));
    assert(usb_six_get(9)==21);
    memset(s.room,0,sizeof s.room);usb_six_put(9,24);
    assert(settings_import(&s,sizeof s));assert(usb_six_get(9)==((USB_EXTRA_DEFAULT>>15)&31u));
    assert(!settings_import(archived,3));
    puts("V2 USB settings: SKS1 round trip, reserved tail preservation, PER8 migration and stock defaults passed");
}
