#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include "libDisk.h"


#define BLOCKSIZE 256

int openDisk(char *filename, int nBytes){

    int disk;
    if (nBytes == 0){
        disk = open(filename, O_RDWR);
        if (disk == -1) return -1;
        return  disk;
    }

    else if (nBytes < BLOCKSIZE) return -1;

    else {
        disk = open(filename, O_RDWR | O_CREAT, 0666);
        if (disk == -1) return -1;
        // make sure size is a multiple of BLOCKSIZE
        nBytes = nBytes - (nBytes % BLOCKSIZE);
        if (ftruncate(disk, nBytes) == -1) {
            close(disk);
            return -1; // adjusting size failed
        }
        return disk;
    }
}

int closeDisk(int disk){
    return close(disk);
}

int readBlock(int disk, int bNum, void *block){
    if (lseek(disk, bNum * BLOCKSIZE, SEEK_SET) == -1) return -1;
    if (read(disk, block, BLOCKSIZE) < BLOCKSIZE) return -1;
    return 0;
}

int writeBlock(int disk, int bNum, void *block){
    if (lseek(disk, bNum * BLOCKSIZE, SEEK_SET) == -1) return -1;
    if (write(disk, block, BLOCKSIZE) < BLOCKSIZE) return -1;
    return 0;
}
