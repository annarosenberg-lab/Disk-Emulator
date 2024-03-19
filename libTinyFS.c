#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "libDisk.c"
#include "tinyFS_errno.h"

#define BLOCKSIZE 256
#define MAGIC_NUMBER 0x44
#define DEFAULT_DISK_SIZE 10240 
#define DEFAULT_DISK_NAME “tinyFSDisk”
typedef int fileDescriptor;

int tfs_unmount(void);

// superblock structure
typedef struct {
    unsigned char blockType;
    unsigned char magicNumber;
    unsigned char rootInode;
    char freeBlockPtr;
    char emptyBytes[BLOCKSIZE - 4];
} Superblock;

typedef struct {
    unsigned char blockType;
    unsigned char magicNumber;
    unsigned char fileName[9];
    char fileSize; // in blocks
    char filePointer;
    char nextInodePtr;
    char firstFileExtentPtr;
    char emptyBytes[BLOCKSIZE - 15];
} Inode;

typedef struct{
    unsigned char blockType;
    unsigned char magicNumber;
    char nextDataBlock;
    char data[BLOCKSIZE - 3];
} FileExtent;

typedef struct {
    unsigned char blockType;
    unsigned char magicNumber;
    char nextFreeBlock; 
    char emptyBytes[BLOCKSIZE - 3];
} FreeBlock;

static char *mountedDiskname = NULL;

typedef struct {
    fileDescriptor fileDescriptor;        
    char filename[9];
    struct OpenFileEntry *nextEntry;

} OpenFileEntry;

static OpenFileEntry *openFileTable = NULL;




/* Makes a blank TinyFS file system of size nBytes on the unix file
specified by ‘filename’. This function should use the emulated disk
library to open the specified unix file, and upon success, format the
file to be a mountable disk. This includes initializing all data to 0x00,
setting magic numbers, initializing and writing the superblock and
inodes, etc. Must return a specified success/error code. */
int tfs_mkfs(char *filename, int nBytes){
    int disk = openDisk(filename, nBytes);
    if (disk < 0) return INVALID_DISK; // Error opening disk

    Superblock superblock;
    superblock.blockType = 1;
    superblock.magicNumber = MAGIC_NUMBER;
    superblock.rootInode = 1;
    superblock.freeBlockPtr = 2;

    Inode rootInode;
    rootInode.blockType = 2;
    rootInode.magicNumber = MAGIC_NUMBER;
    strcpy(rootInode.fileName, "root");
    rootInode.fileSize = nBytes / BLOCKSIZE;
    rootInode.filePointer = -1;
    rootInode.nextInodePtr = -1;
    rootInode.firstFileExtentPtr = -1;

    // Write superblock and root inode to disk
    if (writeBlock(disk, 0, &superblock) < 0) return WRITE_ERROR;
    if (writeBlock(disk, 1, &rootInode) < 0) return WRITE_ERROR;

    // Initialize free blocks
    FreeBlock freeBlock;
    freeBlock.blockType = 3;
    freeBlock.magicNumber = MAGIC_NUMBER;
    freeBlock.nextFreeBlock = 3;
    for (int i = 2; i < nBytes / BLOCKSIZE; i++){
        if (writeBlock(disk, i, &freeBlock) < 0) return WRITE_ERROR;
        if (i < nBytes / BLOCKSIZE - 1) freeBlock.nextFreeBlock = i + 1;
        else if (i == nBytes / BLOCKSIZE - 1) freeBlock.nextFreeBlock = -1;
    }

    return 0;

}


/* tfs_mount(char *diskname) “mounts” a TinyFS file system located within
‘diskname’. As part of the mount operation, tfs_mount should verify the file
system is the correct type. In tinyFS, only one file system may be
mounted at a time.  Must return a specified success/error code. */
int tfs_mount(char *diskname){
    if (mountedDiskname != NULL) tfs_unmount(); // File system already mounted
    int disk = openDisk(diskname, 0);
    if (disk < 0) return INVALID_DISK; // Error opening disk, add error message

    char buffer[BLOCKSIZE];
    if (readBlock(disk, 0, buffer) < 0) return -1;
    if (buffer[0] != 1) return NOT_TINYFS_FORMAT; // Incorrect block type
    if (buffer[1] != MAGIC_NUMBER) return NOT_TINYFS_FORMAT; // Incorrect magic number

    //get file size
    char rootInode[BLOCKSIZE];
    if (readBlock(disk, 1, rootInode) < 0) return READ_ERROR;
    char fileSize = rootInode[11];

    //check that every block has correct magic number
    for (int i = 1; i < fileSize; i++){
        if (readBlock(disk, i, buffer) < 0) return READ_ERROR;
        if (buffer[1] != MAGIC_NUMBER) return NOT_TINYFS_FORMAT; // Incorrect magic number
    }
    
    mountedDiskname = diskname;
    return 0;

}

int tfs_unmount(void){

    mountedDiskname = NULL;

    // clear the open file table
    OpenFileEntry *tempEntry = openFileTable;
    while (tempEntry != NULL) {
        OpenFileEntry *nextEntry = tempEntry->nextEntry;
        free(tempEntry);
        tempEntry = nextEntry;
    }
    openFileTable = NULL;
    return 0;

}

/* Creates or Opens a file for reading and writing on the currently
mounted file system. Creates a dynamic resource table entry for the file,
and returns a file descriptor (integer) that can be used to reference
this entry while the filesystem is mounted. */
fileDescriptor tfs_openFile(char *name){
    

    if (mountedDiskname == NULL) return NO_FS_MOUNTED; // No file system mounted

    int mountedFD = openDisk(mountedDiskname, 0);
    fileDescriptor nextOpenTableFD = mountedFD + 1;
    //check if already in open table
    if (openFileTable != NULL) {
        OpenFileEntry *tempEntry = openFileTable; 
        while (tempEntry != NULL) { 
            if (strcmp(tempEntry->filename, name) == 0) { 
                // File already exists in table, return file descriptor
                return tempEntry->fileDescriptor;
            }
            tempEntry = tempEntry->nextEntry; 
            nextOpenTableFD++;
        }
}

    //check if inode with name already exists
    Inode rootInode;
    if (readBlock(mountedFD, 1, &rootInode) < 0) return READ_ERROR;
    Inode tempInode = rootInode;
    while (tempInode.nextInodePtr != -1){
        if (strcmp(tempInode.fileName, name) == 0){
            //file already exists
            //create new open file entry
            OpenFileEntry *newEntry = malloc(sizeof(OpenFileEntry));
            newEntry->fileDescriptor = nextOpenTableFD;
            strcpy(newEntry->filename, name);
            newEntry->nextEntry = openFileTable;
            openFileTable = newEntry;
            
            //return file descriptor
            return nextOpenTableFD;
        }
        if (readBlock(mountedFD, tempInode.nextInodePtr, &tempInode) < 0) return READ_ERROR; // Bad inode ptr
    }
    
    //if not found, create new inode (make sure there is enough space for new inode)
    Superblock superblock;
    Inode newInode;
    FreeBlock newInodeBlockInfo;
    if (readBlock(mountedFD, 0, &superblock) < 0) return READ_ERROR;
    if (superblock.freeBlockPtr == -1) return -1; // No free blocks
    char newInodeBlock = superblock.freeBlockPtr;
    if (readBlock(mountedFD, newInodeBlock, &newInodeBlockInfo) < 0) return READ_ERROR;
    superblock.freeBlockPtr = newInodeBlockInfo.nextFreeBlock;
    if (writeBlock(mountedFD, 0, &superblock) < 0) return WRITE_ERROR;
    newInode.blockType = 2;
    newInode.magicNumber = MAGIC_NUMBER;
    strcpy(newInode.fileName, name);
    newInode.fileSize = 0;
    newInode.filePointer = 0;
    newInode.nextInodePtr = -1;
    newInode.firstFileExtentPtr = -1;
    if (writeBlock(mountedFD, newInodeBlock, &newInode) < 0) return WRITE_ERROR;

    //create new open file entry
    OpenFileEntry *newEntry = malloc(sizeof(OpenFileEntry));
    newEntry->fileDescriptor = nextOpenTableFD;
    strcpy(newEntry->filename, name);
    newEntry->nextEntry = openFileTable;
    openFileTable = newEntry;

    //return file descriptor
    return nextOpenTableFD;

}
