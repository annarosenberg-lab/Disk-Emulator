#include <stdio.h>
#include <stdlib.h>
#include <string.h>


#define BLOCKSIZE 256
#define MAGIC_NUMBER 0x44
#define DEFAULT_DISK_SIZE 10240 
#define DEFAULT_DISK_NAME "tinyFSDisk"
typedef int fileDescriptor;

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

char *mountedDiskname;

typedef struct {
    fileDescriptor fileDescriptor;        
    char filename[9];
    struct OpenFileEntry *nextEntry;
    int offset;

} OpenFileEntry;

static OpenFileEntry originalFileTable = {-1, "root", NULL, 0};
static OpenFileEntry *openFileTable = &originalFileTable;


int tfs_seek(fileDescriptor FD, int offset);
int tfs_readByte(fileDescriptor FD, char *buffer);
int tfs_mkfs(char *filename, int nBytes);
int tfs_deleteFile(fileDescriptor FD);
int tfs_writeFile(fileDescriptor FD, char *buffer, int size);
int tfs_unmount(void);
int tfs_mount(char *diskname);
int getInodeFromFD(fileDescriptor FD);
fileDescriptor tfs_openFile(char *name);
