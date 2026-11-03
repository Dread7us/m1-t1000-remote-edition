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
    const char *mixed[]={"Ch_down","Power","Ch_up","Vol_down","Mute","Vol_up","Input"};
    uint16_t slots[IR_REMOTE_GRID_MAX];
    uint16_t n=ir_remote_grid_build(mixed,7,slots,IR_REMOTE_GRID_MAX);
    const uint16_t expected[]={1,4,5,2,3,0,6,IR_REMOTE_EMPTY};
    assert(n==8 && !memcmp(slots,expected,sizeof(expected)));
    const char *missing[]={"Vol_up","Power","Ch_down","Source","Vol_down","Ch_up"};
    n=ir_remote_grid_build(missing,6,slots,IR_REMOTE_GRID_MAX);
    assert(n==6 && slots[0]==1 && slots[1]==3 && slots[2]==0 && slots[3]==5 && slots[4]==4 && slots[5]==2);
    const char *sparse[]={"Vol_up","Power","Ch_down"};
    n=ir_remote_grid_build(sparse,3,slots,IR_REMOTE_GRID_MAX);
    assert(slots[0]==1 && slots[1]==IR_REMOTE_EMPTY && slots[2]==0 && slots[5]==2);
    assert(ir_remote_grid_move(0,slots,n,1,0)==2);
    assert(ir_remote_grid_move(2,slots,n,0,1)==0);
    char storage[64][32]; const char *names[64];
    for(unsigned i=0;i<64;i++) { snprintf(storage[i],32,"Button%u",i); names[i]=storage[i]; }
    for(uint16_t count=1;count<=64;count++) {
        n=ir_remote_grid_build(names,count,slots,IR_REMOTE_GRID_MAX);
        assert(n>=count && n<=IR_REMOTE_GRID_MAX && !(n&1));
        unsigned seen[64]={0};
        for(uint16_t j=0;j<n;j++) if(slots[j]!=IR_REMOTE_EMPTY) {
            assert(slots[j]<count);seen[slots[j]]++;
            for(int direction=-1;direction<=1;direction+=2) {
                assert(slots[ir_remote_grid_move(j,slots,n,direction,0)]!=IR_REMOTE_EMPTY);
                assert(slots[ir_remote_grid_move(j,slots,n,0,direction)]!=IR_REMOTE_EMPTY);
            }
        }
        for(uint16_t i=0;i<count;i++) assert(seen[i]==1);
    }
    puts("PASS: semantic layout, shuffled file order, missing Mute/Input fallback, vacant slots, stable signal indexes, all 64 commands");
    puts("PASS: all remote navigation states for 0..64 buttons, odd final rows, wraparound, all pages, icon aliases");
    return 0;
}
