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

#define LINK_MAX (sb->blksz - 32) / sizeof(uint64_t)
#define NAME_MAX sb->blksz - (8 * sizeof(uint64_t))


/*
 
 - Debug write file to find seg fault
 - Find and delete inode for links that are completely unused(if IMCHILD and inode->next == 0 and all links == 0 then putblock and remove reference to inode->meta(previous block) reference to it)
 - Check if dir is root in rmdir(compare with sb->root)
 - Check find_dir_info behaviour with root
 - Add free to every error return case
 - Check TODOs throughout the code
 - THE MEMORY PROBLEM IS ON DIR INFO OR FIND LINK https://ubuntuforums.org/showthread.php?t=774897
*/

/************************
*       UTILITIES       * 
************************/

struct link {
  uint64_t inode;
  int index;
};

struct dir {
	uint64_t dirnode;   /* Dir inode corresponding block            */
	uint64_t nodeblock; /* The requested inode block, if it exists  */
	char *nodename;     /* Name of the requested inode, file or dir */
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

/* Returns the name of the last inode, its parent dir inode position and its inode position(if it doesnt exists returns -1). 
 *  In case of error sets errno to the right value and returns NULL */
struct dir * fs_find_dir_info(struct superblock *sb, const char *dpath) {
	int pathlenght = 0;
	char *token;
	char *nodename = malloc(NAME_MAX);
	char *pathcopy = malloc(NAME_MAX);

	strcpy(pathcopy, dpath);

	token = strtok(pathcopy, "/");
	while(token != NULL) {
		strcpy(nodename, token);
		pathlenght++;
		token = strtok(NULL, "/");
	} 

	strcpy(pathcopy, dpath);

	uint64_t dirnode, nodeblock, j;
	struct dir *dir              = malloc(sizeof *dir); 
	struct inode *inode          = malloc(sb->blksz);
	struct inode *auxinode       = malloc(sb->blksz);
	struct nodeinfo *nodeinfo    = malloc(sb->blksz);
	struct nodeinfo *auxnodeinfo = malloc(sb->blksz);

	dirnode = 1;

	fs_read_data(sb, dirnode, (void*) inode);
	fs_read_data(sb, inode->meta, (void*) nodeinfo);
	token = strtok(pathcopy, "/");

	j = 0;

	for(int i = 0; i < pathlenght; i++) {
		printf("LINK_MAX %lu\n", LINK_MAX);
		while(j < LINK_MAX) {
			//printf("j: %llu\n", j);
			nodeblock = inode->links[j];
			//printf("Nodeblock %llu\n", nodeblock);
			if(nodeblock != 0) {
				fs_read_data(sb, nodeblock, (void*) auxinode);
				fs_read_data(sb, auxinode->meta, (void*) auxnodeinfo);

				if(!strcmp(auxnodeinfo->name, token)) {
					if(i + 1 < pathlenght) dirnode = nodeblock;
					inode = auxinode;
					nodeinfo = auxnodeinfo;
					break;
				}	
			}

			j++;

			if(j == LINK_MAX) {
				if(inode->next != 0) { /* Restarts loop with child inode */
					printf("inode->next != 0\n");
					j = 0;
					fs_read_data(sb, inode->next, (void*)inode);
				}
				else{ 
					if(i + 1 == pathlenght) {
						nodeblock = -1; /* Ends while without finding file */
						printf("nodeblock = -1\n");
					}
					else { /* Error: Path doesn't exists */
						errno = ENOENT;
						return NULL;
					}
				}
			}
		}
		token = strtok(NULL, "/");
	}


	dir->dirnode = dirnode;
	dir->nodeblock = nodeblock;
	dir->nodename = malloc(NAME_MAX);
	printf("Nodename %s\n", nodename);
	strcpy(dir->nodename, nodename);

	free(nodename);
	free(pathcopy);
	free(inode);
	free(auxinode);
	free(nodeinfo);
	free(auxnodeinfo);

	return dir;
}

/* Returns the inode relative to parent that have a free link and the free link index in the array of links.
 * If the link is not found, return the last inode in the chain and -1 as link index. linkvalue should be 0 
 * when looking for a free link. */
struct link * fs_find_link(struct superblock *sb, uint64_t inodeblk, uint64_t linkvalue) {
	int i = 0;
	uint64_t actualblk = inodeblk;
	struct link *link = malloc(sizeof *link);
	struct inode *inode = malloc(sizeof *inode);

	fs_read_data(sb, inodeblk, (void*) inode);

	while(i < LINK_MAX) {
		if(inode->links[i] == linkvalue) {
			link->inode = actualblk;
			link->index = i;
			break;
		}

		i++;

		if(i == LINK_MAX) {
			if(inode->next == 0) {
				link->inode = actualblk;
				link->index = -1;
				break;
			}
			else{ /* Restarts loop with child inode */
				i = 0;
				actualblk = inode->next;
				fs_read_data(sb, inode->next, (void*) inode);
			}
		}
	}

	printf("linknode %llu - linkdex %d\n", link->inode, link->index);

	free(inode);

	return link;
}

/* Returns child inode pos in fs*/
uint64_t fs_create_child(struct superblock *sb, uint64_t thisblk, uint64_t parentblk) {
	uint64_t ret;
	struct inode *inode     = malloc(sb->blksz);
	struct inode *childnode = malloc(sb->blksz);

	fs_read_data(sb, thisblk, (void*) inode);

	inode->next = fs_get_block(sb);

	childnode->mode   = IMCHILD;
	childnode->parent = parentblk;
	childnode->meta   = thisblk;
	childnode->next   = 0;
	for(int i = 0; i < LINK_MAX; i++) {
		childnode->links[i] = 0;
	}

	fs_write_data(sb, thisblk, (void*) inode);
	fs_write_data(sb, inode->next, (void*) childnode);

	ret = inode->next;

	free(inode);
	free(childnode);

	return ret;
}

void fs_add_link(struct superblock *sb, uint64_t parentblk, int linkindex, uint64_t newlink) {
	struct inode *inode = malloc(sb->blksz);
	struct nodeinfo *nodeinfo = malloc(sb->blksz);

	fs_read_data(sb, parentblk, (void*) inode);
	/* TODO SHOULD CHECK IF IM CHILD AND FIND REAL NODEINFO */
	fs_read_data(sb, inode->meta, (void*) nodeinfo);	

	inode->links[linkindex] = newlink;
	nodeinfo->size++;

	fs_write_data(sb, parentblk, (void*) inode);
	fs_write_data(sb, inode->meta, (void*) nodeinfo);

	free(inode);
}

void fs_remove_link(struct superblock *sb, uint64_t parentblk, int linkindex) {
	struct inode *inode = malloc(sb->blksz);
	struct nodeinfo *nodeinfo = malloc(sb->blksz);

	fs_read_data(sb, parentblk, (void*) inode);
	/* TODO SHOULD CHECK IF IM CHILD AND FIND REAL NODEINFO */
	fs_read_data(sb, inode->meta, (void*) nodeinfo);

	inode->links[linkindex] = 0;
	nodeinfo->size--;

	fs_write_data(sb, parentblk, (void*) inode);
	fs_write_data(sb, inode->meta, (void*) nodeinfo);

	free(inode);
}

/* If inode has any links, return 1 */
int fs_has_links(struct superblock *sb, uint64_t thisblk) {
	int ret;
	struct inode *inode = malloc(sb->blksz);

	fs_read_data(sb, thisblk, (void*) inode);

	for(int i = 0; i < LINK_MAX; i++) {
		ret = inode->links[i] ? 1 : 0;
	}

	free(inode);

	return ret;
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

	sb->magic    = 0xdcc605f5;
	sb->blks     = get_file_size(fname) / blocksize;
	sb->blksz    = blocksize;
	sb->freeblks = sb->blks - 3;
	sb->freelist = 3;
	sb->root     = 1;
	sb->fd       = open(fname, O_RDWR, 0666);

	rootnode->mode   = IMDIR;
	rootnode->parent = 1;
	rootnode->meta   = 2;
	rootnode->next   = 0;
	for(int i = 0; i < LINK_MAX; i++) {
		rootnode->links[i] = 0;
	}

	rootinfo->size = 0;
	strcpy(rootinfo->name, "/");

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
	int fd = open(fname, O_RDWR, 0666);

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

   // printf("GET_BLOCK sb->freeblks %llu - sb->freelist %llu\n", sb->freeblks, sb->freelist);

	uint64_t ret;

	//printf("BP1\n");

	struct freepage *freepage = malloc(sb->blksz);

	//printf("BP2\n");

	fs_read_data(sb, sb->freelist, (void*) freepage);

	ret = sb->freelist;
	sb->freeblks--;

//	printf("Freepage->next: %llu\n", freepage->next);
	sb->freelist = freepage->next;

	fs_write_data(sb, 0, (void*) sb);

	free(freepage);

	return ret;
}

int fs_put_block(struct superblock *sb, uint64_t block) {
	struct freepage *freepage = malloc(sb->blksz);

//	printf("PUT_BLOCK(%d) sb->freeblks %llu - sb->freelist %llu\n", block, sb->freeblks, sb->freelist);

	freepage->next  = sb->freelist;
	freepage->count = 0;

	sb->freeblks++;
	sb->freelist = block;

	fs_write_data(sb, block, (void*) freepage);
	fs_write_data(sb, 0, (void*) sb);

	free(freepage);

	return 0;
}

int fs_write_file(struct superblock *sb, const char *fname, char *buf, size_t cnt) {
	uint64_t datablks, extrainodes, neededblks, links;
	uint64_t fileblk, previousblk, linkblk;
	struct dir *dir;
	struct link *link;
	struct inode *inode       = malloc(sb->blksz);
	struct inode *childnode   = malloc(sb->blksz);
	struct nodeinfo *nodeinfo = malloc(sb->blksz);

	datablks = (cnt / sb->blksz) + ((cnt % sb->blksz) ? 1 : 0); /* Blocks needed for data */
	extrainodes = 0; /* Child inodes needed to store all the links to blocks */

	if(datablks > LINK_MAX) {
		extrainodes = (datablks / LINK_MAX) + (datablks % LINK_MAX ? 1 : 0);
	}

	dir = fs_find_dir_info(sb, fname);

	if(dir == NULL) { /* Path not found */
		return -1;
	}

	if(dir->nodeblock != -1) {
		fs_unlink(sb, fname);
	}

//	printf("DIRNODE %d\n", dir->dirnode);

	link = fs_find_link(sb, dir->dirnode, 0);
/*
	printf("BP1\n");

	struct freepage *freepage = malloc(sb->blksz);

	lseek(sb->fd, 4095 * sb->blksz, SEEK_SET);
	read(sb->fd, freepage, sb->blksz);

	printf("BP2\n");
*/

	neededblks = datablks + 2 + extrainodes + (link->index == -1 ? 1 : 0);
	if(neededblks > sb->freeblks) {
		errno = ENOSPC;
		return -1;
	}

	fileblk = fs_get_block(sb);

	printf("BREAKPOINT GETBLOCK\n");

	if(link->index == -1) { /* If no link exists, create child of dirnode to store it */
		fs_add_link(sb, fs_create_child(sb, link->inode, dir->dirnode), 0, fileblk);
	}
	else {
		fs_add_link(sb, link->inode, link->index, fileblk);
	}

	inode->mode   = IMREG;
	inode->parent = dir->dirnode;
	inode->meta   = fs_get_block(sb);
	inode->next   = 0;

	fs_write_data(sb, fileblk, (void*) inode);

	links = (datablks > LINK_MAX) ? LINK_MAX : datablks;
	for(int i = 0; i < LINK_MAX; i++) {
		linkblk = fs_get_block(sb);
		fs_write_data(sb, linkblk, (void*) (buf + i * sb->blksz));
		inode->links[i] = (i < links) ? linkblk : 0;
	}
	datablks -= links;

	previousblk = fileblk;
	for(int i = 1; i <= extrainodes; i++) {
		previousblk = fs_create_child(sb, previousblk, fileblk);

		fs_read_data(sb, previousblk, (void*) childnode);

		links = (datablks > LINK_MAX) ? LINK_MAX : datablks;
		for(int j = 0; j < links; j++) {
			linkblk = fs_get_block(sb);
			fs_write_data(sb, linkblk, (void*) (buf + i * j * sb->blksz));
			childnode->links[j] = linkblk;
		}
		fs_write_data(sb, previousblk, (void*) childnode);
		datablks -= links;
	}

	nodeinfo->size = cnt;
	strcpy(nodeinfo->name, dir->nodename);

	fs_write_data(sb, fileblk, (void*) inode);
	fs_write_data(sb, inode->meta, (void*) nodeinfo);

	free(dir);
	free(link);
	free(inode);
	free(childnode);
	free(nodeinfo);

	return 0;
}

ssize_t fs_read_file(struct superblock *sb, const char *fname, char *buf, size_t bufsz) {
	size_t numblks;
	struct dir *dir = malloc(sizeof *dir);
	struct inode *inode = malloc(sb->blksz);
	struct nodeinfo *nodeinfo = malloc(sb->blksz);

	dir = fs_find_dir_info(sb, fname);

	if(dir->nodeblock == -1) {
		errno = ENOENT;
		return -1;
	}

	fs_read_data(sb, dir->nodeblock, (void*) inode);
	fs_read_data(sb, inode->meta, (void*) nodeinfo);

	if(inode->mode != IMREG) {
		errno = EISDIR;
		return -1;
	}

	if(bufsz > nodeinfo->size) bufsz = nodeinfo->size;
	numblks = (bufsz / sb->blksz) + ((bufsz % sb->blksz) ? 1 : 0);


	for(int i = 0; i < numblks; i++) {
		if((i != 0) && (i % LINK_MAX == 0)) {
			if(inode->next != 0) {
				fs_read_data(sb, inode->next, (void*) inode);
			}
			else {
				errno = EPERM;
				return -1;
			}
		}
		
		fs_read_data(sb, inode->links[i % LINK_MAX], (void*) (buf + i * sb->blksz));
	}

	free(dir);
	free(inode);
	free(nodeinfo);
	return bufsz;
}

int fs_unlink(struct superblock *sb, const char *fname) {
	int numblks, numlinks;
	uint64_t thisblk, nextblk;
	struct dir *dir;
	struct link *link;
	struct inode *inode = malloc(sb->blksz);
	struct nodeinfo *nodeinfo = malloc(sb->blksz);

	dir = fs_find_dir_info(sb, fname);

	if(dir->nodeblock == -1) {
		errno = ENOENT;
		return -1;
	}

	fs_read_data(sb, dir->nodeblock, (void*) inode);
	fs_read_data(sb, inode->meta, (void*) nodeinfo);

	/* Free all blocks used including the one with the inode */
	numblks = (nodeinfo->size / sb->blksz) + ((nodeinfo->size % sb->blksz) ? 1 : 0);
	numlinks = (numblks > LINK_MAX) ? LINK_MAX : numblks;
	for(int i = 0; i < numlinks; i++) {
		fs_put_block(sb, inode->links[i]);
	}
	numblks -= numlinks;
	nextblk = inode->next;
	fs_put_block(sb, dir->nodeblock);

	while(nextblk != 0) {
		fs_read_data(sb, nextblk, inode);
		thisblk = nextblk;
		nextblk = inode->next;

		numlinks = (numblks > LINK_MAX) ? LINK_MAX : numblks;
		for(int i = 0; i < numlinks; i++) {
			fs_put_block(sb, inode->links[i]);
		}
		numblks -= numlinks;

		fs_put_block(sb, thisblk);
	}

	/* Remove parent link to file */
	link = fs_find_link(sb, dir->dirnode, dir->nodeblock);

	fs_remove_link(sb, link->inode, link->index);

	free(dir);
	free(link);
	free(inode);
	free(nodeinfo);

	return 0;
}

int fs_mkdir(struct superblock *sb, const char *dname) {
	uint64_t dirblk;
	struct dir *dir;
	struct link *link;
	struct inode *inode       = malloc(sb->blksz);
	struct nodeinfo *nodeinfo = malloc(sb->blksz);

	dir = fs_find_dir_info(sb, dname);

	if(dir == NULL) {
		return -1;
	}

	if(dir->nodeblock != -1) {
		errno = EEXIST;
		return -1;
	}

	link = fs_find_link(sb, dir->dirnode, 0);

	if((sb->freeblks < (2 + (link->index == -1 ? 1 : 0)))) {
		errno = ENOSPC;
		return -1;
	}

	dirblk = fs_get_block(sb);

	/* If no free links was found, needs to create new child inode */
	if(link->index == -1) {
		fs_add_link(sb, fs_create_child(sb, link->inode, dir->dirnode), 0, dirblk);
	}
	else {
		fs_add_link(sb, link->inode, link->index, dirblk);
	}

	inode->mode = IMDIR;
	inode->parent = dir->dirnode;
	inode->meta = fs_get_block(sb);
	inode->next = 0;
	for(int i = 0; i < LINK_MAX; i++) {
		inode->links[i] = 0;
	}

	nodeinfo->size = 0;
	strcpy(nodeinfo->name, dir->nodename);

	fs_write_data(sb, dirblk, (void*) inode);
	fs_write_data(sb, inode->meta, (void*) nodeinfo);

	free(dir);
	free(link);
	free(inode);
	free(nodeinfo);

	return 0;
}

int fs_rmdir(struct superblock *sb, const char *dname) {
	struct dir *dir;
	struct link *link;
	struct inode *inode       = malloc(sb->blksz);
	struct nodeinfo *nodeinfo = malloc(sb->blksz);

	dir = fs_find_dir_info(sb, dname);

	if(dir == NULL) {
		return -1;
	}

	fs_read_data(sb, dir->nodeblock, (void*) inode);
	fs_read_data(sb, inode->meta, (void*) nodeinfo);

	if(inode->mode != IMDIR) {
		errno = ENOTDIR;
		return -1;
	}

	if(nodeinfo->size) {
		errno = ENOTEMPTY;
		return -1;
	}	

	fs_put_block(sb, dir->nodeblock);

	link = fs_find_link(sb, dir->dirnode, dir->nodeblock);

	fs_remove_link(sb, link->inode, link->index);

	free(dir);
	free(link);
	free(inode);
	free(nodeinfo);

	return 0;
}

char * fs_list_dir(struct superblock *sb, const char *dname) {
	char *ret = "";
	uint64_t elements, size;
	struct dir *dir;
	struct inode *inode          = malloc(sb->blksz);
	struct inode *auxinode       = malloc(sb->blksz);
	struct nodeinfo *nodeinfo    = malloc(sb->blksz);
	struct nodeinfo *auxnodeinfo = malloc(sb->blksz);

	dir = fs_find_dir_info(sb, dname);

	if(dir == NULL) {
		return NULL;
	}

	fs_read_data(sb, dir->nodeblock, (void*) inode);
	fs_read_data(sb, inode->meta, (void*) nodeinfo);

	if(inode->mode != IMDIR) {
		errno = ENOTDIR;
		return NULL;
	}

	if(nodeinfo->size == 0) {
		return ret;
	}

	for(int i = 0; i < LINK_MAX; i++) {
		if(inode->links[i] != 0) {
			fs_read_data(sb, inode->links[i], (void*) auxinode);
			fs_read_data(sb, auxinode->meta, (void*) auxnodeinfo);

			strcat(ret, auxnodeinfo->name);
			if(auxinode->mode == IMDIR) strcat(ret, "/");
			strcat(ret, " ");

			elements++;

			if(elements == nodeinfo->size) {
				free(dir);
				free(inode);
				free(nodeinfo);
				return ret;
			}
		}
	}

	elements = 0;
	size = nodeinfo->size;

	while(elements < size) {
		for(int i = 0; i < LINK_MAX; i++) {
			if(inode->links[i] != 0) {
				fs_read_data(sb, inode->links[i], (void*) auxinode);
				fs_read_data(sb, auxinode->meta, (void*) auxnodeinfo);

				strcat(ret, auxnodeinfo->name);
				if(auxinode->mode == IMDIR) strcat(ret, "/");

				elements++;

				if(elements < size) strcat(ret, " ");
			}
		}
		if(inode->next != 0) {
			fs_read_data(sb, inode->next, (void*) inode);
		}
		else {
			break;
		}
	}

	free(dir);
	free(inode);
	free(auxinode);
	free(nodeinfo);
	free(auxnodeinfo);

	return ret;
}