/*
  Simple File System

  This code is derived from function prototypes found /usr/include/fuse/fuse.h
  Copyright (C) 2001-2007  Miklos Szeredi <miklos@szeredi.hu>
  His code is licensed under the LGPLv2.

*/

#include "params.h"
#include "block.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <fuse.h>
#include <libgen.h>
#include <limits.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <math.h>
#include <fuse/fuse_common.h>

#ifdef HAVE_SYS_XATTR_H
#include <sys/xattr.h>
#endif

#include "log.h"
#include "sfs.h"
#include "sfs_helper_functions.h"


///////////////////////////////////////////////////////////
//
// Prototypes for all these functions, and the C-style comments,
// come indirectly from /usr/include/fuse.h
//

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

#define INODE_BITMAP_UPDATE 0
#define DATA_BITMAP_UPDATE 1
/***************************************************************************************************
 ***************************************************************************************************
 * Distribution of Blocks
 * superblock | inode bitmap | data block bitmap | inode block | data block
 * 0            1              2 - 9               10 - 1033     1034 - 32768
 * 1 block      1 block        8 blocks            1024 blocks   31735 blocks
 ***************************************************************************************************
 ***************************************************************************************************/


superblock *sb;
inode *current_dir;
filehandler_entry opened_files[MAX_OPENED_FILES];
int fh_cursor = 0;


/***************************************************************************************************
 ***************************************************************************************************
 * Some helper functions
 ***************************************************************************************************
 ***************************************************************************************************/
 
/**
 * Given the index of newly allocated inode or data block, update the bitmap
 * @param index: The ith inode, or the ith data block. An index of data block is not the absolute
 *               data block number, but the relative index related to superBlock.data_begin
 * @param mode: INODE_BITMAP_UPDATE == 0; DATA_BITMAP_UPDATE == 1
 */
void update_bitmap(unsigned int index, unsigned int mode) {
    if (sb == NULL) {
        sb = malloc(BLOCK_SIZE * sizeof(char));
        block_read(0, (void *) sb);
    }
    int absolute_char_address = index / 8;
    int bitmap_block_offset = absolute_char_address / BLOCK_SIZE;
    int char_offset = absolute_char_address % BLOCK_SIZE;
    int bit_offset = index % 8;
    int bitmap_block_base = -1;
    if (mode == INODE_BITMAP_UPDATE) {
        bitmap_block_base = sb->inode_bitmap_begin;
        if (bitmap_block_offset >= sb->inode_bitmap_blocks) {
            printf("Inode bitmap calculation error~\n");
            abort();
        }
    } else if (mode == DATA_BITMAP_UPDATE) {
        bitmap_block_base = sb->data_bitmap_begin;
        if (bitmap_block_offset >= sb->data_bitmap_blocks) {
            printf("Data block bitmap calculation error~\n");
            abort();
        }
    }
    int block_to_update = bitmap_block_base + bitmap_block_offset;
    unsigned char buffer[BLOCK_SIZE];
    block_read(block_to_update, buffer);
    buffer[char_offset] = buffer[char_offset] | (128 >> bit_offset);
    block_write(block_to_update, buffer);
}

void directory_block_init(unsigned int block_id, unsigned int inum, unsigned int parent_inum) {
    block_id += sb->data_begin;
    char buffer[BLOCK_SIZE];
    memset(buffer, 0, BLOCK_SIZE);
    file_entry *fe = (file_entry *) &buffer[0];
    fe->inum = inum;
    strcpy(fe->file_name, ".");
    fe = (file_entry *) ((void *) fe + sizeof(file_entry));
    fe->inum = parent_inum;
    strcpy(fe->file_name, "..");
    block_write(block_id, buffer);
}

inode *get_inode_by_inum(int inum) {
    if (inum >= MAX_FILE_NUMBER) {
        printf("Wrong inode number!\n");
        abort();
    }
    inode *target_file = (inode *) malloc(sizeof(inode));
    char buffer[BLOCK_SIZE];
    int block_offset = inum / 4; // One block can store 4 inodes
    int byte_offset = inum % 4 * INODE_SIZE;
    block_read(sb->inode_begin + block_offset, buffer);
    memcpy(target_file, &buffer[byte_offset], INODE_SIZE);
    return target_file;
}

inode *retrieve_file(char *filename, inode *current_dir) {
    // To simplify, we assume that we only have a root directory. All the file is under this directory
    inode *target_file = NULL;
    char buffer[BLOCK_SIZE];
    int i = 0;
    for (; i < current_dir->blocks_number; i++) {
        int absolute_block_id = current_dir->block_pointers[i] + sb->data_begin;
        block_read(absolute_block_id, buffer);
        file_entry *entry;
        int j;
        for (j = 0; j < 4; j++) {
            entry = (file_entry *) &buffer[j * 128];
            if (strcmp(entry->file_name, filename) == 0) {
                target_file = get_inode_by_inum(entry->inum);
                return target_file;
            }
        }
    }
    printf("No such file or directory exists in current directory\n");
    return NULL;
}

inode *resolute_path(char *path, inode *current_dir) {
    inode *target_file = NULL;
    // Target is the root directory
    if (strcmp(path, "/") == 0) {
        target_file = get_inode_by_inum(sb->root_inode_ptr);
        return target_file;
    }
    // Otherwise, target is a file under root directory
    int start = (path[0] == '/') ? 1 : 0;
    size_t length = strlen(path) - start + 1;
    char filename[length];
    memcpy(filename, &path[start], length - 1);
    filename[length - 1] = '\0';
    target_file = retrieve_file(filename, current_dir);
    return target_file;
}


unsigned int assign_block() {
    char buffer[BLOCK_SIZE];
    if (sb->free_data_blocks == 0) return 0;
    unsigned int block_offset = 0, byte_offset = 0, bit_offset = 0;
    for (; block_offset < sb->data_bitmap_blocks; block_offset++) {
        block_read(sb->data_bitmap_begin + block_offset, buffer);
        byte_offset = 0;
        for (; byte_offset < BLOCK_SIZE; byte_offset++) {
            unsigned char c = (unsigned char) buffer[byte_offset];
            for (bit_offset = 0; bit_offset < 8; bit_offset++) {
                if (c == 128 >> bit_offset) {
                    unsigned int ret = block_offset * BLOCK_SIZE * 8 + byte_offset * 8 + bit_offset;
                    update_bitmap(ret, DATA_BITMAP_UPDATE);
                    return ret;
                }
            }
        }
    }
    return 0;
}

unsigned int assign_inode_number() {
    char buffer[BLOCK_SIZE];
    unsigned int block_offset, byte_offset, bit_offset = 0;
    for (block_offset = 0; block_offset < sb->inode_bitmap_blocks; block_offset++) {
        block_read(sb->inode_bitmap_begin + block_offset, buffer);
        for (byte_offset = 0; byte_offset < BLOCK_SIZE; byte_offset++) {
            unsigned char c = (unsigned char) buffer[byte_offset];
            for (bit_offset = 0; bit_offset < 8; bit_offset++) {
                if (c == 128 >> bit_offset) {
                    unsigned int ret = block_offset * BLOCK_SIZE * 8 + byte_offset * 8 + bit_offset;
                    return ret;
                }
            }
        }
    }
    return 0;
}


/**
 * Initialize filesystem
 *
 * The return value will passed in the private_data field of
 * fuse_context to all file operations and as a parameter to the
 * destroy() method.
 *
 * Introduced in version 2.3
 * Changed in version 2.6
 */
void *sfs_init(struct fuse_conn_info *conn) {
    fprintf(stderr, "in bb-init\n");
    log_msg("\nsfs_init()\n");

    log_conn(conn);
    log_fuse_context(fuse_get_context());

    char buffer[BLOCK_SIZE];
    //Disk initialization
    disk_open(SFS_DATA->diskfile);
    //File handler array initialization
    int i;
    for (i = 0; i < MAX_OPENED_FILES; i++) {
        opened_files[i].filehandler = (unsigned int) i;
        opened_files[i].inum = 0;
    }

    //Superblock initialization
    sb = (superblock *) malloc(sizeof(superblock));
    sb->total_blocks = TOTAL_BLOCKS;
    sb->inode_bitmap_begin = 1;
    sb->inode_bitmap_blocks = 1;
    sb->data_bitmap_begin = sb->inode_bitmap_begin + sb->inode_bitmap_blocks;
    sb->data_bitmap_blocks = (unsigned int) ceil(
            ((double) TOTAL_BLOCKS) / 8 / BLOCK_SIZE
    );
    sb->inode_begin = sb->data_bitmap_begin + sb->data_bitmap_blocks;
    sb->inode_blocks = (unsigned int) ceil(
            ((double) MAX_FILE_NUMBER) / (unsigned int) (BLOCK_SIZE / INODE_SIZE)
    );
    sb->data_begin = sb->inode_begin + sb->inode_blocks;
    sb->data_blocks = TOTAL_BLOCKS - sb->data_begin;
    sb->free_data_blocks = sb->data_blocks;
    sb->root_inode_ptr = 1;
    memset(buffer, 0, BLOCK_SIZE);
    memcpy(&buffer, &sb, sizeof(superblock));
    block_write(0, buffer);

    //Root directory '/' inode initialization
    inode *ino;
    ino = (inode *) malloc(sizeof(ino));
    ino->inum = 1;
//    inum->mode =???
    ino->uid = getuid();
    ino->gid = getgid();
    ino->size = 0; //I assume that directory size has no practical meaning, though it does have size
    ino->type = DIRECTORY;
    ino->atime = time(NULL);
    ino->ctime = ino->atime;
    ino->mtime = ino->ctime;
//    inum->dtime = 0;
    ino->blocks_number = 1;
    ino->links_count = 0;
//    ino->flags = 0;
    ino->parent_Ptr = 1;
    ino->block_pointers[0] = 1; // We reserve the first data block empty
    current_dir = ino; //This inum will not be freed here
    memset(buffer, 0, BLOCK_SIZE);
    memcpy(&buffer[128], &ino, sizeof(inode));
    block_write(sb->inode_begin, buffer);
    //Root directory '/' data block initialization
    memset(buffer, 0, BLOCK_SIZE);
    directory_block_init(ino->block_pointers[0], ino->inum, ino->inum);

    //Inode block bitmap initialization and update
    memset(buffer, 0, BLOCK_SIZE);
    for (i = 0; i < sb->inode_bitmap_blocks; i++) {
        int block_id = sb->inode_bitmap_begin + i;
        block_write(block_id, buffer);
    }
    update_bitmap(0, INODE_BITMAP_UPDATE);
    update_bitmap(1, INODE_BITMAP_UPDATE);

    //Data block bitmap initialization and update
    memset(buffer, 0, BLOCK_SIZE);
    for (i = 0; i < sb->data_bitmap_blocks; i++) {
        int block_id = sb->data_bitmap_begin + i;
        block_write(block_id, buffer);
    }
    update_bitmap(0, DATA_BITMAP_UPDATE);
    update_bitmap(1, DATA_BITMAP_UPDATE);
    sb->free_data_blocks = sb->free_data_blocks - 2;
    return SFS_DATA;
}

/**
 * Clean up filesystem
 *
 * Called on filesystem exit.
 *
 * Introduced in version 2.3
 */
void sfs_destroy(void *userdata) {
    disk_close();
    log_msg("\nsfs_destroy(userdata=0x%08x)\n", userdata);
}

/** Get file attributes.
 *
 * Similar to stat().  The 'st_dev' and 'st_blksize' fields are
 * ignored.  The 'st_ino' field is ignored except if the 'use_ino'
 * mount option is given.
 */
int sfs_getattr(const char *path, struct stat *statbuf) {
    int retstat = 0;
    char fpath[PATH_MAX];

    log_msg("\nsfs_getattr(path=\"%s\", statbuf=0x%08x)\n",
            path, statbuf);

    inode *target_file = resolute_path(path, current_dir);
    statbuf->st_ino = target_file->inum;
    statbuf->st_mode = target_file->mode;
    statbuf->st_uid = target_file->uid;
    statbuf->st_gid = target_file->gid;
//    statbuf->st_rdev = 0;
    statbuf->st_atime = target_file->atime;
    statbuf->st_ctime = target_file->ctime;
    statbuf->st_mtime = target_file->mtime;
    statbuf->st_blocks = target_file->blocks_number;
    statbuf->st_nlink = target_file->links_count;
    statbuf->st_size = target_file->size;
    return retstat;
}

/**
 * Create and open a file
 *
 * If the file does not exist, first create it with the specified
 * mode, and then open it.
 *
 * If this method is not implemented or under Linux kernel
 * versions earlier than 2.6.15, the mknod() and open() methods
 * will be called instead.
 *
 * Introduced in version 2.5
 */
int sfs_create(const char *path, mode_t mode, struct fuse_file_info *fi) {
    int retstat = 0;
    log_msg("\nsfs_create(path=\"%s\", mode=0%03o, fi=0x%08x)\n",
            path, mode, fi);

    inode ino;
    ino.inum = assign_inode_number();
    if (ino.inum == 0) {
        printf("You have reached the maximum number of files!\n");
        return -1;
    }
    ino.mode = mode;
    ino.uid = getuid();
    ino.gid = getgid();
    ino.size = 0;
    ino.type = REGULAR_FILE;
    ino.atime = time(NULL);
    ino.ctime = ino.atime;
    ino.mtime = ino.ctime;
    ino.blocks_number = 0;
    ino.links_count = 0;
    ino.flags = 0;
    ino.parent_Ptr = 1;

    char buffer[BLOCK_SIZE];
    int block_offset = ino.inum / 4;
    int byte_offset = ino.inum % 4 * INODE_SIZE;
    block_read(sb->inode_begin + block_offset, buffer);
    memcpy(&buffer[byte_offset], &ino, INODE_SIZE);
    block_write(sb->inode_begin + block_offset, buffer);
    return retstat;
}

/** Remove a file */
int sfs_unlink(const char *path) {
    int retstat = 0;
    log_msg("sfs_unlink(path=\"%s\")\n", path);


    return retstat;
}

/** File open operation
 *
 * No creation, or truncation flags (O_CREAT, O_EXCL, O_TRUNC)
 * will be passed to open().  Open should check if the operation
 * is permitted for the given flags.  Optionally open may also
 * return an arbitrary filehandle in the fuse_file_info structure,
 * which will be passed to all file operations.
 *
 * Changed in version 2.2
 */
int sfs_open(const char *path, struct fuse_file_info *fi) {
    int retstat = 0;
    log_msg("\nsfs_open(path\"%s\", fi=0x%08x)\n",
            path, fi);

    inode *ino = resolute_path(path, current_dir);
    if (ino == NULL) {
        return -1;
    }
    int i = fh_cursor;
    for (; i < MAX_OPENED_FILES; i++) {
        if (opened_files[i].inum == 0) {
            opened_files[i].inum = ino->inum;
            opened_files[i].pid = getpid();
            fi->fh = (uint64_t) i;
            fh_cursor = (i + 1) % MAX_OPENED_FILES;
            return 0;
        }
    }
    for (i = 0; i < fh_cursor; i++) {
        if (opened_files[i].inum != 0) {
            opened_files[i].inum = ino->inum;
            opened_files[i].pid = getpid();
            fi->fh = (uint64_t) i;
            fh_cursor = (i + 1) % MAX_OPENED_FILES;
            return 0;
        }
    }
    retstat = -1;
    printf("This process can not open more files!\n");
    return retstat;
}

/** Release an open file
 *
 * Release is called when there are no more references to an open
 * file: all file descriptors are closed and all memory mappings
 * are unmapped.
 *
 * For every open() call there will be exactly one release() call
 * with the same flags and file descriptor.  It is possible to
 * have a file opened more than once, in which case only the last
 * release will mean, that no more reads/writes will happen on the
 * file.  The return value of release is ignored.
 *
 * Changed in version 2.2
 */
int sfs_release(const char *path, struct fuse_file_info *fi) {
    int retstat = 0;
    log_msg("\nsfs_release(path=\"%s\", fi=0x%08x)\n",
            path, fi);

    unsigned long i = fi->fh;
    opened_files[i].inum = 0;

    return retstat;
}

/** Read data from an open file
 *
 * Read should return exactly the number of bytes requested except
 * on EOF or error, otherwise the rest of the data will be
 * substituted with zeroes.  An exception to this is when the
 * 'direct_io' mount option is specified, in which case the return
 * value of the read system call will reflect the return value of
 * this operation.
 *
 * Changed in version 2.2
 */
int sfs_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi) {
    int retstat = 0;
    log_msg("\nsfs_read(path=\"%s\", buf=0x%08x, size=%d, offset=%lld, fi=0x%08x)\n",
            path, buf, size, offset, fi);
    inode *ino = resolute_path(path, current_dir);
    if (ino == NULL) {
        return -1;
    }
    char buffer[BLOCK_SIZE];
    memset(buf, 0, size);
    size_t cursor = 0;
    unsigned int ptr_offset = (unsigned int) (offset / BLOCK_SIZE);
    unsigned int byte_offset = (unsigned int) (offset % BLOCK_SIZE);
    for (; ptr_offset < (offset + size) / BLOCK_SIZE && ptr_offset < ino->blocks_number; ptr_offset++) {
        block_read(sb->data_begin + ino->block_pointers[ptr_offset], buffer);
        size_t next_read = (size - cursor < BLOCK_SIZE - byte_offset) ?
                           (size - cursor) : (BLOCK_SIZE - byte_offset);
        memcpy(&buf[cursor], &buffer[byte_offset], next_read);
        cursor += next_read;
        byte_offset = 0;
    }
    retstat = cursor;
    return retstat;
}

/** Write data to an open file
 *
 * Write should return exactly the number of bytes requested
 * except on error.  An exception to this is when the 'direct_io'
 * mount option is specified (see read operation).
 *
 * Changed in version 2.2
 */
int sfs_write(const char *path, const char *buf, size_t size, off_t offset,
              struct fuse_file_info *fi) {
    int retstat = 0;
    log_msg("\nsfs_write(path=\"%s\", buf=0x%08x, size=%d, offset=%lld, fi=0x%08x)\n",
            path, buf, size, offset, fi);

    inode *ino = resolute_path(path, current_dir);
    if (ino == NULL) {
        return -1;
    }
    char buffer[BLOCK_SIZE];
    size_t cursor = 0;
    unsigned int ptr_offset = (unsigned int) (offset / BLOCK_SIZE);
    unsigned int byte_offset = (unsigned int) (offset % BLOCK_SIZE);
    for (; ptr_offset < (offset + size) / BLOCK_SIZE; ptr_offset++) {
        if (ptr_offset == ino->blocks_number) { // Need to enlarge this file
            if (ino->blocks_number == MAX_BLOCKS_OF_FILE) break;
            else {
                unsigned int new_block = assign_block();
                if (new_block != 0) ino->block_pointers[ino->blocks_number++] = new_block;
                else break;
            }
        }
        block_read(sb->data_begin + ino->block_pointers[ptr_offset], buffer);
        size_t next_read = (size - cursor < BLOCK_SIZE - byte_offset) ?
                           (size - cursor) : (BLOCK_SIZE - byte_offset);
        memcpy(&buffer[byte_offset], &buf[cursor], next_read);
        cursor += next_read;
        byte_offset = 0;
    }
    retstat = cursor;

    return retstat;
}


/** Create a directory */
int sfs_mkdir(const char *path, mode_t mode) {
    int retstat = 0;
    log_msg("\nsfs_mkdir(path=\"%s\", mode=0%3o)\n",
            path, mode);


    return retstat;
}


/** Remove a directory */
int sfs_rmdir(const char *path) {
    int retstat = 0;
    log_msg("sfs_rmdir(path=\"%s\")\n",
            path);


    return retstat;
}


/** Open directory
 *
 * This method should check if the open operation is permitted for
 * this  directory
 *
 * Introduced in version 2.3
 */
int sfs_opendir(const char *path, struct fuse_file_info *fi) {
    int retstat = 0;
    log_msg("\nsfs_opendir(path=\"%s\", fi=0x%08x)\n",
            path, fi);


    return retstat;
}

/** Read directory
 *
 * This supersedes the old getdir() interface.  New applications
 * should use this.
 *
 * The filesystem may choose between two modes of operation:
 *
 * 1) The readdir implementation ignores the offset parameter, and
 * passes zero to the filler function's offset.  The filler
 * function will not return '1' (unless an error happens), so the
 * whole directory is read in a single readdir operation.  This
 * works just like the old getdir() method.
 *
 * 2) The readdir implementation keeps track of the offsets of the
 * directory entries.  It uses the offset parameter and always
 * passes non-zero offset to the filler function.  When the buffer
 * is full (or an error happens) the filler function will return
 * '1'.
 *
 * Introduced in version 2.3
 */
int sfs_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset,
                struct fuse_file_info *fi) {
    int retstat = 0;


    return retstat;
}

/** Release directory
 *
 * Introduced in version 2.3
 */
int sfs_releasedir(const char *path, struct fuse_file_info *fi) {
    int retstat = 0;


    return retstat;
}

struct fuse_operations sfs_oper = {
        .init = sfs_init,
        .destroy = sfs_destroy,

        .getattr = sfs_getattr,
        .create = sfs_create,
        .unlink = sfs_unlink,
        .open = sfs_open,
        .release = sfs_release,
        .read = sfs_read,
        .write = sfs_write,

        .rmdir = sfs_rmdir,
        .mkdir = sfs_mkdir,

        .opendir = sfs_opendir,
        .readdir = sfs_readdir,
        .releasedir = sfs_releasedir
};

void sfs_usage() {
    fprintf(stderr, "usage:  sfs [FUSE and mount options] diskFile mountPoint\n");
    abort();
}

int main(int argc, char *argv[]) {
    int fuse_stat;
    struct sfs_state *sfs_data;

    // sanity checking on the command line
    if ((argc < 3) || (argv[argc - 2][0] == '-') || (argv[argc - 1][0] == '-'))
        sfs_usage();

    sfs_data = malloc(sizeof(struct sfs_state));
    if (sfs_data == NULL) {
        perror("main calloc");
        abort();
    }

    // Pull the diskfile and save it in internal data
    sfs_data->diskfile = argv[argc - 2];
    argv[argc - 2] = argv[argc - 1];
    argv[argc - 1] = NULL;
    argc--;

    sfs_data->logfile = log_open();

    // turn over control to fuse
    fprintf(stderr, "about to call fuse_main, %s \n", sfs_data->diskfile);
    fuse_stat = fuse_main(argc, argv, &sfs_oper, sfs_data);
    fprintf(stderr, "fuse_main returned %d\n", fuse_stat);

    return fuse_stat;
}
