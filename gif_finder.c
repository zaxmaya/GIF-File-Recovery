#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/hdreg.h>
#include <arpa/inet.h>
#include <string.h>

#define SIZE_OF_MBR 512
#define START_PARTITION_DATA 446
#define PARTITION_ENTRY_SIZE 16
#define LBA_START_OFFSET 8
#define SECTOR_COUNT_OFFSET 12
#define BLOCK_SIZE_OFFSET 24
#define TOTAL_BLOCKS_OFFSET 32
#define BLOCKS_PER_GROUP_OFFSET 40
#define NUM_GROUPS_OFFSET 64
#define TOTAL_INODES_OFFSET 0x54
#define INODES_PER_GROUP_OFFSET 0x68

// GIF signature constants
#define GIF_SIGNATURES_SIZE 2
#define GIF_SIGNATURE_LENGTH 6

// Known GIF signatures (GIF87a and GIF89a)
const char *GIF_SIGNATURES[GIF_SIGNATURES_SIZE] = {"GIF87a", "GIF89a"};

void get_inode_info(unsigned long block_number, const char *device);
void recover_gif(unsigned long inode_number, const char *device);

// Function to search for GIF files in the given device by scanning blocks
void searchForGif(int fd, unsigned long blockCount, unsigned int blockSize, const char *device) {
    unsigned char *blockData = (unsigned char *)malloc(blockSize);
    if (!blockData) {
        fprintf(stderr, "Memory allocation failed\n");
        return;
    }

    for (unsigned long i = 0; i < blockCount; i++) {
        ssize_t bytesRead = pread(fd, blockData, blockSize, blockSize * i);
        if (bytesRead != blockSize) {
            fprintf(stderr, "Failed to read block: bytesRead = %ld\n", bytesRead);
            continue;
        }

        for (int j = 0; j < GIF_SIGNATURES_SIZE; ++j) {
            if (memcmp(blockData, GIF_SIGNATURES[j], GIF_SIGNATURE_LENGTH) == 0) {
                printf("GIF found in block %lu\n", i);
                get_inode_info(i, device);
                break;
            }
        }
    }

    free(blockData);
}

// Function to retrieve partition information from the MBR and read the superblock
void getPartAddr(int fd, const char *device) {
    unsigned char mbr[SIZE_OF_MBR];
    ssize_t bytesRead;

    bytesRead = read(fd, mbr, SIZE_OF_MBR);
    if (bytesRead != SIZE_OF_MBR) {
        fprintf(stderr, "Failed to read MBR: bytesRead = %ld\n", bytesRead);
        return;
    }

    for (int i = 0; i < 4; ++i) {
        int entryOffset = START_PARTITION_DATA + i * PARTITION_ENTRY_SIZE;
        unsigned long lbaStart = 0;
        unsigned long sectorCount = 0;

        for (int j = 0; j < 4; ++j) {
            lbaStart |= (unsigned long)mbr[entryOffset + LBA_START_OFFSET + j] << (8 * j);
            sectorCount |= (unsigned long)mbr[entryOffset + SECTOR_COUNT_OFFSET + j] << (8 * j);
        }

        unsigned long byteAddress = lbaStart * 512;
        off_t partitionAddr = byteAddress;
        lseek(fd, partitionAddr + 1024, SEEK_SET);

        unsigned char superblock[4096];
        bytesRead = read(fd, superblock, 4096);
        if (bytesRead != 4096) {
            fprintf(stderr, "Failed to read superblock: bytesRead = %ld\n", bytesRead);
            return;
        }

        unsigned int blockSize = 1024 << superblock[BLOCK_SIZE_OFFSET];
        unsigned int totalBlocks = *(unsigned int *)&superblock[TOTAL_BLOCKS_OFFSET];

        // Search for GIF in the current partition
        searchForGif(fd, totalBlocks, blockSize, device);
    }
}

// Function to retrieve the inode information for a given block number
void get_inode_info(unsigned long block_number, const char *device) {
    char command[256];
    sprintf(command, "sudo debugfs -R \"icheck %lu\" %s", block_number, device);
    FILE *fp = popen(command, "r");
    if (fp == NULL) {
        fprintf(stderr, "Failed to run command\n");
        return;
    }

    unsigned long inode_number;
    if (fscanf(fp, "Block\tInode number\n%lu\t%lu", &block_number, &inode_number) == 2) {
        printf("Inode %lu for block %lu\n", inode_number, block_number);
    } else {
        fprintf(stderr, "Failed to parse icheck output\n");
        pclose(fp);
        return;
    }
    pclose(fp);

    // Fetch inode details
    sprintf(command, "sudo debugfs -R \"stat <%lu>\" %s", inode_number, device);
    fp = popen(command, "r");
    if (fp == NULL) {
        fprintf(stderr, "Failed to run command\n");
        return;
    }

    // Parse the stat command output
    recover_gif(inode_number, device);

    pclose(fp);
}

// Function to recover the GIF file using inode information
void recover_gif(unsigned long inode_number, const char* device) {
    static int file_counter = 0;
    char command[1024];
    sprintf(command, "sudo debugfs -R \"stat <%lu>\" %s", inode_number, device);
    FILE *fp = popen(command, "r");
    if (!fp) {
        fprintf(stderr, "Failed to open debugfs for inode info.\n");
        return;
    }

    char line[1024];
    int total_blocks = 0;
    unsigned long seek_offset = 0;
    int parsing_blocks = 0;

    // Prepares a script to store recovery commands
    FILE *cmd_file = fopen("recovery_commands.sh", "w");
    if (!cmd_file) {
        fprintf(stderr, "Failed to create command file.\n");
        pclose(fp);
        return;
    }
    fprintf(cmd_file, "#!/bin/bash\n");

    int used_skips[1000] = {0};
    int used_skip_count = 0;

    // Parse the inode information to get block addresses
    while (fgets(line, sizeof(line), fp)) {
        if (strstr(line, "BLOCKS:")) {
            parsing_blocks = 1;  
            continue;
        }

        if (parsing_blocks && line[0] != '\n') {
            char* token = strtok(line, ", ");
            while (token) {
                if (strstr(token, "(IND)")) {  // Skip (IND) blocks
                    token = strtok(NULL, ", ");
                    continue;
                }

                int start_block, end_block;
                sscanf(token, "(%*[^)]):%d-%d", &start_block, &end_block);
                int count = end_block - start_block + 1;
                total_blocks += count;

                // Check if skip has already been used
                int skip_already_used = 0;
                for (int i = 0; i < used_skip_count; i++) {
                    if (used_skips[i] == start_block) {
                        skip_already_used = 1;
                        break;
                    }
                }

                if (!skip_already_used) {
                    fprintf(cmd_file, "sudo dd if=%s of=recovery_%d.gif bs=4096 skip=%d seek=%ld count=%d conv=notrunc\n",
                            device, file_counter, start_block, seek_offset, count);
                    seek_offset += count;  // Update seek offset for next dd command

                    // Mark this skip as used
                    used_skips[used_skip_count++] = start_block;
                }

                token = strtok(NULL, ", ");
            }
            continue;
        }

        if (line[0] == '\n' && parsing_blocks) {  // End of block section
            break;
        }
    }
    pclose(fp);
    fclose(cmd_file);

    // Initialize the recovery file
    char init_cmd[256];
    sprintf(init_cmd, "sudo dd if=/dev/zero of=recovery_%d.gif bs=4096 count=%d", file_counter, total_blocks - 1); // Adjusted total_blocks to total_blocks - 1 as per your requirement
    system(init_cmd); 

    // Execute recovery commands
    system("bash recovery_commands.sh");
    file_counter++;
}


int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <device>\n", argv[0]);
        return EXIT_FAILURE;
    }

    int fd = open(argv[1], O_RDONLY);
    if (fd == -1) {
        perror("Failed to open device");
        return EXIT_FAILURE;
    }

    getPartAddr(fd, argv[1]);
    close(fd);

    return EXIT_SUCCESS;
}

