#ifndef FS_H
#define FS_H

union fs_block;

int  fs_format();
void fs_debug();
int  fs_mount();

int  fs_create();
int  fs_delete( int inumber );
int  fs_getsize();

int  fs_read( int inumber, char *data, int length, int offset );
int  fs_write( int inumber, const char *data, int length, int offset );

int find_free_block(int size);
int find_pointer(union fs_block *p_block);
int create_inode_table(int ninodes);
void write_inode_table_to_disk(int ninodeblocks, int ninodes);

#endif
