#include "libTinyFS.h"

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

    printf("success unmounting!\n");

    return 0;
} 
