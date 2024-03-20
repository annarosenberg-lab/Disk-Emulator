#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "libDisk.h"
#include "libTinyFS.h"
#include "tinyFS_errno.h"


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
    rootInode.fileName[8] = '\0';
    rootInode.fileSize = nBytes / BLOCKSIZE;
    rootInode.filePointer = 1;
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
    newInode.fileName[8] = '\0';
    newInode.fileSize = 0;
    newInode.filePointer = newInodeBlock;
    newInode.nextInodePtr = -1;
    newInode.firstFileExtentPtr = -1;
    if (writeBlock(mountedFD, newInodeBlock, &newInode) < 0) return WRITE_ERROR;

    //update root inode to point to new inode
    Inode tempInode2 = rootInode;
    int prevInodeBlock = 1;
    while (tempInode2.nextInodePtr != -1){
        if (readBlock(mountedFD, tempInode2.nextInodePtr, &tempInode2) < 0) return READ_ERROR; // Bad inode ptr
    }
    tempInode2.nextInodePtr = newInodeBlock;
    if (writeBlock(mountedFD, tempInode.filePointer, &tempInode2) < 0) return WRITE_ERROR;


    

    //create new open file entry
    OpenFileEntry *newEntry = malloc(sizeof(OpenFileEntry));
    newEntry->fileDescriptor = nextOpenTableFD;
    strcpy(newEntry->filename, name);
    newEntry->nextEntry = openFileTable;
    openFileTable = newEntry;

    //return file descriptor
    return nextOpenTableFD;

}

int tfs_closeFile(fileDescriptor FD) {
    OpenFileEntry *prev_entry = openFileTable;
    OpenFileEntry *current_entry = openFileTable->nextEntry;
    if (prev_entry->fileDescriptor == FD) {
        openFileTable = current_entry->nextEntry;
        free(current_entry);
        return 0;
    }
    while(current_entry != NULL) {
        if (current_entry->fileDescriptor == FD) {
            break;
        }
        prev_entry = current_entry;
        current_entry = current_entry->nextEntry;
    }
    if (current_entry == NULL) {
        return 1;
    }
    prev_entry->nextEntry = current_entry->nextEntry;
    free(current_entry);
    return 0;

}


/* Writes buffer ‘buffer’ of size ‘size’, which represents an entire
file’s content, to the file system. Previous content (if any) will be 
completely lost. Sets the file pointer to 0 (the start of file) when
done. Returns success/error codes. */
int tfs_writeFile(fileDescriptor FD, char *buffer, int size){
    if (mountedDiskname == NULL) return NO_FS_MOUNTED; // No file system mounted
    if (FD < 0) return INVALID_FD; // Invalid file descriptor

    int mountedFD = openDisk(mountedDiskname, 0);
    OpenFileEntry *tempEntry = openFileTable;
    while (tempEntry != NULL) {
        if (tempEntry->fileDescriptor == FD) {
              
            // File found in open file table
            char filename[9];
            strcpy(filename, tempEntry->filename);

            //find file inode
            Inode rootInode;
            if (readBlock(mountedFD, 1, &rootInode) < 0) return READ_ERROR;
            Inode fileInode = rootInode;
            while (fileInode.nextInodePtr != -1){
                if (readBlock(mountedFD, fileInode.nextInodePtr, &fileInode) < 0) return READ_ERROR; // Bad inode ptr
              
                if (strcmp(fileInode.fileName, filename) == 0){
                    //file inode found
                    //clear file extent blocks if any exist
                    Inode tempInode = fileInode;
                    FreeBlock replacementBlock;
                    replacementBlock.blockType = 4;
                    replacementBlock.magicNumber = MAGIC_NUMBER;
                    int updateFreeBlockList = 0;
                    while (tempInode.firstFileExtentPtr != -1){
                        updateFreeBlockList = 1;
                        FileExtent targetFileExtent;
                        int targetBlock = tempInode.firstFileExtentPtr;
                        if (readBlock(mountedFD, tempInode.firstFileExtentPtr, &targetFileExtent) < 0) return READ_ERROR;
                        replacementBlock.nextFreeBlock = tempInode.firstFileExtentPtr;
                        tempInode.firstFileExtentPtr = targetFileExtent.nextDataBlock;
                        if (writeBlock(mountedFD, targetBlock, &replacementBlock) < 0) return WRITE_ERROR;
                    }

                    //link replacement blocks to free block list
                    if (updateFreeBlockList){
                        Superblock superblock;
                        if (readBlock(mountedFD, 0, &superblock) < 0) return READ_ERROR;
                        replacementBlock.nextFreeBlock = superblock.freeBlockPtr;
                        superblock.freeBlockPtr = fileInode.firstFileExtentPtr;
                        if (writeBlock(mountedFD, 0, &superblock) < 0) return WRITE_ERROR;

                    }
                    //update file inode
                    fileInode.firstFileExtentPtr = -1;
                    //write buffer to file
                    int ExtentBlocksNeeded = size / (BLOCKSIZE - 3);
                    if (size % (BLOCKSIZE - 3) != 0) ExtentBlocksNeeded++;
                    for (int i = 0; i < ExtentBlocksNeeded; i++){
                        FileExtent newExtentBlock;
                        Superblock superblock;
                        newExtentBlock.blockType = 4;
                        newExtentBlock.magicNumber = MAGIC_NUMBER;
                        if (readBlock(mountedFD, 0, &superblock) < 0) return READ_ERROR;
                        if (superblock.freeBlockPtr == -1) return OUT_OF_BLOCKS; // No free blocks
                        int extentTargetBlock = superblock.freeBlockPtr;
                        FreeBlock targetFreeBlock;
                        if (readBlock(mountedFD, superblock.freeBlockPtr, &targetFreeBlock) < 0) return READ_ERROR;

                        //write data to extent block
                        for (int j = 0; j < BLOCKSIZE - 3; j++){
                            if (i * (BLOCKSIZE - 3) + j < size){
                                newExtentBlock.data[j] = buffer[i * (BLOCKSIZE - 3) + j];
                            }
                            else {
                                newExtentBlock.data[j] = '\0';
                            }
                        }
                        //update free block list
                        if (i < ExtentBlocksNeeded - 1){
                            newExtentBlock.nextDataBlock = targetFreeBlock.nextFreeBlock;
                        }
                        else {
                            newExtentBlock.nextDataBlock = -1;
                        }
                        superblock.freeBlockPtr = targetFreeBlock.nextFreeBlock;

                        //write extent block to disk
                        if (writeBlock(mountedFD, extentTargetBlock, &newExtentBlock) < 0) return WRITE_ERROR;
                        if (writeBlock(mountedFD, 0, &superblock) < 0) return WRITE_ERROR;

                        //link extent block to file inode if first extent block
                        if (i == 0){
                            fileInode.firstFileExtentPtr = extentTargetBlock;
                        }

                    }
                    //update file inode
                    fileInode.fileSize = size;
                    if (writeBlock(mountedFD, fileInode.filePointer, &fileInode) < 0) return WRITE_ERROR;
                    return 0;
                    
                    

                }
                
            }
            return NO_INODE_MATCHING_FD; // No inode found for file descriptor
        }
        tempEntry = tempEntry->nextEntry;
    }


    return INVALID_FD; // File not found in open file table
}

int append_free_block(unsigned char fbPtr) {
    int mountedFD = openDisk(mountedDiskname, 0);
    Superblock sb;
    if (readBlock(mountedFD, 0, &sb) < 0) return -1;
    FreeBlock tempFb;
    if (readBlock(mountedFD, sb.freeBlockPtr, &tempFb) < 0) return -1;
    unsigned char prev_ptr = sb.freeBlockPtr;
    while(tempFb.nextFreeBlock != -1) {
         if (readBlock(mountedFD, tempFb.nextFreeBlock, &tempFb) < 0) return -1;
         prev_ptr = tempFb.nextFreeBlock;
    }
    tempFb.nextFreeBlock = fbPtr;
    if (writeBlock(mountedFD, prev_ptr, &tempFb) < 0) return -1;
    return 0;
}

int getInodeFromFD(fileDescriptor FD) {
    int mountedFD = openDisk(mountedDiskname, 0);
    OpenFileEntry *current_entry = openFileTable;
    while(current_entry != NULL) {
        if (current_entry->fileDescriptor == FD) {
            break;
        }
        current_entry = current_entry->nextEntry;
    }
    //find inode with the same filename
    Inode rootInode;
    readBlock(mountedFD, 1, &rootInode);
    Inode tempInode = rootInode;
    unsigned char prev_inode_ptr = -1;
    while(tempInode.nextInodePtr != -1) {
        if (strcmp(tempInode.fileName, current_entry->filename) == 0) {
            //found the file
            break;
        }
        prev_inode_ptr = tempInode.nextInodePtr;
        if (readBlock(mountedFD, tempInode.nextInodePtr, &tempInode) < 0) return -1; //bad inode ptr
    }
    return prev_inode_ptr;
}


int tfs_deleteFile(fileDescriptor FD) {
    int mountedFD = openDisk(mountedDiskname, 0);
    OpenFileEntry *current_entry = openFileTable;
    while(current_entry != NULL) {
        if (current_entry->fileDescriptor == FD) {
            break;
        }
        current_entry = current_entry->nextEntry;
    }
    //find inode with the same filename
    Inode rootInode;
    if (readBlock(mountedFD, 1, &rootInode) < 0) return -1;
    Inode tempInode = rootInode;
    unsigned char prev_inode_ptr = -1;
    while(tempInode.nextInodePtr != -1) {
        if (strcmp(tempInode.fileName, current_entry->filename) == 0) {
            //found the file
            break;
        }
        prev_inode_ptr = tempInode.nextInodePtr;
        if (readBlock(mountedFD, tempInode.nextInodePtr, &tempInode) < 0) return -1; //bad inode ptr
    }
    FileExtent tempFileExtent;
    unsigned char prev_ptr = tempInode.firstFileExtentPtr;
    unsigned char first_ptr = tempInode.firstFileExtentPtr;
    if (readBlock(mountedFD, tempInode.firstFileExtentPtr, &tempFileExtent) < 0) return -1;
    //free all the blocks
    while(tempFileExtent.nextDataBlock != -1) {
        FreeBlock fb = {4, 0x44, tempFileExtent.nextDataBlock, NULL, NULL};
        if (writeBlock(mountedFD, prev_ptr, &fb) < 0) return -1;
        prev_ptr = tempFileExtent.nextDataBlock;
        if (readBlock(mountedFD, tempFileExtent.nextDataBlock, &tempFileExtent) < 0) return -1;
    }
    //free the last block, and finally the inode   
    FreeBlock fb = {4, 0x44, tempFileExtent.nextDataBlock, NULL, NULL};
    if (writeBlock(mountedFD, prev_ptr, &fb) < 0) return -1;
    fb.nextFreeBlock = first_ptr;
    if (writeBlock(mountedFD, prev_inode_ptr, &fb) < 0) return -1;
    //insert the free block chain to all free blocks
    append_free_block(prev_inode_ptr);
    return 0;
}

int tfs_readByte(fileDescriptor FD, char *buffer) {
    int mountedFD = openDisk(mountedDiskname, 0);
    int inodePtr = getInodeFromFD(FD);
    Inode tempInode;
    if (readBlock(mountedFD, inodePtr, &tempInode) < 0) return -1;
    OpenFileEntry *current_entry = openFileTable;
    while(current_entry != NULL) {
        if (current_entry->fileDescriptor == FD) {
            break;
        }
        current_entry = current_entry->nextEntry;
    }
    int offset = current_entry->offset;
    if (offset >= tempInode.fileSize) {
        return -1;
    }
    int block_count = floor(offset / (BLOCKSIZE - 4));
    int remainder_offset = offset % (BLOCKSIZE - 4);
    //get the first data block
    FileExtent tempFileExtent;
    if (readBlock(mountedFD, tempInode.firstFileExtentPtr, &tempFileExtent) < 0) return -1;
    //read the rest of them
    while(block_count > 0) {
        if (readBlock(mountedFD, tempFileExtent.nextDataBlock, &tempFileExtent) < 0) return -1;
        block_count--;
    }
    memcpy(buffer, tempFileExtent.data + remainder_offset, 1);
    current_entry->offset++;
    return 0;
}

int tfs_seek(fileDescriptor FD, int offset) {
    OpenFileEntry *current_entry = openFileTable;
    while(current_entry != NULL) {
        if (current_entry->fileDescriptor == FD) {
            break;
        }
        current_entry = current_entry->nextEntry;
    }
    current_entry->offset = offset;
    return 0;
}


//main function
int main(int argc, char *argv[]){
    if (argc < 2){
        printf("Usage: %s <diskname>\n", argv[0]);
        return 1;
    }

    char *diskname = argv[1];
    tfs_mkfs(diskname, DEFAULT_DISK_SIZE);

    int result = tfs_mount(diskname);
    if (result < 0){
        printf("Error mounting disk\n");
        return 1;
    }

    printf("Disk mounted\n");
    printf("mountedDiskname: %s\n", mountedDiskname);
    int mountedFD = openDisk(mountedDiskname, 0);
    printf("mountedFD: %d\n", mountedFD);

    //create file with tfs_openFile
    fileDescriptor fd = tfs_openFile("file_01");
    printf("new file fileDescriptor: %d\n", fd);

    //check if inode was created
    Inode rootInode;
    result = readBlock(mountedFD, 1, &rootInode);
    if (result < 0){
        printf("Error reading block\n");
        return 1;
    }
    //print inone nexr ptr
    printf("rootInode.nextInodePtr: %d\n", rootInode.nextInodePtr);
    


    //test tfs_writeByte
    char *buffer = "Hello, World!";
    result = tfs_writeFile(fd, buffer, strlen(buffer));
    if (result < 0){
        printf("Error writing to file\n, result: %d\n", result);
        return 1;
    }

    //test tfs_readByte
    char buf[10];
    for (int i = 0; i < 14; i++) {
        result = tfs_readByte(fd, buf);
        if (result < 0){
            printf("Error reading file\n, result: %d\n", result);
            break;
        }
        printf("Byte read %s\n", buf);
    }

    //test tfs_seek
    tfs_seek(fd, 0);
    tfs_readByte(fd, buf);
    printf("Byte read %s\n", buf);

    
    

    result = tfs_unmount();
    if (result < 0){
        printf("Error unmounting disk\n");
        return 1;
    }


    return 0;
} 
