#include "ext2_fs.h"
#include "read_ext2.h"
#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

void inode_file(int, struct ext2_inode, char *);
void inode_dir_filename(int, __u32, struct ext2_inode);
void single_handler(int, int, int);
void double_handler(int, int, int);
__u8 get_offset(__u8);

char *directory;
int no_inode = -1;

int main(int argc, char **argv) 
{
    if (argc != 3) 
    {
        printf("expected usage: ./runscan inputfile outputfile\n");
        exit(0);
    }

    directory = argv[2];
    // Open a new output directory
    if (opendir(argv[2])) {
        // If directory exists, print error
        perror("The output directory already exists");
        exit(0);
    } 
    int dir = mkdir(argv[2], 0777);
    if (dir != 0) {
        perror("Fail to create a new directory\n");
        exit(0);
    }

    int fd;
    fd = open(argv[1], O_RDONLY);    /* open disk image */

    ext2_read_init(fd);

    struct ext2_super_block super;
    struct ext2_group_desc group;

    // example read first the super-block and group-descriptor
    read_super_block(fd, 0, &super);
    read_group_desc(fd, 0, &group);
;

  
    for (unsigned int i = 0; i < num_groups; i++) {
        struct ext2_group_desc group;
        read_group_desc(fd, i, &group);
        // traverse each block group, get its inode table
        __u32 inode_start_addr = locate_inode_table(i, &group);
        
        struct ext2_inode inode;

        for (unsigned int j = 1; j < inodes_per_group + 1; j++) {
            no_inode = i * inodes_per_group + j;
            
            // Get each inode
            read_inode(fd, inode_start_addr, no_inode, &inode, super.s_inode_size);
            
            //no_inode = i * inodes_per_group + j;
            if (S_ISREG(inode.i_mode)) {
                inode_file(fd, inode, NULL);
            } else if (S_ISDIR(inode.i_mode)) {
                inode_dir_filename(fd, inode_start_addr, inode);
            }
        }
    }

    return 0;
}

void inode_dir_filename(int fd, __u32 inode_start_addr, struct ext2_inode inode) {
    // If it is a directory
    char data_block[1024];
    lseek(fd, BLOCK_OFFSET(inode.i_block[0]), SEEK_SET);
    read(fd, data_block, 1024);
    // The first element of data_block is the pointer
    // to directory entry content
    struct ext2_dir_entry_2 *dir_entry = malloc(sizeof(struct ext2_dir_entry_2));
    dir_entry = (struct ext2_dir_entry_2 *)(data_block);
    
    __u16 stripe = get_offset(dir_entry->name_len);
    
    for (__u16 j = 0; j < inode.i_size; j += stripe) {
        
        dir_entry = (struct ext2_dir_entry_2 *)(&data_block[j]);
        if (data_block[j] <= 0 || dir_entry->inode <= 0) 
            break;

        struct ext2_inode temp_inode;
        read_inode(fd, inode_start_addr, (dir_entry->inode * 2 - 1) % inodes_per_group, &temp_inode, 
            sizeof(struct ext2_inode));

        // If the inode number matches
        // parse the directory entry
        int name_len = dir_entry->name_len & 0xFF;  // convert to 4 bytes
        char name[EXT2_NAME_LEN];
        strncpy(name, dir_entry->name, name_len);
        name[name_len] = '\0';

        // write to this output file
        inode_file(fd, temp_inode, name);
        
        stripe = get_offset(dir_entry->name_len);
    }
}

__u8 get_offset(__u8 name_len) {
    __u8 real_len = 8;
    if (name_len % 4 != 0) {
        real_len += name_len + (4 - (name_len % 4));
    } else {
        real_len += name_len;
    }

    return real_len;
}

void inode_file(int fd, struct ext2_inode inode, char *fullname) {
    // check if it is jpg
    char *buffer = malloc(1024);
    lseek(fd, BLOCK_OFFSET(inode.i_block[0]), SEEK_SET);
    read(fd, buffer, 1024);

    if (buffer[0] == (char)0xff &&
        buffer[1] == (char)0xd8 &&
        buffer[2] == (char)0xff &&
        (buffer[3] == (char)0xe0 ||
        buffer[3] == (char)0xe1 ||
        buffer[3] == (char)0xe8)) 
    {
        // It is a regular jpg file
        // Copy the content of this file to an output file
        // using the inode number as the file name
        int fd2;
        if (fullname == NULL) {
            char new_path_name[100];
            snprintf(new_path_name, sizeof(new_path_name), "./%s/file-%d.jpg", directory, no_inode);
            fd2 = open(new_path_name, O_CREAT | O_RDWR, 0777);
        } else {
            char new_path_name[100];
            snprintf(new_path_name, sizeof(new_path_name), "./%s/%s", directory, fullname);
            fd2 = open(new_path_name, O_CREAT | O_RDWR, 0777);
        }

        // Read each data block
        for (int k = 0; k < 14; k++) {
            int block = inode.i_block[k];

            //printf("Current block is %d\n", block);
            if (block <= 0) {
                continue;
            }

            if (k <= 11) {
                // direct pointers
                // write the data to the output file
                char buffer2[1024];
                lseek(fd, BLOCK_OFFSET(block), SEEK_SET);
                read(fd, buffer2, 1024);
                write(fd2, buffer2, 1024);
                
            } else if (k == 12) {
                // indirect pointers
                single_handler(block, fd, fd2);
            } else if (k == 13) {
                // double indirect pointers5253242
                double_handler(block, fd, fd2);
            }
            //blocks++;
        }    

        // Write the details of this file: links count; file size; owner user id
        if (fullname == NULL) {
            __u32 s_file = inode.i_size;
            __u16 uid = inode.i_uid;
            __u16 links = inode.i_links_count;
            //printf("file size: %d\n uid: %d\n links: %d\n", s_file, uid, links);
            char path_name[100];
            snprintf(path_name, sizeof(path_name), "./%s/file-%d-details.txt", directory, no_inode);
            FILE *fd3 = fopen(path_name, "w");
            // Write to the file
            char buffer[14];
            snprintf(buffer, sizeof(buffer), "%hu\n%u\n%hu", links, s_file, uid);
            //char str[] = 
            fprintf(fd3, "%hu\n", links);
            fprintf(fd3, "%u\n", s_file);
            fprintf(fd3, "%hu", uid);
            
            fclose(fd3);
        }
    }
}

void single_handler(int block, int fd, int fd2) {
    //printf("single handler being executed\n");
    char buffer3[1024];// = malloc(1024);
    lseek(fd, BLOCK_OFFSET(block), SEEK_SET);
    read(fd, buffer3, 1024);
    for (int p = 0; p < 1024; p += 4) {
        __u32 direct_pointer = *(__u32*)(&buffer3[p]);
        if (direct_pointer == 0)
            break;
        char buffer4[1024];
        lseek(fd, BLOCK_OFFSET(direct_pointer), SEEK_SET);
        read(fd, buffer4, 1024);
        write(fd2, buffer4, 1024);
    }
}

void double_handler(int block, int fd, int fd2) {
    char buffer[1024];// = malloc(1024);
    lseek(fd, BLOCK_OFFSET(block), SEEK_SET);
    read(fd, buffer, 1024);

    for (int i = 0; i < 1024; i += 4) {
        __u32 pointer = *(__u32*)(&buffer[i]);
        single_handler(pointer, fd, fd2);
    }
}

