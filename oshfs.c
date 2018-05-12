#define FUSE_USE_VERSION 26
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <fuse.h>
#include <sys/mman.h>
#define MAX_NAME 255
#define BLOCKNR (65536)
#define BLOCKSIZE 65536
#define head 40 //每一块头部的空间

typedef struct filenode {
    char *filename;
    void *content;
    int head_page;
    int head_size;
    struct stat *st;
    struct filenode *next;
    struct filenode *last;
}node;

static const size_t size = 4 * 1024 * 1024 * (size_t)1024;
static void *mem[BLOCKNR];
static int mem_offset[BLOCKNR]={};
size_t blocknr = BLOCKNR;
size_t blocksize = BLOCKSIZE;
int pagenum=0;

//static struct filenode *root = NULL;

int min(int a,int b){
    return a<b?a:b;
}

int find_avail_block(){
    int i=pagenum;
    for (i=(pagenum+1)%blocknr;i!=pagenum;i=(i+1)%blocknr){
        if (!mem[i]){
            pagenum=i;
            return i;
        }
    }
    return -1;
}

int get_offset(int block_num){
    if (!mem[block_num]) return -1;
    return *(int*)mem[block_num];
}

int get_next_block(int block_num){ //获取页链表中lock_num页的下一页
    if (!mem[block_num]) return -1;
    return *((int*)mem[block_num]+1);
}



static int set_page(int block_num){

    if (mem[block_num]){
        return -1;
    }
    mem[block_num]=mmap(NULL, blocksize, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    memset(mem[block_num], 0, blocksize);
    *(int*)mem[block_num]=head;
    return 0;
}

void set_next_block(int cur_page){
    int next = find_avail_block();
    set_page(next);
    *((int*)mem[cur_page]+1)=next;
    *(int*)mem[cur_page]=blocksize;
}

static void *get_mem(int block_num,int size){
    if (mem[block_num]==NULL){
        return NULL;
    }
    int offset=*(int*)mem[block_num];
    if (size+offset>blocksize) size=blocksize-offset;
    *(int*)mem[block_num]+=size;
    return mem[block_num]+offset;
}

void set_root(node *root){
    node **toroot;
    toroot=(node**)(mem[0]+head);
    *toroot=root;
}

node* get_root(){
    return *(node**)(mem[0]+head);
}

static struct filenode *get_filenode(const char *name)
{
    node *node = get_root();
    while(node) {
        if(strcmp(node->filename, name + 1) != 0)
            node = node->next;
        else
            return node;
    }
    return NULL;
}

static void create_filenode(const char *filename, const struct stat *st)
{
    /*这个是创建一个新的文件节点的函数

    find avail函数是找到一个空的页
    setpage(i)是申请i号页的空间并且设置i号页的一些基础信息

    getmem(i,size)是得到i号页里指定的大小的空间，返回这段空间开头的指针
    然后刚开始的话就是获得一个node结构体大小的空间用来装描述这个文件的各种指针

    然后继续在这个页里获取空间装这个文件的信息
    getoffset(i)是获得i号页使用了多少空间

    最后把文件节点插入到链表里，并且node->head记录下文件信息占据到多少空间
    这样一个空文件就算创建完了*/

    int i;
    i=find_avail_block();
    set_page(i);
    node *root = get_root();
    node *new = (node *)get_mem(i,sizeof(node));
    new->filename = (char *)get_mem(i,strlen(filename) + 1);
    memcpy(new->filename, filename, strlen(filename) + 1);
    new->st = (struct stat *)get_mem(i,sizeof(struct stat));
    memcpy(new->st, st, sizeof(struct stat));
    new->next = root;
    /**/
    new->last=NULL;
    if (root) root->last=new;

    new->head_size = get_offset(i);
    new->content = mem[i]+get_offset(i);
    new->head_page=i;
    root = new;
    set_root(root);
}


/*以上为自定义函数*/


static void *oshfs_init(struct fuse_conn_info *conn)
{
    /*
    // Demo 1
    for(int i = 0; i < blocknr; i++) {
        mem[i] = mmap(NULL, blocksize, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        memset(mem[i], 0, blocksize);
    }
    for(int i = 0; i < blocknr; i++) {
        munmap(mem[i], blocksize);
    }
    // Demo 2
    mem[0] = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    for(int i = 0; i < blocknr; i++) {
        mem[i] = (char *)mem[0] + blocksize * i;
        memset(mem[i], 0, blocksize);
    }
    for(int i = 0; i < blocknr; i++) {
        munmap(mem[i], blocksize);
    }
    */
    set_page(0);
    node **toroot=(node**)get_mem(0,sizeof(node*));
    *toroot=NULL;
    return NULL;
}

static int oshfs_getattr(const char *path, struct stat *stbuf)
{
    int ret = 0;
    struct filenode *node = get_filenode(path);
    if(strcmp(path, "/") == 0) {
        memset(stbuf, 0, sizeof(struct stat));
        stbuf->st_mode = S_IFDIR | 0755;
    } else if(node) {
        memcpy(stbuf, node->st, sizeof(struct stat));
    } else {
        ret = -ENOENT;
    }
    return ret;
}

static int oshfs_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi)
{
    struct filenode *node = get_root();
    filler(buf, ".", NULL, 0);
    filler(buf, "..", NULL, 0);
    while(node) {
        filler(buf, node->filename, node->st, 0);
        node = node->next;
    }

    return 0;
}

static int oshfs_mknod(const char *path, mode_t mode, dev_t dev)
{
    struct stat st;
    st.st_mode = S_IFREG | 0644;
    st.st_uid = fuse_get_context()->uid;
    st.st_gid = fuse_get_context()->gid;
    st.st_nlink = 1;
    st.st_size = 0;
    create_filenode(path + 1, &st);
    return 0;
}

static int oshfs_open(const char *path, struct fuse_file_info *fi)
{
    return 0;
}

static int oshfs_write(const char *path, const char *buf, size_t size, off_t offset, struct fuse_file_info *fi)
{
    node *node = get_filenode(path);
    if(offset + size > node->st->st_size)
        node->st->st_size = offset + size;
    int page_offset,complete=0,space=0;
    int cur_page=node->head_page,i;
    space=blocksize - node->head_size;
    while (space<offset){
        if (get_next_block(cur_page)==0){
            set_next_block(cur_page);
        }
        space+=blocksize-head;
        cur_page=get_next_block(cur_page);
    }
    memcpy(mem[cur_page]+blocksize-(space-offset),buf,min(size,space-offset));
    complete+=space-offset;
    while(complete<size){
        if (get_next_block(cur_page)==0){
            set_next_block(cur_page);
        }
        cur_page=get_next_block(cur_page);
        memcpy(mem[cur_page]+head,buf+complete,min(size-complete,blocksize-head));
        complete+=blocksize-head;
    }
    *(int*)mem[cur_page]=blocksize-(complete-size);
    return size;
}

static int oshfs_truncate(const char *path, off_t size)
{
    node *node = get_filenode(path);
    node->st->st_size = size;
    int space=blocksize-node->head_size,cur_page=node->head_page;
    while (space<size){
        if (get_next_block(cur_page)==0){
            set_next_block(cur_page);
        }
        cur_page=get_next_block(cur_page);
        space+=blocksize-head;
    }
    *(int*)mem[cur_page]=blocksize-(space-size);

    /**/
    int next;
    *((int*)mem[cur_page]+1)=0;
    cur_page=get_next_block(cur_page);
    if (cur_page!=0){
        next=get_next_block(cur_page);
        munmap(mem[cur_page],blocksize);
        mem[cur_page]=NULL;
        cur_page=next;
    }

    return 0;
}

static int oshfs_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi)
{
    node *node = get_filenode(path);
    int ret = size;
    int space=blocksize-node->head_size,cur_page=node->head_page;


    if(offset + size > node->st->st_size)
        ret = node->st->st_size - offset;
   // memcpy(buf, node->content + offset, ret);
/**/
    while (space<offset){
        cur_page = get_next_block(cur_page);
        space+=blocksize-head;
    }
    memcpy(buf, mem[cur_page]+blocksize-(space-offset), min(ret,space-offset));

    int complete=space-offset;
    while (complete<ret){
        cur_page=get_next_block(cur_page);
        if (cur_page==0) break;
        memcpy(buf+complete,mem[cur_page]+head,min(ret-complete,blocksize-head));
        complete+=blocksize-head;
    }
    /**/
    return ret;
}


static int oshfs_unlink(const char *path)
{
    // Not Implemented
    node *root = get_root();
    node *node = get_filenode(path);
    int cur_page=node->head_page,next;
    if (!node->last){
        root=node->next;
        if (root)
        root->last=NULL;
    }
    else{
        node->last->next=node->next;
    }

    if (node->next)
    node->next->last=node->last;
    /**/
    while(cur_page){
        next=get_next_block(cur_page);
        munmap(mem[cur_page],blocksize);
        mem[cur_page]=NULL;
        cur_page=next;
    }
    set_root(root);

    return 0;
}

static const struct fuse_operations op = {
    .init = oshfs_init,
    .getattr = oshfs_getattr,
    .readdir = oshfs_readdir,
    .mknod = oshfs_mknod,
    .open = oshfs_open,
    .write = oshfs_write,
    .truncate = oshfs_truncate,
    .read = oshfs_read,
    .unlink = oshfs_unlink,
};

int main(int argc, char *argv[])
{
    return fuse_main(argc, argv, &op, NULL);
}
