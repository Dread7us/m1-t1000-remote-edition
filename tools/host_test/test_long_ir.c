/* See COPYING.txt for license details. */
#include "flipper_ir.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
int main(void){
    flipper_file_t file;flipper_ir_signal_t in={0},out={0};
    strcpy(in.name,"Power");in.type=FLIPPER_IR_SIGNAL_RAW;in.valid=true;in.raw.frequency=38000;in.raw.duty_cycle=0.33f;in.raw.sample_count=512;
    for(unsigned i=0;i<512;++i)in.raw.samples[i]=12345+i;
    assert(ff_open_write(&file,"full.ir"));assert(flipper_ir_write_header(&file));assert(flipper_ir_write_signal(&file,&in));ff_close(&file);
    assert(flipper_ir_open(&file,"full.ir"));assert(flipper_ir_read_signal(&file,&out));assert(out.raw.sample_count==512&&!memcmp(in.raw.samples,out.raw.samples,sizeof(in.raw.samples)));ff_close(&file);
    for(unsigned shift=0;shift<16;++shift){
        FILE *f=fopen("split.ir","wb");assert(f);fputs("Filetype: IR signals file\nVersion: 1\n#\nname: Power\ntype: raw\nfrequency: 38000\ndata:",f);
        for(unsigned i=0;i<shift;++i)fputc(' ',f);
        for(unsigned i=0;i<512;++i)fprintf(f," %d",in.raw.samples[i]);
        fputs("\nname: Next\ntype: parsed\nprotocol: NEC\naddress: 07 00 00 00\ncommand: 02 00 00 00\n",f);fclose(f);
        assert(flipper_ir_open(&file,"split.ir"));assert(flipper_ir_read_signal(&file,&out));assert(out.raw.sample_count==512&&!memcmp(in.raw.samples,out.raw.samples,sizeof(in.raw.samples)));
        assert(flipper_ir_read_signal(&file,&out)&&!strcmp(out.name,"Next"));ff_close(&file);
    }
    FILE *f=fopen("wrapped.ir","wb");assert(f);fputs("Filetype: IR signals file\nVersion: 1\n#\nname: Power\ntype: raw\nfrequency: 38000\n",f);
    for(unsigned chunk=0;chunk<2;++chunk){fputs("data:",f);for(unsigned i=0;i<256;++i)fprintf(f," %d",in.raw.samples[chunk*256+i]);fputc('\n',f);}fclose(f);
    assert(flipper_ir_open(&file,"wrapped.ir"));assert(flipper_ir_read_signal(&file,&out));assert(out.raw.sample_count==512&&!memcmp(in.raw.samples,out.raw.samples,sizeof(in.raw.samples)));ff_close(&file);
    f=fopen("oversize.ir","wb");assert(f);fputs("Filetype: IR signals file\nVersion: 1\n#\nname: Power\ntype: raw\nfrequency: 38000\ndata:",f);for(unsigned i=0;i<513;++i)fputs(" 100",f);fputc('\n',f);fclose(f);
    assert(flipper_ir_open(&file,"oversize.ir"));assert(!flipper_ir_read_signal(&file,&out));ff_close(&file);
    puts("PASS: full 512-sample raw IR roundtrip, split-number/space boundaries at 16 alignments, multiple data lines, following button without separator, and oversized raw rejection");return 0;
}
