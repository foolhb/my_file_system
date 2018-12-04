//
// Created by bh398 on 12/1/18.
//

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

#ifdef HAVE_SYS_XATTR_H
#include <sys/xattr.h>
#endif

#include "log.h"
#include "sfs.h"
#include "sfs_helper_functions.h"


superblock *sb = NULL;
int block_cur = 1;

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
                    update_bitmap(ret,DATA_BITMAP_UPDATE);
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

