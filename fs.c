/*
ERROR when creating inode after writing
*/

#include "fs.h"
#include "disk.h"

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>

#define FS_MAGIC           0x30341003
#define INODES_PER_BLOCK   128
#define POINTERS_PER_INODE 3
#define POINTERS_PER_BLOCK 1024

struct fs_inode *inode_table = 0; //inode table
bool *bitmap = 0;				  //Bitmap
int is_mounted = 0;			  //is the disk mounted?
extern struct disk *thedisk;

struct fs_superblock {
	int32_t magic;
	int32_t nblocks;
	int32_t ninodeblocks;
	int32_t ninodes;
};

struct fs_inode {
	int32_t isvalid;
	int32_t size;
	int64_t ctime;
	int32_t direct[POINTERS_PER_INODE];
	int32_t indirect;
};

union fs_block {
	struct fs_superblock super;
	struct fs_inode inode[INODES_PER_BLOCK];
	int pointers[POINTERS_PER_BLOCK];
	unsigned char data[BLOCK_SIZE];
};

int fs_format()
{
	//If disk is mounted exit
	if(is_mounted){return 0;}

	//Check if nblocks is <= 2
   	if( disk_nblocks(thedisk) <= 2){return 0;}

	//Variables
	union fs_block block;
	memset(&block, 0, sizeof(block));
	int i, j;
	int counter = 0;

	//Write Superblock
	block.super.nblocks = disk_nblocks(thedisk);
	// ERROR CHECK FOR INPUTTED NUMBER HERE
	block.super.ninodeblocks = ceil(.1 * block.super.nblocks);
	block.super.magic = FS_MAGIC;
	block.super.ninodes = INODES_PER_BLOCK * block.super.ninodeblocks;
	disk_write(thedisk, 0, block.data);

    //Create variables
    int ninodeblocks = block.super.ninodeblocks;
	int ninodes = block.super.ninodes;
	
	//Create inode table
	if(create_inode_table(block.super.ninodes) == 0){return 0; }

	//Zero the block
	memset(&block, 0, sizeof(block));
	
	//Write inode table to disk
	for(i = 1; i < ninodeblocks + 1; i++){
		for(j = 0; j < INODES_PER_BLOCK; j++){	
			if(counter > ninodes){continue;}
			block.inode[j] = inode_table[counter];
			counter++;
		}
		disk_write(thedisk, i, block.data);
	}

	return 1;
}

void fs_debug()
{
	//Check if disk is mounted
	if(!is_mounted){
		printf("Disk is not mounted yet.\n");
		return;
	}

	//Init blocks
	union fs_block superblock;
	disk_read(thedisk, 0, superblock.data);
	union fs_block datablock;
	memset(&datablock, 0, sizeof(datablock));

	//Print superblock
	printf("superblock:\n");
	printf("%6d blocks\n", superblock.super.nblocks);
	printf("%6d inode blocks\n", superblock.super.ninodeblocks);
	printf("%6d inodes\n", superblock.super.ninodes);

	int i, j;
	for(i = 1; i < superblock.super.ninodes; i++){
		if(inode_table[i].isvalid == 1){
			//Print Inode
			printf("Inode %d:\n", i);
			printf("	size: %d bytes\n", inode_table[i].size);
			printf("	created: %s", ctime(&inode_table[i].ctime));
			printf("	direct blocks:");
			for(j = 0; j < POINTERS_PER_INODE; j++){
				if(inode_table[i].direct[j] == 0){continue;}
				printf(" %d", inode_table[i].direct[j]);
			}
			printf("\n");

			//If indirect is null, skip the rest
			if(inode_table[i].indirect == 0){continue;}

			//Print indirect block
			memset(&datablock, 0, sizeof(datablock));
			disk_read(thedisk, inode_table[i].indirect, datablock.data);
			printf("	indirect block: %d\n", inode_table[i].indirect);
			printf("	indirect data blocks:");
			for(j = 0; j < POINTERS_PER_BLOCK; j++){
				if(datablock.pointers[j] == 0){continue;}
				printf(" %d", datablock.pointers[j]);
			}
			printf("\n");
		}
	}
}

int fs_mount() 
{
   //If disk is already mounted return
   if(is_mounted){return 0;}
   
   //Check if nblocks is <= 2
   	if( disk_nblocks(thedisk) <= 2){return 0;}

	//get info from the filesystem and create variables
	union fs_block block;
    disk_read(thedisk, 0, block.data);

	//If file not formatted, return
	if(block.super.magic != FS_MAGIC){return 0;}
	if(block.super.nblocks != disk_nblocks(thedisk)){return 0;}
	if(block.super.ninodeblocks <= 0){return 0;}
	if(block.super.ninodes <= 0){return 0;}
	if(block.super.ninodeblocks * INODES_PER_BLOCK != block.super.ninodes){return 0;}

	//Variables
    int ninodeblocks = block.super.ninodeblocks;
	int ninodes = block.super.ninodes;
    int nblocks = block.super.nblocks;
	int i, j, counter = 0;

	//build a free block bitmap
	bitmap = malloc(sizeof(bool) * nblocks);
	if(bitmap == 0){return 0;}
	for(i = 0; i < nblocks; i++){
		bitmap[i] = 0;
	}

	//Create inode table if it is not created already
	if(inode_table == 0){
		if(create_inode_table(block.super.ninodes) == 0){return 0; }
	}

	//Read inode table from disk
	for(i = 1; i < ninodeblocks + 1; i++){ //read in all of the inodes (need to skip the initial block)
		memset(&block, 0, sizeof(block)); //zero the contents of block
		disk_read(thedisk,i,block.data); //read in the inode block from disk
		for(j = 0; j < INODES_PER_BLOCK; j++){
			if(counter > ninodes){continue;}
			inode_table[counter] = block.inode[j]; //fill the inode table with values from disk
			counter++;
		}
		bitmap[i] = 1;
	}

	//Set superblock bit
	bitmap[0] = 1;

	//now that we have all of the inodes, we need to scan through them to find which data blocks are free
	for(i = 1; i < ninodes; i++){
		if(inode_table[i].isvalid == 1){ //if the inode is valid
			for(j = 0; j < POINTERS_PER_INODE; j++){
				if(inode_table[i].direct[j] != 0){ //if direct data pointer is not null
					bitmap[inode_table[i].direct[j]] = 1; //set the corresponding location in the bitmap to in use
				}
			}
			if(inode_table[i].indirect != 0){ //if indirect block pointer is not null
				bitmap[inode_table[i].indirect] = 1; //Update bitmap
				memset(&block, 0, sizeof(block)); //zero the contents of block
				disk_read(thedisk, inode_table[i].indirect, block.data); //read in the indirect block from disk

				for(j = 0; j < POINTERS_PER_BLOCK; j++){
					if(block.pointers[j] != 0){ //if indirect data pointer is not null
						bitmap[block.pointers[j]] = 1; //set the corresponding location in the bitmap to in use
					}	
				}
			}
		}
	}

    is_mounted = 1;
	return 1;
}

int fs_create()
{
	//Check if disk is mounted
	if(!is_mounted){return 0;}

	//Variables
	int i = 1, j = 0;
	union fs_block block;
    disk_read(thedisk, 0, block.data);
	
	//Find an open inode
	while(i < block.super.ninodes){
		if(inode_table[i].isvalid == 0){
			//Update data
			inode_table[i].isvalid = 1;
			inode_table[i].size = 0;
			inode_table[i].ctime = time(NULL);
			for(j = 0; j < POINTERS_PER_INODE; j++){inode_table[i].direct[j] = 0;}
			inode_table[i].indirect = 0;

			//Write to disk
			write_inode_table_to_disk(block.super.ninodeblocks, block.super.ninodes);

			return i;
		}
		i++;
	}

	return 0;
}

int fs_delete( int inumber )
{
	//Check if disk is mounted
	if(!is_mounted){return 0;}

	//Create variables and check if inode exists
	if(inumber < 0){return 0;}
	int i = 0;
	union fs_block block;
	disk_read(thedisk, 0, block.data);
	if(inumber > block.super.ninodes){return 0;}
	if(inode_table[inumber].isvalid == 0){return 0;}

	//Free direct blocks
	for(i = 0; i < POINTERS_PER_INODE; i++){
		if(inode_table[inumber].direct[i] != 0){
			bitmap[inode_table[inumber].direct[i]] = 0;
			inode_table[inumber].direct[i] = 0;
		}
	}

	//Zero other information
	inode_table[inumber].size = 0;
	inode_table[inumber].ctime = 0;

	//If there is no indirect pointer exit
	if(inode_table[inumber].indirect != 0){

		//Fill up block
		memset(&block, 0, sizeof(block)); 
		disk_read(thedisk, inode_table[inumber].indirect, block.data); 
		
		//Free indirect block pointers
		for(i = 0; i < POINTERS_PER_BLOCK; i++){
			if(block.pointers[i] != 0){
				bitmap[block.pointers[i]] = 0;
			} 
		}
		
		//Update bitmap and indirect pointer
		bitmap[inode_table[inumber].indirect] = 0;
		inode_table[inumber].indirect = 0; 
	}

	//Invalidate inode
	inode_table[inumber].isvalid = 0;
	
	//Read superblock to block
	memset(&block, 0, sizeof(block));
	disk_read(thedisk, 0, block.data);

	//Write to disk and return
	write_inode_table_to_disk(block.super.ninodeblocks, block.super.ninodes);
	return 1;
}

int fs_getsize( int inumber )
{
	//Check if disk is mounted
	if(!is_mounted){return -1;}	

	//Make sure inumber exists
	union fs_block superblock;
    disk_read(thedisk, 0, superblock.data);
	if(inumber < 0){return -1;}
	if(inumber > superblock.super.ninodes){return -1;}

	//Check if its valid
	if(inode_table[inumber].isvalid == 0){return -1;}
	else{return inode_table[inumber].size;}
}

int fs_read( int inumber, char *data, int length, int offset )
{
	//Check if disk is mounted
	if(!is_mounted){return 0;}
	union fs_block block;
	memset(&block, 0, sizeof(block));
	disk_read(thedisk, 0, block.data);
	if(inumber > block.super.ninodes){return 0;} //check if the inumber exists
	if(inumber < 0){return 0;}
	
	//Create variables
	static int curr_block = 0; //block that we are on for copying out
	if(offset == 0){curr_block = 0;} //if offset is 0 (first time called on an inode) make the current block 0
	int inode_size = fs_getsize(inumber);
	if(inode_size <= 0){return 0;}
	const int buff_size = sizeof(union fs_block);
	unsigned char buff[buff_size]; //For data
	int data_index = 0; //amount of data read, also the index of *data
	int buff_index = 0; //index for the buffer

	if(offset > inode_size){return 0;} //if the offset is greater than the size of the file, quit

	//first read from direct blocks, assuming that we need to
	if((offset < POINTERS_PER_INODE*buff_size)){ //if the offset is less than the total size of the direct blocks, we need to read from them
		while(curr_block < POINTERS_PER_INODE){
			if(inode_table[inumber].direct[curr_block] != 0){
				memset(buff, 0, buff_size);
				disk_read(thedisk, inode_table[inumber].direct[curr_block], block.data);
				memcpy(buff, block.data, buff_size);
				if(buff != 0){
					while((buff_index < buff_size) && (data_index < length)){ //extra check for length (might be a better way to do this)
						data[data_index] = buff[buff_index];

						if(buff[buff_index] == 0){break;}
						
						buff_index++;
						data_index++;
					}
				}
				else{break;}
			}

			curr_block++;
			buff_index = 0;
		}
	}

	if(inode_table[inumber].indirect != 0 && (data_index < length)){ //if there is an indirect pointer, we need to read from it
		union fs_block p_block; //block for the indirect pointer to be read into
		disk_read(thedisk, inode_table[inumber].indirect, p_block.data);
		while((curr_block - POINTERS_PER_INODE < POINTERS_PER_BLOCK) && (data_index < length)){ //i think this is the condition that works
			if(p_block.pointers[curr_block - POINTERS_PER_INODE] != 0){
				memset(buff, 0, buff_size);
				disk_read(thedisk, p_block.pointers[curr_block - POINTERS_PER_INODE], block.data);
				memcpy(buff, block.data, buff_size);
				if(buff != 0){
					while((buff_index < buff_size) && (data_index < length)){
						data[data_index] = buff[buff_index];
						// if we are at the end of the file break
						if(buff[buff_index] == 0){break;}

						buff_index++;
						data_index++;
					}
				}
				else{break;}
			}
			curr_block++;
			buff_index = 0;
		}
	}

	return data_index;
}

int fs_write( int inumber, const char *data, int length, int offset )
{
	//Check if disk is mounted
	if(!is_mounted){return -1;}

	//Create blocks
	union fs_block superblock;
	memset(&superblock, 0, sizeof(superblock));
	disk_read(thedisk, 0, superblock.data);
	if(inumber > superblock.super.ninodes){return -1;} //check if the inumber exists
	if(inumber < 0){return -1;}
	union fs_block datablock;
	memset(&datablock, 0, sizeof(datablock));
	union fs_block p_block;
	memset(&p_block, 0, sizeof(p_block));
	
	//Create variables
	if(inode_table[inumber].isvalid == 0){return -1;}
	static int curr_block = 0; //block that we are on for writing
	if(offset == 0){curr_block = 0;} //if offset is 0 (first time called on an inode) make the current block 0
	int inode_size = fs_getsize(inumber);
	if((offset == 0) && (inode_size != 0)){
		if(fs_delete(inumber) == 0){return 0;}
		inode_table[inumber].isvalid = 1;
	}
	const int buff_size = sizeof(union fs_block);
	int data_index = 0; //amount of data read, also the index of *data
	int buff_index = 0; //index for the buffer
	int free_block = 0;
	int free_pointer = 0;
	
	if((offset < POINTERS_PER_INODE*buff_size)){
		while(curr_block < POINTERS_PER_INODE){
			free_block = find_free_block(superblock.super.nblocks);
			if(free_block < 0){return data_index;}
			memset(&datablock, 0, sizeof(datablock));
			while((buff_index < buff_size) && (data_index < length)){
				datablock.data[buff_index] = data[data_index];
				if(datablock.data[buff_index] == 0){break;} 
				buff_index++;
				data_index++;
			}
			bitmap[free_block] = 1;
			inode_table[inumber].direct[curr_block] = free_block;
			curr_block++;
			inode_table[inumber].size += buff_index;
			buff_index = 0;
			disk_write(thedisk, free_block, datablock.data);
			write_inode_table_to_disk(superblock.super.ninodeblocks, superblock.super.ninodes);
		}
	}


	if(data_index < length){
		if(inode_table[inumber].indirect == 0){
			free_block = find_free_block(superblock.super.nblocks);
			if(free_block < 0){return data_index;}
			bitmap[free_block] = 1;
			inode_table[inumber].indirect = free_block;
			write_inode_table_to_disk(superblock.super.ninodeblocks, superblock.super.ninodes);
		}
		else{
			disk_read(thedisk, inode_table[inumber].indirect, p_block.data);
		}
		
		while((curr_block - POINTERS_PER_INODE < POINTERS_PER_BLOCK) && (data_index < length)){
			free_pointer = find_pointer(&p_block);
			if(free_pointer < 0){return data_index;}
			free_block = find_free_block(superblock.super.nblocks);
			if(free_block < 0){return data_index;}

			memset(&datablock, 0, sizeof(datablock));
			while((buff_index < buff_size) && (data_index < length)){
				datablock.data[buff_index] = data[data_index];
				if(datablock.data[buff_index] == 0){break;} 
				buff_index++;
				data_index++;
			}
			bitmap[free_block] = 1;
			curr_block++;
			p_block.pointers[free_pointer] = free_block;
			inode_table[inumber].size += buff_index;
			buff_index = 0;
			disk_write(thedisk, inode_table[inumber].indirect, p_block.data);
			write_inode_table_to_disk(superblock.super.ninodeblocks, superblock.super.ninodes);
			disk_write(thedisk, free_block, datablock.data);
		}
	}

	return data_index;
}

int find_free_block(int size){
	int i = 0;
	for(i = 0; i < size; i++){
		if(bitmap[i] == 0){
			return i;
		}
	}

	return -1;
}

int find_pointer(union fs_block *p_block){
	int i = 0;
	for(i = 0; i < POINTERS_PER_BLOCK; i++){
		if(p_block->pointers[i] == 0){
			return i;
		}
	}

	return -1;
}

int create_inode_table(int ninodes){
	//Variables
	int i, j;

	//Malloc and check for erros
	inode_table = malloc(sizeof(struct fs_inode) * ninodes);
	if(inode_table == 0){return 0;}

	//Initialize
	for(i = 0; i < ninodes; i++){
		inode_table[i].isvalid = 0;
		inode_table[i].size = 0;
		inode_table[i].ctime = 0;
		for(j = 0; j < POINTERS_PER_INODE; j++){inode_table[i].direct[j] = 0;}
		inode_table[i].indirect = 0;
	}

	return 1;
}

void write_inode_table_to_disk(int ninodeblocks, int ninodes){
	//variables
	union fs_block block;
	memset(&block, 0, sizeof(block));
	int i = 0, j = 0, counter = 0;

	//Loop through table and write to disk
	for(i = 1; i < ninodeblocks + 1; i++){
		for(j = 0; j < INODES_PER_BLOCK; j++){	
			if(counter > ninodes){continue;}
			block.inode[j] = inode_table[counter];
			counter++;
		}
		disk_write(thedisk, i, block.data);
	}
}