#include <stdio.h>
#include <string.h>
#include "ext2.h"
#define K  1024

int main(int argc, char *argv[]) {
   int fd;
   char buf[K];
   size_t n;

   if (argc < 3) {
       fprintf(stderr,"usage: %s diskimage file\n",argv[0]);
       return 1;
   }

   if (setdiskimage(argv[1]) <= 0) { 
       sprintf(buf,"setdiskimage cannot open '%s' for read/write",argv[1]);
        perror(buf);
        return 1;
   }
   fd = myopen(argv[2], 1);
   if (fd == 0) {
       fprintf(stderr,"cannot myopen %s\n",argv[2]);
       return 1;
   }
   while ((n = read(0,buf,K)) > 0) {
       buf[n] = '\0';
       if ((n = mywrite(fd,buf,strlen(buf))) <= 0)  {
           fprintf(stderr,"EOF writing to file.");
           perror("mywrite");
           return 1;
       }
   }
   myclose(fd);

   return 0;
}
