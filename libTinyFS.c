#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "libDisk.c"

#define BLOCKSIZE 256
#define MAGIC_NUMBER 0x44
#define DEFAULT_DISK_SIZE 10240 
#define DEFAULT_DISK_NAME “tinyFSDisk”
typedef int fileDescriptor;

// superblock structure
typedef struct {
    unsigned char blockType;
    unsigned char magicNumber;
    unsigned char rootInode;
    unsigned char freeBlockListPtr;
} Superblock;

typedef struct {
    unsigned char blockType;
    unsigned char magicNumber;
    unsigned char fileName[8];
    unsigned char fileSize; 
    unsigned char dataBlockPtrs[10];
} Inode;

typedef struct{
    unsigned char blockType;
    unsigned char magicNumber;
    unsigned char nextDataBlock;
    unsigned char data[BLOCKSIZE - 3];
} FileExtent;

typedef struct {
    unsigned char blockType;
    unsigned char magicNumber;
    unsigned char nextFreeBlock; 
} FreeBlock;


int tfs_mkfs(char *filename, int nBytes){
    int disk = openDisk(filename, nBytes);
}
