#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <string.h>
#include <fcntl.h>
#include "vsfs.h"

#define SUPERBLOCK_SIZE_IN_BLOCKS 1
#define FAT_SIZE_IN_BLOCKS 32
#define ROOT_DIR_SIZE_IN_BLOCKS 8

#define FAT_TABLE_LENGTH 16384 // BLOCKSIZE * FAT_SIZE_IN_BLOCKS / 4 
#define ROOT_DIR_LENGTH 128

#define MAX_FILENAME_LENGTH 30
#define METADATA_OFFSET 41 // since first 41 blocks are for metadata

// globals  =======================================
int vs_fd; // file descriptor of the Linux file that acts as virtual disk.
              // this is not visible to an application.
// ========================================================


// read block k from disk (virtual disk) into buffer block.
// size of the block is BLOCKSIZE.
// space for block must be allocated outside of this function.
// block numbers start from 0 in the virtual disk. 
int read_block (void *block, int k)
{
    int n;
    int offset;

    offset = k * BLOCKSIZE;
    lseek(vs_fd, (off_t) offset, SEEK_SET);
    n = read (vs_fd, block, BLOCKSIZE);
    if (n != BLOCKSIZE) {
	printf ("read error\n");
	return -1;
    }
    return (0); 
}

// write block k into the virtual disk. 
int write_block (void *block, int k)
{
    int n;
    int offset;

    offset = k * BLOCKSIZE;
    lseek(vs_fd, (off_t) offset, SEEK_SET);
    n = write (vs_fd, block, BLOCKSIZE);
    if (n != BLOCKSIZE) {
	printf ("write error\n");
	return (-1);
    }
    return 0; 
}

///////////////////////////////////////////////////////////////

typedef struct {
    int blockSize;
    int fatSize;
    int rootDirSize;
    int diskSize;
} SuperBlock;

typedef struct {
    char filename[MAX_FILENAME_LENGTH];
    int fileSize;
    int startBlock;
    
    // Add other attributes if necessary
} DirectoryEntry;

#define FAT_UNALLOCATED -1 // fat entry is unallocated
#define FAT_NO_NEXT -2 // fat entry is allocated but has no next entry (tail of the list)

typedef struct {
    int next; // Pointer to the next cluster
    // next = FAT_UNALLOCATED for unallocated
    // next = FAT_NO_NEXT for allocated but next cluster is empty
} FatEntry;

FatEntry *fat;
SuperBlock superblock;
DirectoryEntry *rootDir;

typedef struct {
    int fd; // File descriptor returned from open()
    char filename[MAX_FILENAME_LENGTH];
    int mode; // MODE_READ or MODE_APPEND
    
    // Add other attributes if necessary
} OpenFileEntry;

OpenFileEntry openFileTable[16]; 

// typedef struct {
//     char* data;
// } DataBlock;
// DataBlock *dataBlocks;

/********************************************************************
    Helper functions, not called directly by applications
********************************************************************/
// Find a free block in the FAT table
int find_free_block() {
    // Iterate through the FAT table to find the first available block
    for (int i = 0; i < FAT_TABLE_LENGTH; i++) {
        if (fat[i].next == FAT_UNALLOCATED) {
            // Mark the block as allocated in the FAT table, but has no next entry yet!
            fat[i].next = FAT_NO_NEXT;
            return i;
        }
    }
    return -1; // No free blocks available
}
    
// Check if the file descriptor is valid
int checkFdValidity(int fd){
    if (fd < 0 || fd >= ROOT_DIR_LENGTH || openFileTable[fd].fd == -1) {
        return -1;
    }
    return fd;
}

int find_file_by_name(const char *filename) {
    for (int i = 0; i < ROOT_DIR_LENGTH; i++) {
        if (strcmp(rootDir[i].filename, filename) == 0) {
            return i; // File found, return its index
        }
    }
    return -1; // File not found
}

/**********************************************************************
   The following functions are to be called by applications directly. 
***********************************************************************/

int vsformat (char *vdiskname, unsigned int m)
{
    char command[1000];
    int size;
    int num = 1;
    int count;

    size  = num << m;
    count = size / BLOCKSIZE;
    printf ("%d %d", m, size);
    sprintf (command, "dd if=/dev/zero of=%s bs=%d count=%d",
             vdiskname, BLOCKSIZE, count);
    printf ("executing command = %s\n", command);
    system (command);

    // Open the virtual disk for read and write
    vs_fd = open(vdiskname, O_RDWR);

    // Initialize superblock
    superblock.blockSize = BLOCKSIZE;
    superblock.fatSize = FAT_SIZE_IN_BLOCKS;
    superblock.rootDirSize = ROOT_DIR_SIZE_IN_BLOCKS;
    superblock.diskSize = size;

    // Write superblock to the virtual disk (block 0)
    write_block(&superblock, 0);

    // Initialize FAT table
    fat = (FatEntry *)malloc(FAT_TABLE_LENGTH * sizeof(FatEntry));
    for (int i = 0; i < FAT_TABLE_LENGTH; i++) {
        fat[i].next = FAT_UNALLOCATED; // Mark all entries as unallocated
    }
    // Write FAT table to the virtual disk (blocks 1 to FAT_SIZE)
    for (int i = 1; i <= FAT_SIZE_IN_BLOCKS; i++) {
        write_block(fat, i);
    }

    // Initialize root directory
    rootDir = (DirectoryEntry *)malloc(ROOT_DIR_LENGTH * sizeof(DirectoryEntry));
    for (int i = 0; i < ROOT_DIR_LENGTH; i++) {
        strcpy(rootDir[i].filename, "\0"); // Set filename to "\0" to mark as empty slot
        rootDir[i].fileSize = 0;
        rootDir[i].startBlock = FAT_UNALLOCATED;
        // Initialize any other attributes if necessary
    }
    // Write root directory to the virtual disk (blocks FAT_SIZE+1 to FAT_SIZE+ROOT_DIR_SIZE)
    for (int i = FAT_SIZE_IN_BLOCKS + 1; i <= FAT_SIZE_IN_BLOCKS + ROOT_DIR_SIZE_IN_BLOCKS; i++) {
        write_block(rootDir, i);
    }

    // Initialize open file table
    for (int i = 0; i < ROOT_DIR_LENGTH; i++){
        openFileTable[i].fd = -1; //no open files initially
    }

    // Initialize data blocks
    // dataBlocks = (DataBlock *)malloc(size - (41 * BLOCKSIZE)); //since first 41 blocks are for metadata
    // Write data blocks to the virtual disk 
    // for (int i = FAT_SIZE_IN_BLOCKS + ROOT_DIR_SIZE_IN_BLOCKS + 1; i <= (size/BLOCKSIZE) - 41; i++) {
    //     write_block(&dataBlocks, i);
    // }

    close(vs_fd);

    return (0); 
}


int  vsmount (char *vdiskname)
{
    // open the Linux file vdiskname and in this
    // way make it ready to be used for other operations.
    // vs_fd is global; hence other function can use it. 
    vs_fd = open(vdiskname, O_RDWR);
    // load (chache) the superblock info from disk (Linux file) into memory
    read_block(&superblock, 0);

    // load the FAT table from disk into memory
    for (int i = 1; i <= FAT_SIZE_IN_BLOCKS; i++) {
        read_block(&fat, i);
    }

    // load root directory from disk into memory
    for (int i = FAT_SIZE_IN_BLOCKS + 1; i <= FAT_SIZE_IN_BLOCKS + ROOT_DIR_SIZE_IN_BLOCKS; i++) {
        read_block(&rootDir, i);
    }    
    
    return(0);
}


int vsumount ()
{
    // Write superblock to the virtual disk file (block 0)
    write_block(&superblock, 0);

    // Write FAT table to the virtual disk file (blocks 1 to FAT_SIZE)
    for (int i = 1; i <= FAT_SIZE_IN_BLOCKS; i++) {
        write_block(&fat, i);
    }

    // Write root directory to the virtual disk file (blocks FAT_SIZE+1 to FAT_SIZE+ROOT_DIR_SIZE)
    for (int i = FAT_SIZE_IN_BLOCKS + 1; i <= FAT_SIZE_IN_BLOCKS + ROOT_DIR_SIZE_IN_BLOCKS; i++) {
        write_block(&rootDir, i);
    }


    fsync (vs_fd); // synchronize kernel file cache with the disk
    close (vs_fd);
    return (0); 
}

int vscreate(char *filename)
{
    // Search for an empty slot in the root directory
    int emptySlot = -1;
    for (int i = 0; i < ROOT_DIR_LENGTH; i++) {
        if (rootDir[i].filename[0] == '\0') {
            emptySlot = i;
            break;
        }
    }

    // If there's no empty slot, return an error
    if (emptySlot == -1) {
        printf("Error in vscreate: No empty slots in the root directory");
        return -1; // No available slot in the root directory
    }

    // Create a new directory entry for the file
    DirectoryEntry newFile;
    snprintf(newFile.filename, MAX_FILENAME_LENGTH, "%s", filename);
    newFile.fileSize = 0;

    // Find the first available block in the FAT table
    int startBlock = find_free_block();
    if (startBlock == -1) {
        printf("Error in vcreate: No free blocks available in the FAT table");
        return -1;
    }
    newFile.startBlock = startBlock;

    // Insert the new directory entry into the root directory
    rootDir[emptySlot] = newFile;
    
    return (0);
}


int vsopen(char *filename, int mode)
{
    // Search for the file in the root directory
    int fileIndex = -1;
    for (int i = 0; i < ROOT_DIR_LENGTH; i++) {
        if (strcmp(rootDir[i].filename, filename) == 0) {
            fileIndex = i;
            break;
        }
    }    

    // If the file is not found, return an error
    if (fileIndex == -1) {
        printf("Error in vsopen: File not found\n");
        return -1;
    }

    // Check if the file is already open in the specified mode
    for (int i = 0; i < ROOT_DIR_LENGTH; i++) {
        if (openFileTable[i].fd >= 0 && strcmp(openFileTable[i].filename, filename) == 0) {
            if (openFileTable[i].mode == mode) {
                printf("vsopen warning: File is already open in the specified mode.\nReturning existing file descriptor.\n");
                return openFileTable[i].fd;
            } else {
                printf("vsopen warning: File is already open in a different mode.\nReturning existing file descriptor.\n");
                return -1;
            }
        }
    }

    // Find an available entry in the open file table
    int openFileIndex = -1;
    for (int i = 0; i < ROOT_DIR_LENGTH; i++) {
        if (openFileTable[i].fd == -1) {
            openFileIndex = i;
            break;
        }
    }
    // If there's no available entry in the open file table, return an error
    if (openFileIndex == -1) {
        printf("Error in vsopen: Could not find available space for opening the file\n");
        return -1;
    }

    // Initialize the open file table entry
    snprintf(openFileTable[openFileIndex].filename, MAX_FILENAME_LENGTH, "%s", filename);
    openFileTable[openFileIndex].mode = mode;

    // Open the file based on the access mode
    if (mode == MODE_READ) {
        openFileTable[openFileIndex].fd = open(filename, O_RDONLY);
    } else if (mode == MODE_APPEND) {
        openFileTable[openFileIndex].fd = open(filename, O_APPEND);
    } else {
        printf("Error in vsopen: Invalid access mode\n");
        return -1;
    }

    // Return the index of the opened file in the openfile table
    return openFileIndex;
}

// Returns 0 on success, -1 on failure
int vsclose(int fd){

    if (checkFdValidity(fd) < 0){
        printf("Error in vsclose: Either the file descriptor is invalid or the specified file is not open\n");
        return -1;
    }

    // Close the file using the file descriptor
    int result = close(openFileTable[fd].fd);
    if (result < 0){
        printf("Error while closing file with fd %d", fd);
        return -1;
    }
    // Mark the open file table entry as free
    openFileTable[fd].fd = -1;
    openFileTable[fd].mode = -1; // Reset mode

    return 0;
}

int vssize (int  fd)
{
    if (checkFdValidity(fd) < 0){
        printf("Error in vssize: Either the file descriptor is invalid or the specified file is not open\n");
        return -1;
    }

    // Use the open file table to get the file size
    return rootDir[find_file_by_name(openFileTable[fd].filename)].fileSize;

}

int vsread(int fd, void *buf, int n) {
    // Check if the file descriptor is valid
    if (checkFdValidity(fd) < 0) {
        printf("Error in vsread: Either the file descriptor is invalid or the specified file is not open\n");
        return -1;
    }

    // Get the file index in the root directory
    int fileIndexInDirectory = find_file_by_name(openFileTable[fd].filename);

    // Get the start block of the file
    int currentBlock = rootDir[fileIndexInDirectory].startBlock;

    // Initialize variables to keep track of the number of bytes read and the buffer offset
    int bytesRead = 0;
    int bufferOffset = 0;

    // Continue reading until all requested bytes are read or we reach the end of the file
    while (currentBlock != FAT_NO_NEXT && bytesRead < n) {
        // Allocate a buffer to store the data block read from the virtual disk
        char dataBlock[BLOCKSIZE];

        // Read the data block from the virtual disk
        read_block(dataBlock, currentBlock + METADATA_OFFSET);

        // Calculate the number of bytes to copy from the data block to the buffer
        int bytesToCopy = (n - bytesRead) < BLOCKSIZE ? (n - bytesRead) : BLOCKSIZE;

        // Copy data from the data block to the buffer
        memcpy(buf + bufferOffset, dataBlock, bytesToCopy);

        // Update counters
        bytesRead += bytesToCopy;
        bufferOffset += bytesToCopy;

        // Move to the next block in the FAT table
        currentBlock = fat[currentBlock].next;
    }

    return bytesRead;
}

int vsappend(int fd, void *buf, int n) {
    // Check if the file descriptor is valid
    if (checkFdValidity(fd) < 0) {
        printf("Error in vsappend: Either the file descriptor is invalid or the specified file is not open\n");
        return -1;
    }

    // Get the file index in the root directory
    int fileIndexInDirectory = find_file_by_name(openFileTable[fd].filename);

    // Get the start block of the file
    int currentBlock = rootDir[fileIndexInDirectory].startBlock;

    // Move to the end of the file
    while (fat[currentBlock].next != FAT_NO_NEXT) {
        currentBlock = fat[currentBlock].next;
    }

    // Calculate the offset within the last block
    int offsetWithinBlock = rootDir[fileIndexInDirectory].fileSize % BLOCKSIZE;

    // Append new data blocks to the file
    int bytesRead = 0;
    while (bytesRead < n) {
        // Check if there's space in the current block
        int spaceInBlock = BLOCKSIZE - offsetWithinBlock;
        int bytesToWrite = (n - bytesRead) < spaceInBlock ? (n - bytesRead) : spaceInBlock;

        // Write the data block to the virtual disk
        write_block(buf + bytesRead, currentBlock + METADATA_OFFSET);

        // Update counters
        bytesRead += bytesToWrite;
        rootDir[fileIndexInDirectory].fileSize += bytesToWrite;

        // Check if we need to allocate a new block
        if (bytesRead < n) {
            // Find a free block in the FAT table
            int newBlock = find_free_block();

            // Update the FAT table entry for the new block
            fat[currentBlock].next = newBlock;
            fat[newBlock].next = FAT_NO_NEXT;

            // Move to the new block
            currentBlock = newBlock;
            offsetWithinBlock = 0;
        }
    }

    return bytesRead;
}

int vsdelete(char *filename)
{
    // Search for the file in the root directory
    int fileIndex = find_file_by_name(filename);

    // If the file is not found, return an error
    if (fileIndex == -1) {
        printf("Error in vsdelete: File not found\n");
        return -1;
    }

    // Free the blocks in the FAT table and the data table 
    int currentBlock = rootDir[fileIndex].startBlock;
    while (currentBlock != FAT_NO_NEXT) {
        // write the deleted data block back to the virtual disk
        write_block(NULL, currentBlock + METADATA_OFFSET); 

        int nextBlock = fat[currentBlock].next;
        fat[currentBlock].next = FAT_UNALLOCATED; // Mark block as unallocated
        currentBlock = nextBlock;
    }

    // Clear the directory entry for the file
    strcpy(rootDir[fileIndex].filename, "\0");
    rootDir[fileIndex].fileSize = 0;
    rootDir[fileIndex].startBlock = FAT_UNALLOCATED;

    return 0;
}

