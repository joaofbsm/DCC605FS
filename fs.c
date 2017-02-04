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

#define LINK_LIMIT (sb->blksz - 32) / sizeof(uint64_t)
#define MAX_NAME_LENGTH sb->blksz - (8 * sizeof(uint64_t)

/************************
*       UTILITIES       * 
************************/

struct link {
  uint64_t inode;
  int index;
};

struct dir {
	uint64_t dirnode;
	uint64_t filenode;
	char *filename;
};


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
	* 3 - Freelist(Initial position)
	*/

	lseek(sb->fd, pos * sb->blksz, SEEK_SET);
	write(sb->fd, data, sb->blksz);
}

void fs_read_data(struct superblock *sb, uint64_t pos, void *data) {
	lseek(sb->fd, pos * sb->blksz, SEEK_SET);
	read(sb->fd, data, sb->blksz);
}

/* Returns the name of the last inode, its parent dir inode position and its inode position(if it doesnt exists returns -1). In case of error returns NULL */
struct dir * fs_find_dir_info(struct superblock *sb, const char *dpath) {
	int pathlenght = 0;
	char *pathcopy, *token, *filename;

	strcpy(pathcopy, dpath);

	token = strtok(pathcopy, "/");
	while(token != NULL) {
		strcpy(filename, token);
		pathlenght++;
		token = strtok(NULL, "/");
	} 

	strcpy(pathcopy, dpath);

	int iterations;
	uint64_t dirnode, filenode, j;
	struct dir *dir              = malloc(sizeof *dir); 
	struct inode *inode          = malloc(sb->blksz);
	struct inode *auxinode       = malloc(sb->blksz);
	struct nodeinfo *nodeinfo    = malloc(sb->blksz);
	struct nodeinfo *auxnodeinfo = malloc(sb->blksz);

	dirnode = 1;

	fs_read_data(sb, dirnode, (void*) inode);
	fs_read_data(sb, inode->meta, (void*) nodeinfo);
	token = strtok(pathcopy, "/");
	for(int i = 0; i < pathlenght; i++) {
		while(j < LINK_LIMIT) {
			filenode = inode->links[j];
			if(filenode != 0) {
				fs_read_data(sb, filenode, (void*) auxinode);
				fs_read_data(sb, auxinode->meta, (void*) auxnodeinfo);

				if(!strcmp(auxnodeinfo->name, token)) {
					if(i + 1 < pathlenght) dirnode = filenode;
					inode = auxinode;
					nodeinfo = auxnodeinfo;
					break;
				}	
			}

			j++;

			if(j == LINK_LIMIT) {
				if(inode->next != 0) { /* Restarts loop with child inode */
					j = 0;
					fs_read_data(sb, inode->next, (void*)inode);
				}
				else{ 
					if(i + 1 == pathlenght) {
						filenode = -1; /* Ends while without finding file */
					}
					else { /* Error: Path doesn't exists */
						return NULL;
					}
				}
			}
		}
		token = strtok(NULL, "/");
	}

	dir->dirnode = dirnode;
	dir->filenode = filenode;
	strcpy(dir->filename, filename);

	return dir;
}

/* Returns the inode relative to parent that have a free link and the free link index in the array of links.
 * If no free link is available, return the last inode in the chain and -1 as link index. */
struct link * fs_find_free_link(struct superblock *sb, struct inode *inode, uint64_t inodeblk) {
	int i = 0;
	uint64_t actualblk = inodeblk;
	struct link *link = malloc(sizeof *link);

	while(i < LINK_LIMIT) {
		if(inode->links[i] == 0) {
			link->inode = actualblk;
			link->index = i;
			break;
		}

		i++;

		if(i == LINK_LIMIT) {
			if(inode->next == 0) {
				link->inode = actualblk;
				link->index = -1;
				break;
			}
			else{ /* Restarts loop with child inode */
				i = 0;
				actualblk = inode->next;
				fs_read_data(sb, inode->next, (void*)inode);
			}
		}
	}

	return link;
}

/* Returns child inode pos in fs*/
uint64_t fs_create_child(struct superblock *sb, uint64_t thisblk, uint64_t parentblk) {
	struct inode *inode     = malloc(sb->blksz);
	struct inode *childnode = malloc(sb->blksz);

	fs_read_data(sb, thisblk, (void*) inode);

	inode->next = fs_get_block(sb);

	childnode->mode   = IMCHILD;
	childnode->parent = parentblk;
	childnode->meta   = thisblk;
	childnode->next   = 0;
	for(int i = 0; i < LINK_LIMIT; i++) {
		childnode->links[i] = 0;
	}

	fs_write_data(sb, thisblk, (void*) inode);
	fs_write_data(sb, inode->next, (void*) childnode);

	return inode->next;
}

void fs_add_link(struct superblock *sb, uint64_t parentblk, int linkindex, uint64_t newlink) {
	struct inode *inode = malloc(sb->blksz);

	fs_read_data(sb, parentblk, (void*) inode);

	inode->links[linkindex] = newlink;

	fs_write_data(sb, parentblk, (void*) inode);
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
	struct inode *rootnode    = malloc(blocksize);
	struct nodeinfo *rootinfo = malloc(blocksize);
	struct freepage *freepage = malloc(blocksize);

	rootnode->mode   = IMDIR;
	rootnode->parent = 1;
	rootnode->meta   = 2;
	rootnode->next   = 0;

	rootinfo->size = 0;
	strcpy(rootinfo->name, "/");

	sb->magic    = 0xdcc605f5;
	sb->blks     = get_file_size(fname) / blocksize;
	sb->blksz    = blocksize;
	sb->freeblks = sb->blks - 3;
	sb->freelist = 3;
	sb->root     = 1;
	sb->fd       = open(fname, O_RDWR);

	if(flock(sb->fd, LOCK_EX | LOCK_NB) == -1){
		errno = EBUSY;
		return NULL;
	}

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

	free(rootnode);
	free(rootinfo);
	free(freepage);

	if(sb->blks < MIN_BLOCK_COUNT) {
		close(sb->fd);
		free(sb);
		errno = ENOSPC;
		return NULL;
	}

	return sb;
}

struct superblock * fs_open(const char *fname) {
	struct superblock *sb = malloc(sizeof *sb);
	int fd = open(fname, O_RDWR);

	if(flock(fd, LOCK_EX | LOCK_NB) == -1){
		errno = EBUSY;
		return NULL;
	}

	read(fd, sb, sizeof *sb);
	sb->fd = fd;

	if(sb->magic != 0xdcc605f5) {
		errno = EBADF;
		return NULL;
	}

	return sb;
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
	if(sb->freeblks == 0) {
		return 0;
	}

	uint64_t ret;
	struct freepage *freepage = malloc(sb->blksz);

	fs_read_data(sb, sb->freelist, (void*) freepage);

	ret = sb->freelist;
	sb->freeblks--;
	sb->freelist = freepage->next;

	fs_write_data(sb, 0, (void*) sb);

	return ret;
}

int fs_put_block(struct superblock *sb, uint64_t block) {
	struct freepage *freepage = malloc(sb->blksz);

	freepage->next  = sb->freelist;
	freepage->count = 0;

	sb->freeblks++;
	sb->freelist = block;

	fs_write_data(sb, block, (void*) freepage);
	fs_write_data(sb, 0, (void*) sb);

	return 0;
}

int fs_write_file(struct superblock *sb, const char *fname, char *buf, size_t cnt) {
	int datablks, extrainodes, neededblks, links;
	uint64_t parentblk, fileblk, previousblk, linkblk;
	struct dir *dir;
	struct link *link;
	struct inode *inode       = malloc(sb->blksz);
	struct inode *childnode   = malloc(sb->blksz);
	struct inode *parentnode  = malloc(sb->blksz);
	struct nodeinfo *nodeinfo = malloc(sb->blksz);

	datablks = (cnt / sb->blksz) + ((cnt % sb->blksz) ? 1 : 0); /* Be careful with the size of int */
	extrainodes = 0;

	if(datablks > LINK_LIMIT) {
		extrainodes = (datablks / LINK_LIMIT) + (datablks % LINK_LIMIT ? 1 : 0);
	}

	dir = fs_find_dir_info(sb, fname);

	if(dir == NULL) {
		errno = ENOENT;
		return -1;
	}

	if(dir->filenode != -1) {
		fs_unlink(sb, fname);
	} 

	parentblk = dir->dirnode;
	fs_read_data(sb, parentblk, (void*) parentnode);

	link = fs_find_free_link(sb, parentnode, parentblk);

	neededblks = datablks + 2 + extrainodes + (link->index == -1 ? 1 : 0);
	if(neededblks > sb->freeblks) {
		errno = ENOSPC;
		return -1;
	}

	fileblk = fs_get_block(sb);

	if(link->index == -1) {
		fs_add_link(sb, fs_create_child(sb, link->inode, parentblk), 0, fileblk);
	}
	else {
		fs_add_link(sb, link->inode, link->index, fileblk);
	}

	inode->mode   = IMREG;
	inode->parent = parentblk;
	inode->meta   = fs_get_block(sb);

	links = (datablks > LINK_LIMIT) ? LINK_LIMIT : datablks;
	for(int i = 0; i < LINK_LIMIT; i++) {
		linkblk = fs_get_block(sb);
		fs_write_data(sb, linkblk, (void*) (buf + i * sb->blksz));
		inode->links[i] = (i < links) ? linkblk : 0;
	}
	datablks -= links;

	previousblk = fileblk;
	for(int i = 1; i <= extrainodes; i++) {
		previousblk = fs_create_child(sb, previousblk, fileblk);

		fs_read_data(sb, previousblk, (void*) childnode);

		links = (datablks > LINK_LIMIT) ? LINK_LIMIT : datablks;
		for(int j = 0; j < links; j++) {
			linkblk = fs_get_block(sb);
			fs_write_data(sb, linkblk, (void*) (buf + i * j * sb->blksz));
			childnode->links[j] = linkblk;
		}
		fs_write_data(sb, previousblk, (void*) childnode);
		datablks -= links;
	}

	nodeinfo->size = cnt;
	strcpy(nodeinfo->name, dir->filename);

	fs_write_data(sb, fileblk, (void*) inode);
	fs_write_data(sb, inode->meta, (void*) nodeinfo);

	return 0;
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