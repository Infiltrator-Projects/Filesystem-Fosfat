/* SPDX-License-Identifier: GPL-3.0-or-later
 * Native Smaky FOS reader. On-disk field definitions and FOSBOOT detection
 * derived from the retained Fosfat libfosfat/fosfat.c format reference:
 * Copyright (C) 2006-2010,2023,2025-2026 Mathieu Schroeter.
 * Thanks to Pierre Arnaud and Epsitec SA for the format documentation.
 * The new engine performs bounded Common V2 I/O only; it does not call
 * libfosfat, a host filesystem, an external tool or a private ABI.
 */
#include <infiltratr/filesystem_module_v2.h>
#include <stdint.h>
#include <stddef.h>
#include <stdatomic.h>
enum {B=256,MAX_NAME=16,SLOTS=8,MAX_BLOCKS=64*1024*1024,MAX_CHAIN=4096,MAX_ENTRIES=262144};
typedef struct {IfsFsHostRuntimeV2 host;uint32_t boot,blocks,variant,chk;char label[17];uintptr_t token;} Volume;
typedef struct {uint32_t id,type,bytes,flags,blocks;char rawname[17];} Node;
typedef struct {Node node;uint32_t length;char name[MAX_NAME+1];} Entry;
static Volume slots[SLOTS];static uintptr_t generation;
static atomic_flag gate=ATOMIC_FLAG_INIT;
static int enter(void){return !atomic_flag_test_and_set_explicit(&gate,memory_order_acquire);}
static int leave(int r){atomic_flag_clear_explicit(&gate,memory_order_release);return r;}
static void copy(void*d,const void*s,size_t n){uint8_t*a=d;const uint8_t*b=s;while(n--)*a++=*b++;}
static void zero(void*d,size_t n){uint8_t*a=d;while(n--)*a++=0;}
static int same(const void*a,const void*b,size_t n){const uint8_t*p=a,*q=b;while(n--)if(*p++!=*q++)return 0;return 1;}
static uint32_t be16(const uint8_t*p){return (uint32_t)p[0]<<8|p[1];}
static uint32_t be32(const uint8_t*p){return be16(p)<<16|be16(p+2);}
static unsigned lower(unsigned c){return c>='A'&&c<='Z'?c+32:c;}
static int eq(const char*a,const char*b,uint32_t n){while(n--)if(lower((uint8_t)*a++)!=lower((uint8_t)*b++))return 0;return 1;}
static int rd(const Volume*v,uint32_t b,uint8_t*p)
{
    if(b>=v->blocks)return IFS_FS_CORRUPT;
    if(v->host.cancelled&&v->host.cancelled(v->host.context))return IFS_FS_BUSY;
    return ifs_fs_host_read_bytes_v2(&v->host,(uint64_t)(v->boot+b)*B,p,B);
}
static void dispose(Volume*v){zero(v,sizeof(*v));}
static int name(const uint8_t*p,char*out,uint32_t*n)
{
    *n=0;
    while(*n<MAX_NAME&&p[*n]){
        unsigned c=p[*n];
        if(c<32||c>=127||c=='/'||c==92)return IFS_FS_UNSUPPORTED;
        out[*n]=(char)c;++*n;
    }
    out[*n]=0;return *n?IFS_FS_OK:IFS_FS_CORRUPT;
}
static int mark(uint8_t*seen,uint32_t b)
{
    unsigned bit=1u<<(b%8);
    if(seen[b/8]&bit)return 0;
    seen[b/8]|=(uint8_t)bit;return 1;
}
typedef int (*Extent)(const Volume*,uint32_t,uint32_t,uint32_t,void*);
/* Each description owns up to 42 contiguous runs. Validate the entire
 * chain, previous/next links, physical bounds, repeated data or metadata
 * blocks, and logical offsets before a read can report success. */
static int walk(const Volume*v,const Node*n,Extent cb,void*ctx,uint32_t*blocks,uint64_t*bytes)
{
    size_t cap=(v->blocks+7u)/8u;
    uint8_t*seen=v->host.allocate(v->host.context,cap);
    if(!seen)return IFS_FS_NO_MEMORY;
    zero(seen,cap);
    uint32_t id=n->id,prev=0,count=0,total=0;
    uint64_t length=0;int rc=IFS_FS_OK;
    while(id){
        if(id>=v->blocks||id<1||!mark(seen,id)){rc=IFS_FS_CORRUPT;break;}
        if(++count>MAX_CHAIN){rc=IFS_FS_UNSUPPORTED;break;}
        uint8_t p[B];rc=rd(v,id,p);if(rc)break;
        uint32_t next=be32(p),nr=be16(p+8),last=be16(p+240);
        if(be32(p+246)!=v->chk||be32(p+4)!=prev||nr>42||
           be32(p+250)!=total||last>B||(!nr&&(next||n->bytes))||
           (nr&&n->type==IFS_FS_ENTRY_FILE&&(!last||(next&&last!=B)))){
            rc=IFS_FS_CORRUPT;break;
        }
        if(count==1&&n->id!=1){
            char desc[17];uint32_t size;
            rc=name(p+178,desc,&size);if(rc)break;
            uint32_t actual=0;while(n->rawname[actual])++actual;
            if(actual!=size||!eq(desc,n->rawname,size)){rc=IFS_FS_CORRUPT;break;}
        }
        for(uint32_t i=0;i<nr;++i){
            uint32_t start=be32(p+10+4*i),run=p[194+i];
            if(start<2||start>=v->blocks||!run||run>v->blocks-start||
               run>UINT32_MAX-total){rc=IFS_FS_CORRUPT;break;}
            for(uint32_t j=0;j<run;++j)if(!mark(seen,start+j)){rc=IFS_FS_CORRUPT;break;}
            if(rc)break;
            uint32_t size=run*B;
            if(n->type==IFS_FS_ENTRY_FILE&&i+1==nr)size-=B-last;
            if(cb){rc=cb(v,start,run,size,ctx);if(rc)break;}
            total+=run;length+=size;
        }
        if(rc)break;
        prev=id;id=next;
    }
    v->host.release(v->host.context,seen);
    if(rc)return rc;
    if(n->type==IFS_FS_ENTRY_FILE&&(length!=n->bytes||total!=n->blocks))return IFS_FS_CORRUPT;
    *blocks=total;*bytes=length;return IFS_FS_OK;
}
typedef struct {const Node*dir;const char*name;uint32_t length,index,count,found,prev,next;Entry entry;} Search;
static int bl(const Volume*v,uint32_t start,uint32_t run,uint32_t size,void*ctx)
{
    (void)size;Search*s=ctx;
    for(uint32_t i=0;i<run;++i){
        uint32_t id=start+i;uint8_t p[B];int rc=rd(v,id,p);if(rc)return rc;
        if(be32(p+244)!=v->chk||be32(p+248)!=s->prev||
           (s->prev&&s->next!=id))return IFS_FS_CORRUPT;
        s->prev=id;s->next=be32(p+240);
        for(unsigned j=0;j<4;++j){
            const uint8_t*e=p+60*j;uint32_t flags=be32(e+18);
            if(!e[0]||!(flags&3)||(e[16]&0xf8))continue;
            Entry x={0};rc=name(e,x.node.rawname,&x.length);if(rc)return rc;
            copy(x.name,x.node.rawname,x.length+1);
            for(uint32_t k=0;k<x.length;++k)x.name[k]=(char)lower((uint8_t)x.name[k]);
            x.node.id=be32(e+50);x.node.bytes=be32(e+54);x.node.blocks=be32(e+22);x.node.flags=flags;
            x.node.type=flags&0x1000?IFS_FS_ENTRY_DIRECTORY:flags&0x1000000?IFS_FS_ENTRY_LINK:IFS_FS_ENTRY_FILE;
            if(!x.node.id||x.node.id>=v->blocks||x.node.id==s->dir->id)return IFS_FS_CORRUPT;
            if(x.node.type==IFS_FS_ENTRY_DIRECTORY&&x.length>4&&eq(x.name+x.length-4,".dir",4)){
                x.length-=4;x.name[x.length]=0;
            }
            if((x.length==1&&x.name[0]=='.')||(x.length==2&&same(x.name,"..",2)))return IFS_FS_CORRUPT;
            if(s->name?(x.length==s->length&&eq(x.name,s->name,x.length)):s->index==s->count){
                if(s->found++)return IFS_FS_CORRUPT;
                s->entry=x;
            }
            if(++s->count>MAX_ENTRIES)return IFS_FS_UNSUPPORTED;
        }
    }
    return IFS_FS_OK;
}
static int child(const Volume*v,const Node*dir,const char*name_,uint32_t len,uint32_t index,Entry*out)
{
    if(dir->type!=IFS_FS_ENTRY_DIRECTORY)return IFS_FS_NOT_FOUND;
    if(dir->flags&0x20000)return IFS_FS_UNSUPPORTED;
    Search s={.dir=dir,.name=name_,.length=len,.index=index};
    uint32_t blocks;uint64_t bytes;int rc=walk(v,dir,bl,&s,&blocks,&bytes);
    if(rc)return rc;
    if(s.next)return IFS_FS_CORRUPT;
    if(!s.found)return name_?IFS_FS_NOT_FOUND:IFS_FS_END;
    /* The native link record embeds a platform-specific device prefix.
     * Expose its type but do not manufacture a portable link target. */
    *out=s.entry;return IFS_FS_OK;
}
typedef struct {uint64_t logical,at,end,covered;uint8_t*out;} Read;
static int data(const Volume*v,uint32_t start,uint32_t run,uint32_t size,void*ctx)
{
    (void)run;Read*r=ctx;
    uint64_t begin=r->logical,end=begin+size;
    uint64_t lo=begin>r->at?begin:r->at,hi=end<r->end?end:r->end;
    if(lo<hi){
        if(lo!=r->covered)return IFS_FS_CORRUPT;
        int rc=ifs_fs_host_read_bytes_v2(&v->host,(uint64_t)(v->boot+start)*B+lo-begin,
               r->out+(size_t)(lo-r->at),(uint32_t)(hi-lo));if(rc)return rc;
        r->covered=hi;
    }
    r->logical=end;return IFS_FS_OK;
}
static int file_read(const Volume*v,const Node*n,uint64_t at,void*out,uint32_t len)
{
    if((!out&&len)||at>n->bytes||len>n->bytes-at)return IFS_FS_INVALID;
    if(n->type!=IFS_FS_ENTRY_FILE||(n->flags&0x20000))return IFS_FS_UNSUPPORTED;
    Read r={.at=at,.end=at+len,.covered=at,.out=out};
    uint32_t blocks;uint64_t bytes;int rc=walk(v,n,data,&r,&blocks,&bytes);
    return rc?rc:r.covered==r.end?IFS_FS_OK:IFS_FS_CORRUPT;
}
static int root(const Volume*v,Node*n)
{
    (void)v;*n=(Node){.id=1,.type=IFS_FS_ENTRY_DIRECTORY};return IFS_FS_OK;
}
static int candidate(const Volume*host,uint32_t boot,Volume*out)
{
    Volume v=*host;v.boot=boot;v.variant=boot==16?1:2;
    uint64_t total=v.host.volume_bytes/B;
    if(total<=boot+2)return IFS_FS_NOT_FOUND;
    if(total-boot>MAX_BLOCKS)return IFS_FS_UNSUPPORTED;
    v.blocks=(uint32_t)(total-boot);
    uint8_t p[B],q[B],z[B];int rc=rd(&v,1,p);if(rc)return rc;
    /* FOS has no universal magic. Require the SYS_LIST description
     * name, its fixed first list block, and matching nonzero check ID. */
    if(be32(p+10)!=2||!be16(p+8)||be16(p+8)>42||!eq((const char*)p+178,"sys_list",8)||p[186])
        return IFS_FS_NOT_FOUND;
    v.chk=be32(p+246);if(!v.chk)return IFS_FS_CORRUPT;
    rc=rd(&v,2,q);if(rc)return rc;
    rc=rd(&v,0,z);if(rc)return rc;
    if(be32(q+244)!=v.chk||be32(z+60)!=v.chk||z[236])return IFS_FS_CORRUPT;
    if(z[44]!=255){
        for(unsigned i=0;i<16&&z[44+i];++i){
            if(z[44+i]<32||z[44+i]>=127)return IFS_FS_UNSUPPORTED;
            v.label[i]=(char)z[44+i];
        }
    }
    Node n;root(&v,&n);Entry e;rc=child(&v,&n,NULL,0,UINT32_MAX,&e);
    if(rc!=IFS_FS_END)return rc?rc:IFS_FS_CORRUPT;
    *out=v;return IFS_FS_OK;
}
static int discover(const IfsFsHostRuntimeV2*h,Volume*out)
{
    if(!ifs_fs_host_v2_valid(h))return IFS_FS_INVALID;
    Volume host={0},a,b;
    size_t n=h->structure_bytes;if(n>sizeof host.host)n=sizeof host.host;
    copy(&host.host,h,n);host.host.structure_bytes=(uint32_t)n;
    int x=candidate(&host,16,&a),y=candidate(&host,32,&b);
    if(!x&&!y)return IFS_FS_CORRUPT;
    if(x==IFS_FS_IO_ERROR||y==IFS_FS_IO_ERROR)return IFS_FS_IO_ERROR;
    if(x==IFS_FS_NO_MEMORY||y==IFS_FS_NO_MEMORY)return IFS_FS_NO_MEMORY;
    if(x==IFS_FS_BUSY||y==IFS_FS_BUSY)return IFS_FS_BUSY;
    if(!x){*out=a;return IFS_FS_OK;}if(!y){*out=b;return IFS_FS_OK;}
    return x!=IFS_FS_NOT_FOUND?x:y;
}
static Volume *mounted(void *p)
{if(p)for(unsigned i=0;i<SLOTS;++i)if(slots[i].token==(uintptr_t)p)return &slots[i];return NULL;}
static void info(const Volume *v,IfsFsVolumeInfoV2 *o)
{*o=(IfsFsVolumeInfoV2){.structure_bytes=sizeof(*o),.variant=v->variant,.max_name_bytes=MAX_NAME,
.block_bytes=256,.total_bytes=(uint64_t)v->blocks*256,.free_bytes=0};}
static int probe_impl(const IfsFsHostRuntimeV2 *h,uint32_t *variant)
{if(!variant)return IFS_FS_INVALID;Volume v={0};int rc=discover(h,&v);if(!rc)*variant=v.variant;dispose(&v);return rc;}
static int mount_impl(const IfsFsHostRuntimeV2 *h,void **out,IfsFsVolumeInfoV2 *o)
{
    if(!out||!o||o->structure_bytes<sizeof(*o))return IFS_FS_INVALID;
    *out=NULL;if(!ifs_fs_host_v2_valid(h))return IFS_FS_INVALID;
    if(!(h->capabilities&IFS_FS_HOST_READ_ONLY))return IFS_FS_ACCESS;
    unsigned i=0;while(i<SLOTS&&slots[i].token)++i;
    if(i==SLOTS||generation==UINTPTR_MAX)return IFS_FS_BUSY;
    int rc=discover(h,&slots[i]);if(rc)return rc;
    slots[i].token=++generation;*out=(void *)slots[i].token;info(&slots[i],o);return IFS_FS_OK;
}
static int unmount_impl(void *p)
{Volume *v=mounted(p);if(!v)return IFS_FS_INVALID;dispose(v);return IFS_FS_OK;}
static int resolve(const Volume *v,const char *path,Entry *out)
{
    if(!path||*path!='/')return IFS_FS_INVALID;
    Entry e={.length=1,.name="/"};int rc=root(v,&e.node);if(rc)return rc;
    ++path;
    for(unsigned depth=0;*path;++depth){
        if(depth>=256)return IFS_FS_INVALID;
        uint32_t n=0;while(n<=MAX_NAME&&path[n]&&path[n]!='/')++n;
        if(!n||n>MAX_NAME||(n==1&&path[0]=='.')||(n==2&&same(path,"..",2)))return IFS_FS_INVALID;
        Node dir=e.node;rc=child(v,&dir,path,n,0,&e);if(rc)return rc;
        path+=n;if(*path=='/'){++path;if(!*path)return IFS_FS_INVALID;}
    }
    *out=e;return IFS_FS_OK;
}
static int emit(const Entry *e,IfsFsEntryV2 *o)
{
    if(!o||o->structure_bytes<sizeof(*o)||!o->name)return IFS_FS_INVALID;
    if(o->name_capacity<=e->length)return IFS_FS_NO_SPACE;
    char *name=o->name;uint32_t cap=o->name_capacity;
    *o=(IfsFsEntryV2){.structure_bytes=sizeof(*o),.type=e->node.type,.size_bytes=e->node.bytes,
      .name=name,.name_capacity=cap,.name_bytes=e->length};
    copy(name,e->name,e->length+1);return IFS_FS_OK;
}
static int stat_impl(void *p,const char *path,IfsFsEntryV2 *o)
{Volume *v=mounted(p);if(!v)return IFS_FS_INVALID;Entry e;int rc=resolve(v,path,&e);return rc?rc:emit(&e,o);}
static int list_impl(void *p,const char *path,uint32_t i,IfsFsEntryV2 *o)
{Volume *v=mounted(p);if(!v)return IFS_FS_INVALID;Entry dir,e;int rc=resolve(v,path,&dir);if(!rc)rc=child(v,&dir.node,NULL,0,i,&e);return rc?rc:emit(&e,o);}
static int read_impl(void *p,const char *path,uint64_t at,void *out,uint32_t n)
{Volume *v=mounted(p);if(!v)return IFS_FS_INVALID;Entry e;int rc=resolve(v,path,&e);if(rc)return rc;return e.node.type==IFS_FS_ENTRY_FILE?file_read(v,&e.node,at,out,n):IFS_FS_UNSUPPORTED;}

static int label_impl(void*p,char*out,uint32_t cap,uint32_t*n)
{
    if(!n)return IFS_FS_INVALID;
    *n=0;Volume*v=mounted(p);if(!v||!out)return IFS_FS_INVALID;
    uint32_t len=0;while(v->label[len])++len;
    if(cap<=len)return IFS_FS_NO_SPACE;
    copy(out,v->label,len+1);*n=len;return IFS_FS_OK;
}
#define WRAP(name,args,call) static int name args{if(!enter())return IFS_FS_BUSY;return leave(call);}
WRAP(probe,(const IfsFsHostRuntimeV2*h,uint32_t*v),probe_impl(h,v))
WRAP(mount,(const IfsFsHostRuntimeV2*h,void**p,IfsFsVolumeInfoV2*o),mount_impl(h,p,o))
WRAP(unmount,(void*p),unmount_impl(p))
WRAP(stat_path,(void*p,const char*s,IfsFsEntryV2*o),stat_impl(p,s,o))
WRAP(list_path,(void*p,const char*s,uint32_t i,IfsFsEntryV2*o),list_impl(p,s,i,o))
WRAP(read_path,(void*p,const char*s,uint64_t a,void*b,uint32_t n),read_impl(p,s,a,b,n))
WRAP(label_path,(void*p,char*s,uint32_t c,uint32_t*n),label_impl(p,s,c,n))
static const IfsFsCoreOpsV2 core={.header={2,sizeof(core),IFS_FS_GROUP_CORE,0,IFS_FS_CORE_PROBE|IFS_FS_CORE_MOUNT|IFS_FS_CORE_STAT|IFS_FS_CORE_LIST|IFS_FS_CORE_READ},.probe=probe,.mount=mount,.unmount=unmount,.stat=stat_path,.directory_entry=list_path,.read=read_path};
/* The format's free-space map is not decoded. Mount reports no proven
 * writable free bytes, and QUERY/ALLOCATION are deliberately absent. */
static const IfsFsVolumeOpsV2 volume={.header={2,sizeof(volume),IFS_FS_GROUP_VOLUME,0,IFS_FS_VOLUME_LABEL_GET},.label=label_path};
static const IfsFsGroupHeaderV2*group(uint32_t id,uint32_t lo,uint32_t hi)
{if(lo>2||hi<2)return NULL;if(id==IFS_FS_GROUP_CORE)return &core.header;if(id==IFS_FS_GROUP_VOLUME)return &volume.header;return NULL;}
static const IfsFsModuleApiV2 api={.abi_version=2,.structure_bytes=sizeof(api),.format_id="fosfat",
#ifdef IFS_BOOT_FIRMWARE
.runtime_profiles=IFS_FS_PROFILE_FREESTANDING|IFS_FS_PROFILE_FIRMWARE,
#else
.runtime_profiles=IFS_FS_PROFILE_HOSTED,
#endif
.group_mask=IFS_FS_GROUP_BIT(IFS_FS_GROUP_CORE)|IFS_FS_GROUP_BIT(IFS_FS_GROUP_VOLUME),.query_group=group};
__attribute__((visibility("default")))const IfsFsModuleApiV2*infiltrator_filesystem_api_v2(void){return &api;}
