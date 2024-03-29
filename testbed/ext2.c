
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

#define min(a,b) ((a)<(b)?(a):(b))


BLOCK1 __sb1;
BLOCK2 __sb2;
int __image_fd = 0; // global fd for ext2disk image file.
DIR_STRUCT *__open_files; //global for file metadata

int setdiskimage(char *path);
off_t gotoBlock(uint blockno);
off_t gotoInode(uint inodeno);
off_t gotoDataBlock(DIR_STRUCT *ds);
void readsuperblock();
BGDT *readbgd(uint bgno);
INODE *getinode(uint inodeno);
int readblockbitmap(uint bitmap_block, uchar bmp[]);
int readinodebitmap(uint bitmap_block, uchar bmp[]);
DIR_STRUCT *get_dir(uint blockno[], uint count);
void printmode(uint mode);
int ext2close(int fd); 


//jump to a given block number
//return where we were before, in case we need to go back
off_t gotoBlock(uint blockno) {
  int oldPos = lseek(__image_fd,0,SEEK_CUR);
  lseek(__image_fd,blockno*(K<<__sb1.s_log_block_size),SEEK_SET);
  return oldPos;
}

//jump to first data block for a given inode
//return where we were before, in case we need to go back
off_t gotoInode(uint inodeno) {
    uint block_group,blockno,inode_index, starting_pos;
    block_group = (inodeno-1) / __sb1.s_inodes_per_group;
    inode_index = (inodeno-1) % __sb1.s_inodes_per_group;
    //jump to the inode table for this group
    //find the inode structure for the node at this index
    //read the metadata from the inode structure
    //
    //later: access the blocks for file access.

    //find the real block number for the inode table for this group:
    blockno = block_group*__sb1.s_blocks_per_group + 2; //block group descriptor table
    starting_pos = gotoBlock(blockno);
    BGDT bg;
    read(__image_fd,&bg,sizeof(BGDT));
    gotoBlock(bg.bg_inode_table);
    lseek(__image_fd,inode_index*__sb2.s_inode_size,SEEK_CUR); //jump to this index
    return starting_pos;
}

off_t gotoDataBlock(DIR_STRUCT *ds) {
    off_t starting_pos = lseek(__image_fd,0,SEEK_CUR);
    uint di_block;
   //indirection support:
   //not sure why i'm doing this
   if (ds->active_block <= 11) {
       //fprintf(stderr,"gotodatablock: jumping to d_block #%d (%d)\n",ds->active_block,ds->ino->i_block[ds->active_block]);
       gotoBlock(ds->ino->i_block[ds->active_block]);
       lseek(__image_fd,ds->offset,SEEK_CUR);
   } else if (ds->active_block > 11 && ds->active_block < 268) { //indirect-block layer
       gotoBlock(ds->ino->i_block[12]);
       lseek(__image_fd,(ds->active_block-12)*sizeof(int),SEEK_CUR);
       read(__image_fd,&di_block,sizeof(int));
       //fprintf(stderr,"gotodatablock: jumping to i_block #%d (%d)\n",ds->active_block,di_block);
       if (di_block == 0) 
           return 0;
       gotoBlock(di_block);
       lseek(__image_fd,ds->offset,SEEK_CUR);
   } else if (ds->active_block > 267 && ds->active_block < 65805) { //doubly-indirect layer
       gotoBlock(ds->ino->i_block[13]);
       lseek(__image_fd,((ds->active_block-268)/256)*sizeof(int),SEEK_CUR);
       read(__image_fd,&di_block,sizeof(int)); //get the matryshka blockno 
       gotoBlock(di_block); //now, find real blockno
       lseek(__image_fd,((ds->active_block-268)%256)*sizeof(int),SEEK_CUR);
       read(__image_fd,&di_block,sizeof(int));
       if (di_block == 0)
           return 0;
       gotoBlock(di_block);
       lseek(__image_fd,ds->offset,SEEK_CUR);
   } else { //triply-indirect layer
       //no
       fprintf(stderr,"Triply-indirect block addressing not supported.\n");
       return 0;
   }
   return starting_pos;
}

void readsuperblock() {
    //jump to superblock
    lseek(__image_fd,K,SEEK_SET);

    if ((read(__image_fd,&__sb1,sizeof(BLOCK1))) < 0) {
        perror("read sblock1");
        return;
    }

    if ((read(__image_fd,&__sb2,sizeof(BLOCK2))) < 0) {
        perror("read sblock2");
        return;
    }
}


BGDT *readbgd(uint bgno) {
    //jump to block group, read descriptor
    BGDT *bg;
    bg = (BGDT *) malloc(sizeof(BGDT));
    gotoBlock(bgno);
    uint i;
    if ((read(__image_fd,bg,sizeof(BGDT))) < 0) {
        perror("read block group descriptor");
        return;
    }
    return bg;
}

void printbin(uint dec) {
    int i;
    for (i = 7;i>=0;i--){
        putchar(dec & (1<<i) ? '1':'0');
    }
}

void printinode(uint inodeno, INODE *ino) {
    fprintf(stderr,"Inode %d:\n",inodeno);
    fprintf(stderr,"Uid: %d Mode: %06o\n",ino->i_uid,ino->i_mode);
    fprintf(stderr,"Blocks: %d\n",ino->i_blocks);
}

void print_ds(DIR_STRUCT *mfd) {
    fprintf(stderr,"File: %s\n",mfd->d->name);
    fprintf(stderr,"inode: %d\tmode: %o\n",mfd->d->inode,mfd->ino->i_mode);
    fprintf(stderr,"size: %d, blocks: %d\n",mfd->ino->i_size,mfd->ino->i_blocks);
    fprintf(stderr,"block nos: ");
    int i;
    for (i = 0;i < 15;i++) {
        fprintf(stderr,"%d ",mfd->ino->i_block[i]);
    }
    fprintf(stderr,"\nCalculated size: %d\n",mfd->ino->i_blocks *512);
}

int readinodebitmap(uint bitmap_block, uchar bmp[]) {
    gotoBlock(bitmap_block);
    uint bytes = (__sb1.s_inodes_per_group > __sb1.s_inodes_count ? __sb1.s_inodes_count : __sb1.s_inodes_per_group)/8;
    return read(__image_fd,bmp,bytes);
}
int readblockbitmap(uint bitmap_block, uchar bmp[]) {
    gotoBlock(bitmap_block);
    uint bytes = (__sb1.s_blocks_per_group > __sb1.s_blocks_count ? __sb1.s_blocks_count : __sb1.s_blocks_per_group)/8;
    return read(__image_fd,bmp,bytes);
}


INODE *getinode(uint inodeno) {
    gotoInode(inodeno);
    INODE *in;
    in = (INODE *) malloc(sizeof(INODE));
    read(__image_fd,in,sizeof(INODE)); //how many ways do i have to say '128?'
    return in;
}


DIR_STRUCT *newd(DIR *d, INODE *i) {
    DIR_STRUCT *ds;
    ds = (DIR_STRUCT *) malloc(sizeof(DIR_STRUCT));
    ds->d = d;
    ds->ino = i;
    ds->active_block = 0;
    ds->offset = 0;
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



DIR_STRUCT *get_dir(uint blockno[],uint count) {
    off_t old_pos,p;
    int _K = 512;
    int i,j,l,bytes,totalBytes;
    DIR *d;INODE *ino;
    DIR_STRUCT *top,*ds,*td;
    totalBytes = 0;
    //save where we are and jump
    gotoBlock(blockno[0]);
    top = td = ds = 0;

    do {
        p = lseek(__image_fd,0,SEEK_CUR);
        d = (DIR *) malloc(sizeof(DIR));
        bytes = read(__image_fd,d,8); //8 bytes of non-name data.
        d->name = (char *) malloc(sizeof(char)*d->name_len+1);
        bytes += read(__image_fd,d->name,d->name_len);
        d->name[d->name_len] = '\0';
        old_pos = lseek(__image_fd,0,SEEK_CUR);
        ino = getinode(d->inode);
        lseek(__image_fd,old_pos+d->rec_len-bytes,SEEK_SET);
        totalBytes += bytes;
        if (ino->i_links_count > 0) { 
            top = addDir(top, newd(d,ino));
        }
    } while (totalBytes+d->rec_len < K);//used to be _k
//    lseek(__image_fd,old_pos,SEEK_SET);
    return top;
}


void __insert_fd(DIR_STRUCT *ds) {
    ds->next = __open_files;
    __open_files = ds;
}

void __remove_fd(DIR_STRUCT *ds) {
    DIR_STRUCT *itr,*last;
    if (memcmp(ds,__open_files,sizeof(DIR_STRUCT))==0) { //not gonna ==
        if (__open_files->next) {
            __open_files = __open_files->next;
        } else {
            __open_files = 0;
        }
    } else {
        last = __open_files;
        for (itr = __open_files->next;itr->next;itr=itr->next) {
            if (memcmp(itr,ds,sizeof(DIR_STRUCT))==0) { //not gonna ==
                if (itr->next) {
                    last->next=itr->next;
                } else {
                    last->next=0;
                }
            }
        }
    }
    free(ds); //we cut it out of the list. this should be safe.
}

int setdiskimage(char *path) {
    //if we'd previously opened an image...
    if (__image_fd > 0) {
        close(__image_fd); //close it
        //and close all open descriptors that use it.
        //TODO: futureproof? Let multiple images be open?
        //loop to close all __open_files;
    }
    //re-set to O_RDWR
    if ((__image_fd = open(path,O_RDWR)) < 0) {
        perror("setdiskimage");
        exit(1);
    }

    readsuperblock();
    return __image_fd;
}

uint __next_available_inode(uchar save) {
    uint bgno, ino;
    uchar ibmp[__sb1.s_inodes_per_group];

    //assume ONE block group... this time.
    BGDT *bg;
    bg = (BGDT *) malloc(sizeof(BGDT));
    bg = readbgd(2);//future proofed... sorta
    int icount = readinodebitmap(bg->bg_inode_bitmap,ibmp); //how many actually read
    int i,j,done;

    done = 0;
    for (i = 0;i < icount;i++) {
        done = 0;
        for (j = 0;j < 8;j++) {
            if ((ibmp[i] & (1 << j)) == 0) { //FOUND ONE!
                fprintf(stderr,"next_available_inode = %d \n",i*8+j+1);
                //flip the bit
                ibmp[i] |= (1 << j);
                if (save) {
                    gotoBlock(bg->bg_inode_bitmap);
                    lseek(__image_fd,i,SEEK_CUR);
                    if (write(__image_fd,(void*)&ibmp[i],1) < 0) {
                        perror("saveibmp");
                        exit(1);
                    }
                }
                return (i*8+j)+1;
            }
        }
    }

    return 0;
}

uint __next_available_block(uchar save) {
    uint bgno, bno;
    uchar bbmp[__sb1.s_blocks_per_group];

    //assume ONE block group... this time.
    BGDT *bg;
    bg = (BGDT *) malloc(sizeof(BGDT));
    bg = readbgd(2);//future proofed... sorta
    int icount = readblockbitmap(bg->bg_block_bitmap,bbmp); //how many actually read
    int i,j,done;

    for (i = 0;i < icount;i++) {
        for (j = 0;j < 8;j++) {
            if ((bbmp[i] & (1 << j)) == 0) { //FOUND ONE!
                fprintf(stderr,"next_available_block = %d\n",i*8+j);
                //flip the bit
                bbmp[i] |= (1 << j);
                if (save) {
                    gotoBlock(bg->bg_block_bitmap);
                    lseek(__image_fd,i,SEEK_CUR);
                    if (write(__image_fd,(void*)&bbmp[i],1) < 0) {
                        perror("savebbmp");
                        exit(1);
                    }
                }
                return (i*8+j);
            }
        }
    }
    return 0;
}


DIR_STRUCT *__new_fd(char *fname) {
    //making a new file:
    //find the block bitmap that p belongs to
    //find the first free block.
    //find the inode bitmap for the block group FOR THAT BLOCK.
    //allocate one block at the first free spot for the new inode
    DIR_STRUCT *new;
    //TODO: futureproof: find out the blockgroup descriptor table location for this
    //block group.
    //
    new = (DIR_STRUCT *) malloc(sizeof(DIR_STRUCT));
    //est directory structure
#define _D(x) new->d->x
    new->d = (DIR *) malloc(sizeof(DIR));
    _D(inode)= __next_available_inode(1);
    _D(name)= (char *) malloc(min(strlen(fname),255));
    strncpy(_D(name),fname,min(strlen(fname),255));
    _D(name_len)= min(strlen(fname),255);
    _D(rec_len)= _D(name_len)+8; //uint=4+ushort=2+uchar=1+uchar=1
    new->next = 0;

    //est INODE
#define _I(x) new->ino->x
    new->ino = (INODE *) malloc(sizeof(INODE));
    //default all values to 0
    memset(new->ino, 0, sizeof(INODE));
    memset(_I(i_block),0,15);
    _I(i_mode) = 0644;
    _I(i_uid)= getuid();
    _I(i_size)= 0; //current estimate
    _I(i_atime)= time(NULL);
    _I(i_ctime) = time(NULL);
    _I(i_mtime) = time(NULL); //now
    _I(i_gid)= getgid();
    _I(i_blocks) = 1;
    fprintf(stderr,"new_fd: next block:");
    _I(i_block[0]) = __next_available_block(1);
    _I(i_links_count) = 1;
    //let the rest default to 0's
    return new;
}

//WHY DO I NEED TO PASS THE NUMBER SEPARATELY???
//darn you ext2
int __write_inode(int inodeno, INODE *data) {
    gotoInode(inodeno);
    uint bytes;
    if ((bytes = write(__image_fd,data,sizeof(INODE))) < 0) {
        perror("write_inode");
    }
    return bytes;
}

//Dir needs to be the PARENT of the target directory.
int __write_ds(DIR_STRUCT *dir, off_t insert_point) {
    INODE cur;
    uchar done,old_pos;
    uint bytes,totalBytes = 0;
    //jump to offset
    //though, we should already be there.
    lseek(__image_fd,insert_point,SEEK_SET);
    //write out the parent ds first.
    //there will be a child of this dir.
    if ((bytes = write(__image_fd,dir->d,sizeof(dir->d))) <= 0) {
        perror("write_ds update entry");
    }
    totalBytes += bytes;
    //skip over the name and the padding.
    lseek(__image_fd,dir->d->rec_len-bytes,SEEK_CUR);
    if ((bytes = write(__image_fd,dir->next->d,sizeof(dir->d))) < 0) {
        perror("write_ds write new ds");
    }
    totalBytes += bytes;
    if ((bytes = write(__image_fd,dir->next->d->name,dir->next->d->name_len)) < 0) {
        perror("write_ds write new filename");
    }
    totalBytes += bytes;
    return totalBytes;
}

DIR_STRUCT *find_file(char *path, int mode) {
    DIR_STRUCT *top,*itr;
    DIR d;
    INODE root;
    DIR_STRUCT *mfd,*parent,*last;
    uint old_rl, bytes,totalBytes, padding;
    char *cur,*fullpath,*target;
    char done;
    off_t tmp,old_pos;
    uchar flag;

    gotoInode(2);
    read(__image_fd,&root,sizeof(INODE));
    top = get_dir(root.i_block,root.i_blocks);
        
    fullpath = strdup(path);

    done = 0;
    mfd = 0;
    cur = strtok(path,"/");
    parent = top;
    last =0;
    for (itr = top;itr;) {
//        fprintf(stderr,"i: %d %s rl:%d \n",itr->d->inode,itr->d->name,itr->d->rec_len);
        if (strcmp(itr->d->name,cur) == 0) {
            target=strdup(cur);
            cur = strtok(NULL,"/");
            if (cur == NULL) { //ARE WE THERE YET?
                if ((itr->ino->i_mode & 0x4000) == 0) { //no opening directories
                    mfd = (DIR_STRUCT *) malloc(sizeof(DIR_STRUCT));
                    *mfd = *itr;
                    mfd->next = 0;
                }
                break;
            } else { //more places to go!
                parent = itr;
                last = itr;
                itr = get_dir(itr->ino->i_block,itr->ino->i_blocks);
            }
        } else {
            //it's possible that the linked list has the same element more than once. though Idunno why
       //     if ((!last) || (itr->d->inode > last->d->inode)) {    
                last = itr;
         //   }
            itr = itr->next;
        }
    }
    if (mfd == 0 && mode > 0) { //not found, want to write
        //get_dir stopped us just a bit short of
        //the end of the block.
        //need to: need to start at the parent's beginnand and
        //read back til the end
        /* possible breakthrough
        gotoBlock(parent->ino->i_block[0]);
        old_pos = lseek(__image_fd,0,SEEK_CUR);
        fprintf(stderr,"Starting with parent inode %d, looking for ending entry %d\n",parent->d->inode,last->d->inode);
        //now, read directory entires until we find the last one
        //interesting quirk.
        //when making files on the root
        //. and .. share an inode number.
        if (last->d->inode == 2) { //is .. of root
            flag = 1;
        } else {
            flag = 0;
        }
        do {
            if ((bytes = read(__image_fd,&d,8)) < 0) {
                perror("ext2open seek end of dir");
                return 0;
            } //not sure i need to read the filename...
            fprintf(stderr,"d.inode %d d.rec_len %d\n",d.inode,d.rec_len);
            if (d.inode != last->d->inode) {//not it, keep going
                old_pos = lseek(__image_fd,d.rec_len-bytes,SEEK_CUR);
            } else if (flag == 1) { //IT'S A TRAP!
                fprintf(stderr,"IT'S A TRAP!\n");
                old_pos = lseek(__image_fd,d.rec_len-bytes,SEEK_CUR);
                flag = 0;
            }
        } while (d.inode != last->d->inode && flag > 0);
        */
        gotoBlock(parent->ino->i_block[0]);
        old_pos = lseek(__image_fd,0,SEEK_CUR);
        old_pos += (K - last->d->rec_len); //THERE IT IS?
        old_rl = last->d->rec_len;
        print_ds(last);
        mfd = __new_fd(cur);
        print_ds(mfd);
        tmp = last->d->name_len+8;
        tmp += (tmp % 4 > 0 ? 4 - tmp % 4 : 0); 
        last->d->rec_len = tmp;
        padding = ((mfd->d->name_len + 8) % 4 > 0 ? (mfd->d->name_len+8)%4:0);
        mfd->d->rec_len = K - (K - old_rl + tmp + 8 + mfd->d->name_len+padding); 
        last->next = mfd;
        __write_ds(last,old_pos);
    }

    return mfd;
}

//return the inode number of the file
int ext2open(char *path, int mode) {
    if (__image_fd <= 0) {
        fprintf(stderr,"ext2open: invalid image file. Aborting.\n");
        exit(1);
    }
    //mode: O_RDONLY 0
    //      O_WRONLY 1
    //      O_RDWR   2
    DIR_STRUCT *mfd;
    INODE root;
    
    mfd = 0;
    char *newpath = strdup(path);
    mfd = find_file(newpath,mode);

    if (mfd == 0) 
        return 0;
    //debug prints:
    //print_ds(mfd);
    mfd->mode = mode;
    mfd->active_block = 0; 
    mfd->offset = 0;
    __insert_fd(mfd);

    return mfd->d->inode;
}

int ext2close(int fd) {
    if (!__open_files) { //nothing open
        return -1;
    }
    DIR_STRUCT *itr;
    for (itr = __open_files;itr;itr=itr->next) {
        if (itr->d->inode == fd) {
            //print_ds(itr);
            __write_inode(itr->d->inode,itr->ino);
            __remove_fd(itr);
            return 0;
        }
    }
    return -1;
}

//fd is really the inode number
size_t ext2read(int fd, void *buf, size_t len) {
   int i,j,k;
   int endp,totalbytes,bytes;
   uint di_block;
   DIR_STRUCT *itr;
   uchar done = 0;

   for(itr = __open_files;itr;itr=itr->next) {
       if (itr->d->inode == fd)
           break;
   }
   if (!itr) {
      fprintf(stderr,"ext2read: invalid fd %d\n",fd);
      return 0;
   }

   if (itr->mode == 1) { //open for writing only?
       return 0;
   }
   
   //don't let them request more bytes than the file has to give
   //don't forget: take into account where we are vs how much
   //is left to read.
   //remaindeer is: total size - how much of it we've read already.
   //               i_size     - blocks we've read so far*(block size) + offset from last block boundry
   uint remainder = itr->ino->i_size - (itr->active_block*(K)+itr->offset);
   len = min(len,remainder);
   bytes = totalbytes = 0;
   //jump to correct data block
   if (gotoDataBlock(itr) == 0) {
       fprintf(stderr,"ext2read: premature EOF on block# %d\n",itr->active_block);
       return 0;
   }

   while (!done) {
       //don't read past the block boundry
       endp = min(len - totalbytes,K - itr->offset);
       bytes = read(__image_fd,buf+totalbytes,endp);
       totalbytes += bytes;
       if (bytes < 0) {
           perror("read");
       }
       if (bytes < endp) {
           fprintf(stderr,"expected bytecount mismatch: expected %d, got %d\n",endp,bytes);
           break;
       }
       //update the offset/active_block
       if (bytes+itr->offset == K) { //hit end-of-block
           itr->active_block++; 
           if (gotoDataBlock(itr) > 0)
               itr->offset = 0;
           else
               break;
       } else { //otherwise: keep offsetting it... that's fine.
           itr->offset += bytes;
       }
       if (totalbytes == len)
           break; //done 
   }
   return totalbytes;
}

size_t ext2write(int fd, void *buf, size_t len) {
   DIR_STRUCT *itr;
   size_t remainder, endp,bytes,totalbytes;
   off_t pos;
   uint blockno,blockno2;
   for(itr = __open_files;itr;itr=itr->next) {
       if (itr->d->inode == fd)
           break;
   }
   if (!itr) {
      fprintf(stderr,"ext2write: invalid fd %d\n",fd);
      return 0;
   }

   if (itr->mode == 0) { //open for reading only?
       return 0;
   }
   //have file, time to make "magic"
   //find the data starting point
   
   bytes = totalbytes = 0;
   gotoDataBlock(itr);
   do {
       //set the endpoint to be the lesser of:
       //the data remaining to write and the remaining distance
       //to the block boundry.
       endp = min(len-totalbytes,(K - itr->offset));
       //assume that we're at a place where we can write.
       if ((bytes = write(__image_fd,buf+totalbytes,endp)) < 0) {
           perror("ext2write");
           exit(1);
       }
       if (bytes < 0) {
           perror("ext2write");
           break;
       }
       totalbytes += bytes;
       itr->ino->i_size+=bytes;
       if (bytes + itr->offset > K) { //wrote too much!
           fprintf(stderr,"Panic! overwrote block boundry!\n");
           exit(1); //what do, we just broke the disk?
       }
       else if (bytes + itr->offset < K) { //more room
           itr->offset += bytes;
       }
       else { //properly hit the block boundry
           itr->active_block ++;
           itr->ino->i_blocks+=2;
           pos = lseek(__image_fd,0,SEEK_CUR);
           //grab next block. set it to 'used'
           blockno = __next_available_block(1);
           //indirection support
           if (itr->active_block < 12) {//direct
               itr->ino->i_block[itr->active_block] = blockno;
               itr->offset = itr->offset + bytes - K;//should be 0
               __write_inode(itr->d->inode,itr->ino);
           } else if (itr->active_block == 12) { //first indirection.
               //first indirection means: establish block for indirect blocknos
               itr->ino->i_block[12] = blockno;
               __write_inode(itr->d->inode,itr->ino);
               blockno = __next_available_block(1);
               //now, write this blockno into the block pointed to by [12]
               gotoBlock(itr->ino->i_block[12]);
               write(__image_fd,(void*)&blockno,sizeof(int)); //blockno saved.
               gotoBlock(blockno);
               itr->offset = itr->offset + bytes - K;//should be 0
           } else if (itr->active_block < 268) { //indirect
               gotoBlock(itr->ino->i_block[12]);
               lseek(__image_fd,(itr->active_block-12)*sizeof(int),SEEK_CUR);
               write(__image_fd,(void*)&blockno,sizeof(int));
               gotoBlock(blockno);
               itr->offset = itr->offset + bytes - K;//should be 0
           }
           else if ((itr->active_block-268)%256 == 0) {//new level of double-indirection
               if (itr->ino->i_block[13]) {
                   gotoBlock(itr->ino->i_block[13]);
                   lseek(__image_fd,((itr->active_block-268)/256)*sizeof(int),SEEK_CUR);
                   write(__image_fd,(void*)&blockno,sizeof(int)); //set the matryshka blockno 
               }
               else {
                   itr->ino->i_block[13] = blockno;
                   __write_inode(itr->d->inode,itr->ino);
               }
               gotoBlock(blockno);
               pos = lseek(__image_fd,0,SEEK_CUR);
               blockno = __next_available_block(1);
               lseek(__image_fd,pos,SEEK_SET);
               write(__image_fd,(void*)&blockno,sizeof(int));
               gotoBlock(blockno);
               itr->offset = itr->offset + bytes - K; //should be 0
           }
           else if (itr->active_block < 65804)  { //doubly-indirect
               // no... 
               // YES!
               gotoBlock(itr->ino->i_block[13]);
               lseek(__image_fd,(itr->active_block-268)/256,SEEK_CUR);
               read(__image_fd,&blockno2,sizeof(int));
               gotoBlock(blockno2);
               lseek(__image_fd,(itr->active_block-268)%256,SEEK_CUR);
               write(__image_fd,(void*)&blockno,sizeof(int));
               gotoBlock(blockno);
               itr->offset = itr->offset + bytes - K; //should be 0
           }
           else { //triply indirect??
               //no...
           }
       }
       if (bytes < endp) {
           fprintf(stderr,"write mismatch: expected %d got %d\n", endp, bytes);
           break;
       }
       if (totalbytes == len) {
           break;
       }
   } while (len > 0);
   itr->ino->i_blocks= itr->ino->i_size / 512 + 1;
   return totalbytes;
}
