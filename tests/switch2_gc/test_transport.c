// SPDX-License-Identifier: Apache-2.0
// Included after the complete production transport, with only platform headers mocked.
#include <assert.h>
#include <stdio.h>
#include "ns2_pairing_crypto.h"

static bool ready = true, hid_ready = true, busy, claim = true, queue_ok = true;
static uint32_t now;
static unsigned hid_count, in_count, out_count;
static uint8_t hid_id, hid[63], control[256], sent[64];
static uint16_t control_len, sent_len;
bool tud_ready(void) { return ready; }
bool tud_hid_n_ready(uint8_t i) { (void)i; return hid_ready && ready; }
bool tud_hid_n_report(uint8_t i, uint8_t id, const void *p, uint16_t n) {
    (void)i; assert(n == 63); hid_id = id; memcpy(hid,p,n); hid_count++; return true;
}
bool tud_control_xfer(uint8_t rh, const tusb_control_request_t *r, void *p, uint16_t n) {
    (void)rh; (void)r; memcpy(control,p,n); control_len=n; return true;
}
bool tud_control_status(uint8_t rh, const tusb_control_request_t *r) { (void)rh; (void)r; return true; }
bool usbd_edpt_open(uint8_t rh, const tusb_desc_endpoint_t *ep) { (void)rh; (void)ep; return true; }
bool usbd_edpt_busy(uint8_t rh, uint8_t ep) { (void)rh; (void)ep; return busy; }
bool usbd_edpt_claim(uint8_t rh, uint8_t ep) { (void)rh; (void)ep; return claim; }
bool usbd_edpt_xfer(uint8_t rh, uint8_t ep, uint8_t *p, uint16_t n) {
    (void)rh;
    if (!queue_ok) return false;
    if (ep == 0x82) { in_count++; sent_len=n; memcpy(sent,p,n); }
    else { assert(ep == 2 && n == 64); out_count++; }
    return true;
}
uint32_t get_absolute_time(void) { return now; }

static void command(uint8_t id, uint8_t sub, uint8_t arg) {
    assert(bulk.out_armed && !bulk.reply_len);
    memset(bulk.rx,0,64); bulk.rx[0]=id; bulk.rx[1]=0x91; bulk.rx[3]=sub; bulk.rx[8]=arg;
    driver_xfer(0,2,XFER_RESULT_SUCCESS,16);
}
static void complete(void) {
    assert(bulk.in_flight);
    driver_xfer(0,0x82,XFER_RESULT_SUCCESS,bulk.reply_len);
    assert(bulk.out_armed && !bulk.in_flight && !bulk.reply_len);
}
int main(void) {
    mode_init();
    const uint8_t *config=switch_gc_config_descriptor();
    assert(config[2] == 80 && config[4] == 2);
    assert(switch_gc_device_descriptor()[10] == 0x73);
    assert(driver_open(0,(const tusb_desc_interface_t *)(config+57),23) == 23);
    assert(bulk.out_armed);
    switch_gc_task(); assert(hid_count == 0); // No unsolicited input before selection.
    tusb_control_request_t req={0xC0,2,0,0,16};
    assert(switch_gc_vendor_control_xfer(0,0,&req) && control_len==16);
    req.bRequest=3; req.wLength=64;
    assert(switch_gc_vendor_control_xfer(0,0,&req) && control_len==64 && control[20]==0x73);
    req.bRequest=4; req.bmRequestType=0x40; req.wLength=0;
    assert(switch_gc_vendor_control_xfer(0,0,&req));
    req.bRequest=3; req.wLength=64;
    assert(!switch_gc_vendor_control_xfer(0,0,&req)); // Reject reversed direction.
    req.bRequest=2; req.bmRequestType=0xC0; req.wLength=7;
    assert(switch_gc_vendor_control_xfer(0,0,&req) && control_len==7);

    command(3,0x0D,0); assert(sent_len==12 && sent[5]==0xF8 && sent[8]==1); complete();
    busy=true; command(3,0x0A,0x0A);
    assert(bulk.reply_len==8 && !bulk.in_flight && !bulk.out_armed);
    unsigned previous=in_count;
    switch_gc_task(); assert(in_count==previous);
    busy=false; claim=false; switch_gc_task(); assert(in_count==previous);
    claim=true; queue_ok=false; switch_gc_task(); assert(in_count==previous && bulk.reply_len==8);
    queue_ok=true; switch_gc_task(); assert(in_count==previous+1);
    uint8_t retained[64]; memcpy(retained,bulk.tx,64);
    driver_xfer(0,0x82,XFER_RESULT_FAILED,0);
    assert(bulk.in_flight && memcmp(retained,bulk.tx,64)==0);
    complete();

    profile_output_t p={128,128,128,128,0,127};
    uint32_t b=JP_BUTTON_B1|JP_BUTTON_B2|JP_BUTTON_B3|JP_BUTTON_B4|JP_BUTTON_S1|
        JP_BUTTON_L1|JP_BUTTON_R1|JP_BUTTON_DU;
    assert(send_report(0,NULL,&p,b)); switch_gc_task();
    assert(hid_id==0x0A && hid[2]==0x3F && hid[3]==0x18);
    assert(hid[12]==255 && hid[13]==255); // Both final clicks force full travel.
    assert(hid[5]==0 && hid[6]==8 && hid[7]==128); // Exact stick centers.
    send_report(0,NULL,&p,JP_BUTTON_S2); switch_gc_task(); assert(hid[2]==0x40 && hid[3]==0);
    send_report(0,NULL,&p,JP_BUTTON_S1|JP_BUTTON_S2); switch_gc_task();
    assert(hid[2]==0 && hid[3]==0x20); // ZL combo consumes native Z and Start.
    send_report(0,NULL,&p,JP_BUTTON_S2|JP_BUTTON_DU); switch_gc_task();
    assert(hid[2]==0 && hid[3]==0x40 && hid[4]==0); // Minus consumes Start and Up.
    send_report(0,NULL,&p,JP_BUTTON_S2|JP_BUTTON_DD); switch_gc_task();
    assert(hid[2]==0 && hid[3]==0 && hid[4]==1); // Home consumes Start and Down.
    send_report(0,NULL,&p,JP_BUTTON_DU|JP_BUTTON_S1); switch_gc_task();
    assert(hid[2]==0x20 && hid[3]==8 && hid[4]==0); // Unmodified Up and Z still pass through.
    assert(hid[12]==0 && hid[13]==127); // Menu combinations do not change trigger travel.
    p.l2_analog=255; p.r2_analog=255; send_report(0,NULL,&p,0); switch_gc_task();
    assert(hid[2]==0 && hid[3]==0 && hid[12]==255 && hid[13]==255);
    p.l2_analog=p.r2_analog=0; send_report(0,NULL,&p,JP_BUTTON_L2|JP_BUTTON_R2);
    switch_gc_task(); assert(hid[2]==0 && hid[3]==0 && hid[12]==0 && hid[13]==0);
    // A digital-only controller supplies no analog data on either side.
    send_report(0,NULL,&p,JP_BUTTON_L1); switch_gc_task();
    assert(hid[12]==255 && hid[13]==0 && hid[3]==0x10);
    send_report(0,NULL,&p,JP_BUTTON_R1); switch_gc_task();
    assert(hid[12]==0 && hid[13]==255 && hid[2]==0x10);
    send_report(0,NULL,&p,JP_BUTTON_L1|JP_BUTTON_R1); switch_gc_task();
    assert(hid[12]==255 && hid[13]==255);
    send_report(0,NULL,&p,0); switch_gc_task();
    assert(hid[12]==0 && hid[13]==0 && hid[2]==0 && hid[3]==0);
    for (unsigned value=0; value<256; value++) {
        p.l2_analog=value; p.r2_analog=255-value; send_report(0,NULL,&p,0); switch_gc_task();
        assert(hid[12]==value && hid[13]==255-value);
        send_report(0,NULL,&p,JP_BUTTON_L1); switch_gc_task();
        assert(hid[12]==255 && hid[13]==255-value && hid[2]==0 && hid[3]==0x10);
        send_report(0,NULL,&p,JP_BUTTON_R1); switch_gc_task();
        assert(hid[12]==value && hid[13]==255 && hid[2]==0x10 && hid[3]==0);
        send_report(0,NULL,&p,JP_BUTTON_L1|JP_BUTTON_R1); switch_gc_task();
        assert(hid[12]==255 && hid[13]==255 && hid[2]==0x10 && hid[3]==0x10);
        send_report(0,NULL,&p,0); switch_gc_task();
        assert(hid[12]==value && hid[13]==255-value && hid[2]==0 && hid[3]==0);
        if (value) { assert(axis12(value)>=axis12(value-1)); assert(axis12_y(value)<=axis12_y(value-1)); }
    }
    assert(axis12(0)==0 && axis12(255)==4095 && axis12_y(0)==4095 && axis12_y(255)==0);
    command(3,0x0A,5); complete(); switch_gc_task(); assert(hid_id==5);
    command(3,0x0A,0xFF); complete(); switch_gc_task(); assert(hid_id==5);

    // Short memory and pairing requests never access absent fields or produce a reply.
    uint8_t short_cmd[8]={2,0x91,0,4};
    switch_gc_vendor_dispatch(short_cmd,8); assert(!bulk.reply_len);
    short_cmd[0]=0x15; switch_gc_vendor_dispatch(short_cmd,8); assert(!bulk.reply_len);
    command(0x02,0x04,255); // Safe capped read and erased-memory fill.
    assert(sent_len==56 && sent[8]==40 && sent[16]==0xFF); complete();
    // Startup-family response envelopes/lengths retained from the reference contract.
    const uint8_t cases[][3]={{3,3,12},{0x0C,1,20},{0x0C,6,48},{0x0C,2,12},
        {0x15,1,17},{0x15,3,9},{0x16,0,32},{7,0,9},{9,0,8},{0x10,0,20},
        {0x0B,3,12},{0x0B,4,12},{0x11,1,12},{0x11,3,37},{1,0,8},
        {0x18,1,16},{0x18,3,9},{0x0A,0,8}};
    for (unsigned i=0;i<sizeof(cases)/sizeof(cases[0]);i++) {
        command(cases[i][0],cases[i][1],0x27);
        assert(sent_len==cases[i][2] && sent[0]==cases[i][0] && sent[3]==cases[i][1]);
        assert(sent[1]==(cases[i][0]==1 ? 4 : 1) && sent[5]==0xF8);
        complete();
    }
    memset(bulk.rx,0,64); bulk.rx[0]=2; bulk.rx[1]=0x91; bulk.rx[3]=4;
    bulk.rx[8]=40; bulk.rx[12]=0x80; bulk.rx[13]=0x30; bulk.rx[14]=1;
    driver_xfer(0,2,XFER_RESULT_SUCCESS,16);
    assert(sent_len==56 && sent[16]==1 && sent[17]==0xAD); complete();

    // USB AES handshake uses actual challenge data, then resets its session key.
    uint8_t pairing[25]={0x15,0x91,0,4};
    for (int i=9;i<25;i++) pairing[i]=(uint8_t)i;
    memcpy(bulk.rx,pairing,25); driver_xfer(0,2,XFER_RESULT_SUCCESS,25);
    uint8_t key[16], expected[16];
    ns2_pairing_derive_ltk(pairing+9,sent+9,key); complete();
    pairing[3]=2; ns2_pairing_challenge(key,pairing+9,expected);
    memcpy(bulk.rx,pairing,25); driver_xfer(0,2,XFER_RESULT_SUCCESS,25);
    assert(sent_len==25 && memcmp(sent+9,expected,16)==0); complete();

    uint8_t rumble[]={0x03,0,0,1,0};
    handle_output(0,rumble,5); assert(get_rumble()==0xB0);
    now=39; switch_gc_task(); assert(get_rumble()==0xB0);
    now=40; switch_gc_task(); assert(get_rumble()==0);
    handle_output(3,rumble+1,4); assert(get_rumble()==0xB0);
    driver_reset(0); assert(!bulk.ep_in && get_rumble()==0);
    unsigned count=hid_count; switch_gc_task(); assert(hid_count==count);
    assert(driver_open(0,(const tusb_desc_interface_t *)(config+57),23)==23);
    uint8_t zero_key[16]={0}; ns2_pairing_challenge(zero_key,pairing+9,expected);
    memcpy(bulk.rx,pairing,25); driver_xfer(0,2,XFER_RESULT_SUCCESS,25);
    assert(memcmp(sent+9,expected,16)==0); complete();
    command(3,0x0A,0x0A); complete(); switch_gc_task();
    assert(hid_id==0x0A && hid[0]==0); // Counter and selected report reset together.
    assert(joypad_gc_string_descriptor(0xEE)[0]==0x0312);
    assert(joypad_gc_string_descriptor(6)==NULL);
    puts("Native USB transport, protocol, mapping, retry and reset checks passed");
    return 0;
}
