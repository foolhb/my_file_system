//
// Created by bh398 on 12/1/18.
//

#ifndef ASSIGNMENT3_SFS_HELPER_FUNCTIONS_H
#define ASSIGNMENT3_SFS_HELPER_FUNCTIONS_H

#endif //ASSIGNMENT3_SFS_HELPER_FUNCTIONS_H


#define INODE_BITMAP_UPDATE 0
#define DATA_BITMAP_UPDATE 1


void update_bitmap(unsigned int index, unsigned int mode);

void directory_block_init(unsigned int block_id, unsigned int inum, unsigned int parent_inum);

inode* resolute_path(char *path, inode *current_dir);

unsigned int assign_block();

unsigned int assign_inode_number();

