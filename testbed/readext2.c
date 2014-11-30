
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <sys/types.h>
#include <time.h>
#include <pwd.h>
#include <grp.h>
#include "ext2.h"

BLOCK1 sb1;
BLOCK2 sb2;

int gotoBlock(int fd, uint blockno);
int gotoInode(int fd, uint inodeno);
void readsuperblock(int fd);
void readbgd(int fd, BGDT *bg);
INODE *getinode(int fd, uint inodeno);
void readinodebitmap(int fd, uint start, uint inode_table_start); 
void readblockbitmap(int fd, uint bitmap_block);
void read_block(int fd, uint blockno[], uint count) ;
DIR_STRUCT *read_dir(int fd, uint inode_table_start, char *dirname);
DIR_STRUCT *get_dir(int fd, uint blockno[], uint count);
void print_dir(char *prefix, DIR_STRUCT *d);
void printmode(uint mode);

int main(int argc,char *argv[]) {
  int fd;
  int i,j,k;
  uint blockcount;
  uint inodecount;
  uint blockgroups;
  if (argc < 2) {
    fprintf(stderr,"usage: %s diskfile\n",argv[0]);
    exit(0);
  }

  if ((fd = open(argv[1], O_RDONLY)) < 0) {
     perror("open");
     exit(0);
  }
 
  //read the superblock for the block size
  readsuperblock(fd);
  //read 1st block group dt
  BGDT bg;
  readbgd(fd,&bg);
  DIR_STRUCT *ds,*top,*d;
  top = read_dir(fd,2,"."); //root fs is at inode 2
  ds = top;  
  //print_dir(".",ds);    
  putchar('\n');

  close(fd);
}

//jump to a given block number
//return where we were before, in case we need to go back
int gotoBlock(int fd, uint blockno) {
  int oldPos = lseek(fd,0,SEEK_CUR);
  lseek(fd,blockno*(K<<sb1.s_log_block_size),SEEK_SET);
  return oldPos;
}

//jump to first data block for a given inode
//return where we were before, in case we need to go back
int gotoInode(int fd, uint inodeno) {
    uint block_group,blockno,inode_index, starting_pos;
    block_group = (inodeno-1) / sb1.s_inodes_per_group;
    inode_index = (inodeno-1) % sb1.s_inodes_per_group;
    //jump to the inode table for this group
    //find the inode structure for the node at this index
    //read the metadata from the inode structure
    //
    //later: access the blocks for file access.

    //find the real block number for the inode table for this group:
    blockno = block_group*sb1.s_blocks_per_group + 2; //block group descriptor table
    starting_pos = gotoBlock(fd,blockno);
    BGDT bg;
    read(fd,&bg,sizeof(BGDT));
    gotoBlock(fd,bg.bg_inode_table);
    lseek(fd,inode_index*sb2.s_inode_size,SEEK_CUR); //jump to this index
    return starting_pos;
}

void readsuperblock(int fd) {
    //jump to superblock
    lseek(fd,K,SEEK_SET);
    //gotoBlock(fd,1);  // can't use gotoblock, sb1 hasn't been filled-in

    if ((read(fd,&sb1,sizeof(BLOCK1))) < 0) {
        perror("read sblock1");
        return;
    }

    if ((read(fd,&sb2,sizeof(BLOCK2))) < 0) {
        perror("read sblock2");
        return;
    }
/*  
printf("Superblock Info:\n");
  printf("Blocks: %d  blocks per group: %d block size: %d block groups: %d\n",sb1.s_blocks_count,sb1.s_blocks_per_group,(K<<sb1.s_log_block_size), sb1.s_blocks_count/sb1.s_blocks_per_group+1);
  printf("inodes: %d  inodes per group: %d inode size: %d\n",sb1.s_inodes_count,sb1.s_inodes_per_group,sb2.s_inode_size);
 */
}


void readbgd(int fd, BGDT *bg) {
    //jump to block group, read descriptor
    //lseek(fd,K+(K<<sb1.s_log_block_size),SEEK_SET); //comes after the superblock
    gotoBlock(fd,2);
    uint i;
    if ((read(fd,bg,sizeof(BGDT))) < 0) {
        perror("read block group descriptor");
        return;
    }
/*
    printf("Block Group Descriptor table:\n");
    printf("block bitmap at block: %d  inode bitmap at block: %d\n",bg->bg_block_bitmap, bg->bg_inode_bitmap);
    printf("This block's inode table starts at block: %d\n",bg->bg_inode_table);
*/
//    readblockbitmap(fd,bg.bg_block_bitmap);
    
}

void printbin(uint dec) {
    int i;
    for (i = 7;i>=0;i--){
        putchar(dec & (1<<i) ? '1':'0');
    }
}

void readblockbitmap(int fd, uint bitmap_block) {
    gotoBlock(fd,bitmap_block);
    
    uint bytes = (sb1.s_blocks_per_group > sb1.s_blocks_count ? sb1.s_blocks_count : sb1.s_blocks_per_group)/8;
    uchar bmp[bytes];
    read(fd,&bmp,bytes);

    printf("Block Bitmap (%d bytes):\n",bytes);
    int i;
    for (i = 0;i < bytes;i++) {
        if (i % 8 == 0) {
            printf("Blocks %04d-%04d: ", i*8,((i+8)*8)-1);
        }
        printbin(bmp[i]);
        if (i % 8 == 7) {
           putchar('\n');
        }
    }


}

void readinodebitmap(int fd, uint start, uint inode_table_start) {
    gotoBlock(fd,start);
    uint ibit,ibyte;
    uint bytes = (sb1.s_inodes_per_group > sb1.s_inodes_count ? sb1.s_inodes_count : sb1.s_inodes_per_group)/8;
    uchar bmp[bytes];
    read(fd,&bmp,bytes);

    /*
    printf("Inode Bitmap (%d bytes):\n",bytes);
    int i,k;
    for (i = 0;i < bytes;i++) {
        if (i % 8 == 0) {
            printf("Inodes %04d-%04d: ", i*8,((i+8)*8)-1);
        }
        printbin(bmp[i]);
        if (i % 8 == 7) {
           putchar('\n');
        }
    }

    printf("Attempting to read ...\n");
    for (i = sb2.s_first_ino; i < sb1.s_inodes_count ; i++) {
        ibyte = i / 8; ibit = i % 8;
        if(bmp[ibyte] & (1 << ibit) > 0){
            gotoBlock(fd,inode_table_start);
            lseek(fd,i*sb2.s_inode_size,SEEK_CUR);
            read(fd,&inode,sizeof(INODE));

            if (inode.i_mode == 0)  continue;
            printf("Inode #: %d\n",i);
            printf("Mode: %06o\n",inode.i_mode);
            if (inode.i_mode & 0x8000 > 0)
                printf("regular file\n");
            else if (inode.i_mode & 0x6000 > 0)
                printf("directory\n");
            printf("Inode's owner id: %d\n",inode.i_uid);
            printf("File size: %d\n", inode.i_size);
            printf("This inode spans %d blocks\n",inode.i_blocks);
            printf("Block data: \n");
            read_block(fd,inode.i_block);
        }
    }
 */
}

DIR_STRUCT *read_dir(int fd, uint inodeno,char *dirname) {
    INODE inode;
    DIR_STRUCT *top,*itr,*nx;
    char *newpath;
    //    printf("Gathering directory structure...\n");
    gotoInode(fd,inodeno);
    read(fd,&inode,sizeof(INODE));
    top = get_dir(fd,inode.i_block,inode.i_blocks);
    //push nx to the end of the list
    //for (nx = top;nx->next;nx=nx->next);

    print_dir(dirname,top);
    newpath = 0;
    for (itr = top;itr;itr=itr->next){
        if ((itr->ino->i_mode & 0x4000) > 0 && (strcmp(itr->d->name,".") != 0) && strcmp(itr->d->name,"..") != 0) {
            newpath = (char *) realloc(newpath,strlen(dirname)+strlen(itr->d->name)+1);
            sprintf(newpath,"%s/%s",dirname,itr->d->name);
            read_dir(fd,itr->d->inode,newpath);
        }
    }
    return top;
}

void printmode(uint mode) {
    //default everything to '-'
#define M(i,x,c) ((i & x) > 0) ? c : '-'
/*    putchar(M(mode,0xC000,'s'));
    putchar(M(mode,0xA000,'l'));
    putchar(M(mode,0x8000,'-'));
    putchar(M(mode,0x6000,'b'));*/
    putchar(M(mode,0x4000,'d'));
/*    putchar(M(mode,0x2000,'c'));
    putchar(M(mode,0x1000,'f'));
    putchar(M(mode,0X0800,'U'));
    putchar(M(mode,0X0400,'G'));
    putchar(M(mode,0X0200,'S'));*/
    putchar(M(mode,0X0100,'r'));
    putchar(M(mode,0X0080,'w'));
    putchar(M(mode,0X0040,'x'));
    putchar(M(mode,0X0020,'r'));
    putchar(M(mode,0X0010,'w'));
    putchar(M(mode,0X0008,'x'));
    putchar(M(mode,0X0004,'r'));
    putchar(M(mode,0X0002,'w'));
    putchar(M(mode,0X0001,'x'));
    putchar(' ');

}
INODE *getinode(int fd, uint inodeno) {
    gotoInode(fd,inodeno);
    INODE *in;
    in = (INODE *) malloc(sizeof(INODE));
    read(fd,in,sizeof(INODE)); //how many ways do i have to say '128?'
    return in;
}

void read_block(int fd, uint blockno[],uint count) {
    uint old_pos;
    int _K = 512;
    int i,j,l;
    char blockdata[_K];

    for (i = 0;i < count;i++) { //skip 13/14/15
        old_pos = lseek(fd,0,SEEK_CUR); //where am I NOW?
        lseek(fd,i*_K,SEEK_CUR); 
        if (j = read(fd,blockdata,_K) <= 0)
            break;
        else {
            blockdata[_K] = 0;
            printf("%s",blockdata); 
            }
        }
    //in case of [glass,] break
    lseek(fd,old_pos,SEEK_SET);
}

void print_dir(char *prefix, DIR_STRUCT *d) {
    DIR_STRUCT *itr;
    struct passwd *pwd;
    struct group *grp;
    char buf[11], *when;

    printf("%s:\n",prefix);
    for (itr = d;itr;itr=itr->next) {
        when = 0;
        memset(buf,0,11);
        printmode(itr->ino->i_mode);
        //printf("%o ",ino->i_mode);
        printf("%3d ",itr->ino->i_links_count);
        pwd = getpwuid(itr->ino->i_uid);
        printf("%-4s ", pwd->pw_name);
        grp = getgrgid(itr->ino->i_gid);
        printf("%-4s ", grp->gr_name);
        //printf("%2d ",ino->i_uid);
        //printf("%2d ",ino->i_gid);
        printf("%4d ",itr->ino->i_size);
        when = asctime(localtime((time_t *)&itr->ino->i_mtime));
        when[strlen(when) -1] = 0;
        printf("%s ", when);
        printf("%s\n",itr->d->name);
    }
    printf("\n");
}

DIR_STRUCT *newd(DIR *d, INODE *i) {
    DIR_STRUCT *ds;
    ds = (DIR_STRUCT *) malloc(sizeof(DIR_STRUCT));
    ds->d = d;
    ds->ino = i;
    ds->next = 0;
    return ds;
}

DIR_STRUCT *addDir(DIR_STRUCT *head, DIR_STRUCT *n) {
    DIR_STRUCT *itr;
    if (!head) {
        return n;
    }
    itr = head;
    while(itr->next) {
        itr=itr->next;
    }
    itr->next=n;

    return head;
}



DIR_STRUCT *get_dir(int fd,uint blockno[],uint count) {
    uint old_pos;
    int _K = 512;
    int i,j,l,bytes,totalBytes;
    DIR *d;INODE *ino;
    DIR_STRUCT *top,*ds,*td;
    totalBytes = 0;
    //save where we are and jump
    gotoBlock(fd,blockno[0]);
    top = td = ds = 0;
    do {
        d = (DIR *) malloc(sizeof(DIR));
        bytes = read(fd,d,8); //8 bytes of non-name data.
        d->name = (char *) malloc(sizeof(char)*d->name_len+1);
        bytes += read(fd,d->name,d->name_len);
        d->name[d->name_len] = '\0';
        old_pos = lseek(fd,0,SEEK_CUR);
        ino = getinode(fd,d->inode);
        lseek(fd,old_pos+d->rec_len-bytes,SEEK_SET);
        totalBytes += bytes;
        if (ino->i_links_count > 0)
        top = addDir(top, newd(d,ino));
    } while (totalBytes+d->rec_len < K);
    lseek(fd,old_pos,SEEK_SET);
    return top;
}
