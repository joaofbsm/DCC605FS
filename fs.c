/***************************************
* Author: Joao Francisco B. S. Martins *
*                                      *
*         joaofbsm@dcc.ufmg.br         *
***************************************/

#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/file.h>

#include "fs.h"

/************************
*   UTILITY FUNCTIONS   * 
************************/

int get_file_size(const char *fname) {
	int sz;
	FILE *fd = fopen(fname, "r");
	fseek(fd, 0L, SEEK_END);
	sz = ftell(fd);
	rewind(fd);
	fclose(fd);
	return sz;
}

void fs_write_data(struct superblock *sb, uint64_t pos, void *data) {

	/* DATA POSITIONS
	* 0 - Superblock
	* 1 - Root Inode
	* 2 - Root Nodeinfo
	* 3 - Freelist
	*/

	lseek(sb->fd, pos * sb->blksz, SEEK_SET);
	write(sb->fd, data, sb->blksz);
}

/************************
* FILE SYSTEM FUNCTIONS *
************************/

struct superblock * fs_format(const char *fname, uint64_t blocksize) {
	if(blocksize < MIN_BLOCK_SIZE) {
		errno = EINVAL;
		return NULL;
	}

	struct superblock *sb     = malloc(sizeof *sb);
	struct inode *rootnode    = malloc(sizeof blocksize);
	struct nodeinfo *rootinfo = malloc(sizeof blocksize);
	struct freepage *freepage = malloc(sizeof blocksize);

	rootnode->mode   = IMDIR;
	rootnode->parent = 1;
	rootnode->meta   = 2;
	rootnode->next   = 0;

	rootinfo->size = 0;
	strcpy(rootinfo->name, "/");

	sb->magic    = 0xdcc605f5;
	sb->blks     = get_file_size(fname) / blocksize;
	sb->blksz    = blocksize;
	sb->freeblks = sb->blks - 4;
	sb->freelist = 3;
	sb->root     = 1;
	sb->fd       = open(fname, O_RDWR);

	fs_write_data(sb, 0, (void*) sb);
	fs_write_data(sb, 1, (void*) rootnode);
	fs_write_data(sb, 2, (void*) rootinfo);

	for(uint64_t i = 3; i < sb->blks; i++) {
		if(i + 1 == sb->blks) {
			freepage->next = 0;
		}
		else {
			freepage->next = i + 1;
		}

		freepage->count = 0;
		fs_write_data(sb, i, (void*) freepage);
	}

	if(sb->blks < MIN_BLOCK_COUNT) {
		close(sb->fd);
		free(sb);
		errno = ENOSPC;
		return NULL;
	}

	return sb;
}

struct superblock * fs_open(const char *fname) {

}

int fs_close(struct superblock *sb) {
	if(sb->magic != 0xdcc605f5) {
		errno = EBADF;
		return -1;
	}

	flock(sb->fd, LOCK_UN);
	close(sb->fd);
	free(sb);

	return 0;
}

uint64_t fs_get_block(struct superblock *sb) {

}

int fs_put_block(struct superblock *sb, uint64_t block) {

}

int fs_write_file(struct superblock *sb, const char *fname, char *buf, size_t cnt) {

}

ssize_t fs_read_file(struct superblock *sb, const char *fname, char *buf, size_t bufsz) {

}

int fs_unlink(struct superblock *sb, const char *fname) {

}

int fs_mkdir(struct superblock *sb, const char *dname) {

}

int fs_rmdir(struct superblock *sb, const char *dname) {

}

char * fs_list_dir(struct superblock *sb, const char *dname) {

}