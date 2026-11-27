/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <infiltratr/filesystem_module_v2.h>
#include <assert.h>
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
enum{B=256,N=256,MAX=B*N};
static uint8_t disk[MAX],saved[MAX];static unsigned boot,sector,allocs,fail;
static uint8_t*blk(unsigned b){return disk+(boot+b)*B;}
static void p16(uint8_t*p,uint32_t n){p[0]=n>>8;p[1]=n;}
static void p32(uint8_t*p,uint32_t n){p16(p,n>>16);p16(p+2,n);}
static void bd(unsigned id,const char*name,unsigned next,unsigned prev,unsigned off,unsigned start,unsigned len,unsigned last)
{
    uint8_t*p=blk(id);p32(p,next);p32(p+4,prev);p16(p+8,1);p32(p+10,start);
    memcpy(p+178,name,strlen(name));p[194]=len;p16(p+240,last);p32(p+246,0x12345678);p32(p+250,off);
}
static void entry(uint8_t*p,const char*name,unsigned flags,unsigned id,unsigned blocks,unsigned bytes)
{memcpy(p,name,strlen(name));p32(p+18,flags|1);p32(p+22,blocks);p32(p+50,id);p32(p+54,bytes);}
static void fixture(void)
{
    memset(disk,0,sizeof disk);p32(blk(0)+60,0x12345678);memcpy(blk(0)+44,"SMAKY",5);
    bd(1,"SYS_LIST",0,0,0,2,2,B);p32(blk(2)+244,0x12345678);p32(blk(2)+240,3);
    p32(blk(3)+244,0x12345678);p32(blk(3)+248,2);
    entry(blk(2),"PROOF.BIN",0,4,3,2*B+17);entry(blk(2)+60,"DIR.DIR",0x1000,6,1,B);
    entry(blk(2)+120,"LINK",0x1000000,8,1,4);entry(blk(2)+180,"ENCODED",0x20000,9,1,1);
    entry(blk(3),"SYS_LIST",0x1000,1,2,2*B);blk(3)[16]=0xf8;
    bd(4,"PROOF.BIN",5,0,0,20,2,B);bd(5,"PROOF.BIN",0,4,2,30,1,17);
    bd(6,"DIR.DIR",0,0,0,7,1,B);p32(blk(7)+244,0x12345678);
    entry(blk(7),"CHILD",0,10,1,3);bd(10,"CHILD",0,0,0,31,1,3);
    memcpy(blk(31),"yes",3);
    for(unsigned i=0;i<2*B;++i)blk(20)[i]=(uint8_t)(i*13+7);
    for(unsigned i=0;i<17;++i)blk(30)[i]=(uint8_t)(i*7+1);
    memcpy(saved,disk,sizeof disk);
}
static int rd(void*c,uint64_t off,void*out,uint32_t n)
{(void)c;if(fail||off>sizeof disk||n>sizeof disk-off||off%sector||n%sector)return -1;memcpy(out,disk+off,n);return 0;}
static void*alloc(void*c,size_t n){(void)c;void*p=malloc(n);if(p)++allocs;return p;}
static void drop(void*c,void*p){(void)c;if(p){assert(allocs);--allocs;free(p);}}
int main(int argc,char**argv)
{
    if(argc==4&&!strcmp(argv[1],"--emit")){
        boot=16;fixture();FILE*f=fopen(argv[2],"wb"),*p=fopen(argv[3],"wb");assert(f&&p);
        assert(fwrite(disk,1,sizeof disk,f)==sizeof disk);assert(fwrite(blk(20),1,2*B,p)==2*B);
        assert(fwrite(blk(30),1,17,p)==17);assert(!fclose(f)&&!fclose(p));return 0;
    }
    assert(argc==2);void*lib=dlopen(argv[1],RTLD_NOW|RTLD_LOCAL);if(!lib){puts(dlerror());return 1;}
    IfsFsModuleApiEntryV2 entry_;*(void**)(&entry_)=dlsym(lib,IFS_FS_COMMON_EXPORT_V2);assert(entry_);
    const IfsFsModuleApiV2*api=entry_();assert(ifs_fs_api_v2_valid(api,"fosfat"));
    const IfsFsCoreOpsV2*core=(const void*)ifs_fs_group_v2(api,IFS_FS_GROUP_CORE);
    const IfsFsVolumeOpsV2*vol=(const void*)ifs_fs_group_v2(api,IFS_FS_GROUP_VOLUME);assert(core&&vol&&!core->write&&!vol->query);
    for(boot=16;boot<=32;boot*=2)for(sector=512;sector<=4096;sector*=8){
        fixture();IfsFsHostRuntimeV2 h={.abi_version=2,.structure_bytes=sizeof h,.capabilities=IFS_FS_HOST_READ_ONLY,
          .volume_bytes=sizeof disk,.logical_sector_bytes=sector,.physical_sector_bytes=sector,.maximum_transfer_bytes=4096,
          .read_at=rd,.allocate=alloc,.release=drop};
        uint32_t variant=0;assert(core->probe(&h,&variant)==0&&variant==(boot==16?1u:2u)&&!allocs);
        void*v=NULL;IfsFsVolumeInfoV2 info={.structure_bytes=sizeof info};assert(core->mount(&h,&v,&info)==0&&v);
        char name[64];IfsFsEntryV2 e={.structure_bytes=sizeof e,.name=name,.name_capacity=sizeof name};
        assert(core->directory_entry(v,"/",0,&e)==0&&!strcmp(name,"proof.bin"));
        assert(core->directory_entry(v,"/",1,&e)==0&&!strcmp(name,"dir"));
        assert(core->stat(v,"/link",&e)==0&&e.type==IFS_FS_ENTRY_LINK);
        assert(core->stat(v,"/encoded",&e)==IFS_FS_UNSUPPORTED);
        assert(core->stat(v,"/PROOF.BIN",&e)==0&&e.size_bytes==2*B+17);
        uint8_t out[2*B+17];assert(core->read(v,"/proof.bin",0,out,sizeof out)==0&&!memcmp(out,blk(20),2*B)&&!memcmp(out+2*B,blk(30),17));
        assert(core->read(v,"/dir/child",0,out,3)==0&&!memcmp(out,"yes",3));
        uint32_t n;assert(vol->label(v,name,sizeof name,&n)==0&&n==5&&!strcmp(name,"SMAKY"));
        p32(blk(5)+10,20);assert(core->read(v,"/proof.bin",0,out,1)==IFS_FS_CORRUPT);memcpy(disk,saved,sizeof disk);
        p32(blk(5),4);assert(core->read(v,"/proof.bin",0,out,1)==IFS_FS_CORRUPT);memcpy(disk,saved,sizeof disk);
        p32(blk(3)+244,0);assert(core->stat(v,"/proof.bin",&e)==IFS_FS_CORRUPT);memcpy(disk,saved,sizeof disk);
        fail=1;assert(core->stat(v,"/proof.bin",&e)!=0);fail=0;
        assert(core->unmount(v)==0&&core->unmount(v)==IFS_FS_INVALID&&!allocs);
        assert(!memcmp(disk,saved,sizeof disk));
    }
    puts("FOS floppy and hard disk, linked descriptions, fragmented reads, check ID and cycle rejection PASS");dlclose(lib);return 0;
}
