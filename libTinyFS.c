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
    unsigned char freeBlockPtr;
    unsigned char emptyBytes[BLOCKSIZE - 4];
} Superblock;

typedef struct {
    unsigned char blockType;
    unsigned char magicNumber;
    unsigned char fileName[9];
    unsigned char fileSize; 
    unsigned char filePointer;
    unsigned char nextInodePtr;
    unsigned char firstFileExtentPtr;
    unsigned char emptyBytes[BLOCKSIZE - 15];
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
    unsigned char emptyBytes[BLOCKSIZE - 3];
} FreeBlock;



/* Makes a blank TinyFS file system of size nBytes on the unix file
specified by ‘filename’. This function should use the emulated disk
library to open the specified unix file, and upon success, format the
file to be a mountable disk. This includes initializing all data to 0x00,
setting magic numbers, initializing and writing the superblock and
inodes, etc. Must return a specified success/error code. */
int tfs_mkfs(char *filename, int nBytes){
    int disk = openDisk(filename, nBytes);
    if (disk < 0) return -1; // Error opening disk

    Superblock superblock;
    superblock.blockType = 1;
    superblock.magicNumber = MAGIC_NUMBER;
    superblock.rootInode = 1;
    superblock.freeBlockPtr = 2;

    Inode rootInode;
    rootInode.blockType = 2;
    rootInode.magicNumber = MAGIC_NUMBER;
    strcpy(rootInode.fileName, "root");
    rootInode.fileSize = 0;
    rootInode.filePointer = -1;
    rootInode.nextInodePtr = -1;
    rootInode.firstFileExtentPtr = -1;

    // Write superblock and root inode to disk
    if (writeBlock(disk, 0, &superblock) < 0) return -1;
    if (writeBlock(disk, 1, &rootInode) < 0) return -1;

    // Initialize free blocks
    FreeBlock freeBlock;
    freeBlock.blockType = 3;
    freeBlock.magicNumber = MAGIC_NUMBER;
    freeBlock.nextFreeBlock = 3;
    for (int i = 2; i < nBytes / BLOCKSIZE; i++){
        if (writeBlock(disk, i, &freeBlock) < 0) return -1;
        if (i < nBytes / BLOCKSIZE - 1) freeBlock.nextFreeBlock = i + 1;
        else if (i == nBytes / BLOCKSIZE - 1) freeBlock.nextFreeBlock = -1;
    }

    return 0;

}
