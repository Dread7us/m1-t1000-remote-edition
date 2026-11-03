/* See COPYING.txt for license details. */
#include <assert.h>
#include <stdio.h>
#include "m1_ir_remote_view.h"
int main(void)
{
    assert(ir_remote_move(0,0,0,1)==0);
    for (uint16_t count=1;count<=64;count++) {
        for (uint16_t i=0;i<count;i++) {
            assert(ir_remote_move(i,count,1,0)<count);
            assert(ir_remote_move(i,count,-1,0)<count);
            assert(ir_remote_move(i,count,0,1)<count);
            assert(ir_remote_move(i,count,0,-1)<count);
        }
        /* Traversing rows and columns can reach every button, including the
           final unpaired button and every page after button sixteen. */
        unsigned char seen[64]={0}; uint16_t at=0;
        for (uint16_t j=0;j<count*2;j++) {
            seen[at]=1;
            uint16_t neighbor=ir_remote_move(at,count,1,0); seen[neighbor]=1;
            at=ir_remote_move(at,count,0,1);
        }
        for (uint16_t i=0;i<count;i++) assert(seen[i]);
    }
    assert(ir_remote_move(15,64,0,1)==17);
    assert(ir_remote_move(62,64,1,0)==63);
    assert(ir_remote_move(63,64,0,1)==1);
    assert(ir_remote_icon("Vol_up")==IR_ICON_PLUS);
    assert(ir_remote_icon("V+")==IR_ICON_PLUS);
    assert(ir_remote_icon("Volume Down")==IR_ICON_MINUS);
    assert(ir_remote_icon("Ch_prev")==IR_ICON_DOWN);
    assert(ir_remote_icon("Power_off")==IR_ICON_POWER);
    assert(ir_remote_icon("Input")==IR_ICON_TEXT);
    puts("PASS: all remote navigation states for 0..64 buttons, odd final rows, wraparound, all pages, icon aliases");
    return 0;
}
