//
// Created by bh398 on 12/1/18.
//

#ifndef ASSIGNMENT3_SFS_H
#define ASSIGNMENT3_SFS_H

#endif //ASSIGNMENT3_SFS_H

// block_size = 512 = 2^9
#define BLOCK_SIZE 512
#define DISK_SIZE (16*1024*1024)
//At most 4096 inodes, which needs 4096 / 4 = 1024 = 2^10 inode blocks
//and needs bitmap of 4096 bits = 2^12 bits = 2^9 bytes = 512 bytes = 1 block
#define MAX_FILE_NUMBER 4096
//total_blocks = 16 * 1024 * 2 = 2^15, need bitmap of 2^15 bits = 2^12 bytes = 8 blocks
//So in 2^15 blocks, 1+1+8=10 was used for superblock and bitmap
#define TOTAL_BLOCKS (DISK_SIZE / BLOCK_SIZE)
#define INODE_SIZE 128  //2^7
#define MAX_BLOCKS_OF_FILE 12 //A file can have at most 12 blocks
#define FILE_ENTRY_SIZE 128
#define MAX_OPENED_FILES 100
/***************************************************************************************************
 ***************************************************************************************************
 * Distribution of Blocks
 * superblock | inode bitmap | data block bitmap | inode block | data block
 * 0            1              2 - 9               10 - 1033     1034 - 32768
 * 1 block      1 block        8 blocks            1024 blocks   31735 blocks
 ***************************************************************************************************
 ***************************************************************************************************/


typedef struct superblock {
    unsigned int total_blocks;
    unsigned int inode_bitmap_begin; // inode bitmap only takes one block
    unsigned int inode_bitmap_blocks;
    unsigned int data_bitmap_begin;
    unsigned int data_bitmap_blocks;
    unsigned int inode_begin;
    unsigned int inode_blocks;
    unsigned int data_begin;
    unsigned int data_blocks;
    unsigned int free_data_blocks;
    unsigned int root_inode_ptr;
} superblock;

/**
 * Size table:
 * Double = off_t = size_t = time_t = nlink_t = 8
 * Float = int = mode_t = enum = 4
 * int 4
 * short 3
 * char 1
 * time_t 8
 * enum 4
 */


typedef enum Type{
    DIRECTORY = 0, REGULAR_FILE = 1
}Type;

/**
 * Total size == 128 bytes
 */
typedef struct inode {
    unsigned int inum;    //4
    mode_t mode;    //4 can this file be read/written/executed?
    uid_t uid;    //4
    gid_t gid;    //4
    off_t size;    //8 How many bytes are in
    Type type;            //4 Dir or file
    time_t atime;    //8 Last accessed atime
    time_t ctime;   //8 Created atime
    time_t mtime;   //8 Last modified atime
    time_t dtime;   //8 Deleted atime
    unsigned int blocks_number;   //4 How many blocks this file owns
    unsigned short links_count;   //2 How many hard links are there to this file?
    unsigned int flags;    //4 how should ext2 use this inode?
//    unsigned int osd1;    //4 an OS-dependent field
    unsigned int parent_Ptr;
    //So far, the total size is 74
    unsigned int block_pointers[MAX_BLOCKS_OF_FILE];
    //MAX_BLOCKS_OF_FILE == 12. So far, the total size is 126
    //But as a 64-bit machine, this struct will be automatically padded to 128 bytes
    /** Recalculate it !!!!*/
} inode;

/**
 * Total size = 128 bytes
 */
typedef struct __filename_inum_pair{
    unsigned int inum;
    char file_name[124];
} file_entry;

//char byte_vector[4] = {0x10000000, 0x01000000, 0x00100000, 0x00010000};

typedef struct __filehandler_process_inode_tuple{
    unsigned long filehandler;
    __pid_t pid;
    unsigned int inum;
} filehandler_entry;
