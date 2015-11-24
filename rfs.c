#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <time.h>
#include <strings.h>
#include <string.h>

#define MAXNAMELENGTH	14
#define BLOCKSIZE		512	
#define TOTALBLOCKS		8196		
#define CACHESIZE		10	

// Data structure definitions
// Super block structure
struct SuperBlock{
	char sb_vname[MAXNAMELENGTH];			//Superblock name
	int	sb_ninodes;							//Number of inodes
	int	sb_nblocks;							//Number of data blocks
	int	sb_rootdir;							//Inode of root directory
	int	sb_nfreeblocks;						//Number of free data blocks
	int	sb_nfreeinodes;						//Number of free inodes
	int	sb_flags;							
	unsigned short sb_freeblocks[CACHESIZE];					//Free datablock cache
	unsigned short sb_freeinodes[CACHESIZE];					//Free inode cache
	int	sb_freeblockindex;										//Free datablock cache index
	int	sb_freeinodeindex;										//Free inode cache index
	unsigned int sb_chktime;									// Last dump time
	unsigned int sb_ctime;										// Superblock creation time
};

// Inode structure
struct INode {
	unsigned int i_size;				// File size
	unsigned int i_atime;				// Last access time
	unsigned int i_ctime;				// Creation time
	unsigned int i_mtime;				// Last modified time
	unsigned short i_blocks[13];		// Data links (12 direct + 1 indirect)		
	short i_mode;						// Permissions
	unsigned int i_uid;					// User id
	unsigned int i_gid;					// Group id
	unsigned int i_gen;				
	unsigned int i_nlinks;				// Number of symbolic links			
};

struct OnDiskDirEntry{
	char d_name[MAXNAMELENGTH];			// Directory name
	unsigned short d_inode;				// Inode number of directory
};


struct DirEntry{
	struct OnDiskDirEntry d_entry;		// Directory entry
	int	d_offset;						// Offset while opening using O_APPEND flag
}; 

struct InCoreINode{
	struct INode ic_inode;				
	int	ic_ref;
	unsigned int ofo_curpos;
	int ic_ino;
	short ic_dev;
	struct InCoreINode *ic_next;
	struct InCoreINode *ic_prev;
};

struct OpenFileObject{
	struct InCoreINode *ofo_inode;
	int ofo_mode;
	int	ofo_ref;
};

#define ROOTDIRSIZE		((4*512)/sizeof(struct OnDiskDirEntry))

//============= TESTING APPLICATION USING THESE FS CALLS ==============
// Menu driven testing application for creation of fs, 
// and all file and directory related operations
int main(int argc, char** argv){
	int fd = openDevice(0);
	init_FS(fd);
	makeDir(fd, "Folder 1", 1, 1, 1);
	makeDir(fd, "Folder 1", 1, 1, 1);
	shutdownDevice(0);
}

//============= GLOBAL VARIABLES ==============
struct SuperBlock s;
struct INode nullINode;
char nullbuf[BLOCKSIZE];
int INODETABLESIZE;
int nsuperblocks;
int ninodeblocks;
int nbootblocks;
int nrootdirblocks;
int ndatablocks;
int MAXDIRENTRIES;
int DATABLOCKSTART;
int INODEBLOCKSTART;
int fd2;
int currDirINode;

//============= SYSTEM CALL LEVEL NOT FOLLOWED =======

//============= VNODE/VFS NOT FOLLOWED ===============

//============== UFS INTERFACE LAYER ==========================
int init_FS(int fd){
	// Boot block dummy block (Because no boot loader nothing...)
	bzero(nullbuf, BLOCKSIZE);
	write(fd, nullbuf, BLOCKSIZE);

	// Initialize variables
	int i;
	nsuperblocks = 1;
	ninodeblocks = 8;
	nbootblocks = 1;
	nrootdirblocks = 1;
	ndatablocks = TOTALBLOCKS - nsuperblocks - ninodeblocks - nbootblocks - nrootdirblocks;
	INODETABLESIZE = (ninodeblocks*BLOCKSIZE)/sizeof(struct INode);
	MAXDIRENTRIES = BLOCKSIZE/ sizeof(struct DirEntry);
	DATABLOCKSTART = BLOCKSIZE*(TOTALBLOCKS - ndatablocks);
	INODEBLOCKSTART = BLOCKSIZE*(nsuperblocks+nbootblocks);
	
	//Initialize super block
	strcpy(s.sb_vname, "root");
	s.sb_ninodes = (ninodeblocks*BLOCKSIZE)/sizeof(struct INode);
	s.sb_nblocks = ndatablocks;
	s.sb_nfreeblocks = s.sb_nblocks;
	s.sb_nfreeinodes = s.sb_ninodes;
	s.sb_flags = 0;
	bzero(s.sb_freeblocks, CACHESIZE);
	bzero(s.sb_freeinodes, CACHESIZE);
	s.sb_freeblockindex = CACHESIZE;
	s.sb_freeinodeindex = CACHESIZE;
	s.sb_chktime = time(NULL);
	s.sb_ctime = time(NULL);
	write(fd, &s, sizeof(struct SuperBlock));
	write(fd, nullbuf, (nsuperblocks*BLOCKSIZE) - sizeof(struct SuperBlock));
	printf("Superblock initialized!\n");

	// Write initialized list of inodes
	nullINode.i_size = 0;
	nullINode.i_atime = 0;
	nullINode.i_ctime = 0;
	nullINode.i_mtime = 0;
	memset(nullINode.i_blocks, 0, 13);
	nullINode.i_mode = 0;
	nullINode.i_uid = 0;
	nullINode.i_gid = 0;
	nullINode.i_gen = 0;
	nullINode.i_nlinks = 0;
	for(i=0; i<INODETABLESIZE; i++)
		write(fd, &nullINode, sizeof(struct INode));
	write(fd, &nullbuf, BLOCKSIZE%sizeof(struct INode));
	printf("Inodes initialized!\n");

	// Write initialized list of directory entries
	// Fill the remaining empty datablocks
	printf("%d\n",ndatablocks+nrootdirblocks );
	for(i=0; i<ndatablocks+nrootdirblocks; i++)
		write(fd, &nullbuf, BLOCKSIZE);
	printf("All data blocks initialized!\n");

	// Write free block information (data structures)
	struct INode in;
	in.i_atime = 0;
	in.i_blocks[0] = allocBlock(fd);
	in.i_gen = 0;
	in.i_gid = 1;
	in.i_uid = 1;
	in.i_nlinks = 0;
	in.i_mode = 0;
	struct DirEntry d;
	strcpy(d.d_entry.d_name,".");
	s.sb_rootdir = currDirINode = d.d_entry.d_inode = allocINode(fd, &in);
	d.d_offset = 0;
	allocDirEntry(fd, &in, &d);
	writeInode(fd, s.sb_rootdir, &in);
	updateSB(fd);
	printf("\n");
}

// Open/create a file in the given directory with specified uid, gid 
// and attributes. Type of modes are limited to read, write and append only.
int openFile(int dirhandle, char *fname, int mode, int uid, int gid, int attrib){
	// Find corresponding open file object using reference no
	// Check for validity of the file in that inode
	// According to flags create or open a file and add it to open file objects
	// Return open file object reference number	
}

// Close a file
int CloseFile(int fhandle){
	// Find and remove open file object using reference number
}

// Read from a file already opened, nbytes into nullbuf
int ReadFile(int fhandle, char buf[], int nbytes){
	// Find corresponding open file object using reference no and get its inode
	// Using offset read nbytes of the file in buf
}

// Write into a file already opened, nbytes from nullbuf
int WriteFile(int fhandle, char nullbuf[], int nbytes){
	// Find corresponding open file object using reference no and get its inode
	// Using offset write nbytes of the file in buf	
}

// Move the file pointer to required position
int SeekFile(int fhandle, int pos, int whence){
	// Find corresponding open file object using reference no and get its inode
	// Change offset		
}

// Create a directory
int makeDir(int fd, char *dname, int uid, int gid, int attributes){
	printf("Creating new directory %s\n",dname);
	int parINodeNo, inodeNo;
	struct INode parent_in;
	struct DirEntry d;

	//Tokenize the dname, check for validity and find its parent directory
	parINodeNo = currDirINode;
	readINode(fd, parINodeNo, &parent_in);

	// Check if the dname already exists in the INode in
	if( fileExists(fd, dname, parent_in, &d)!=-1 ){
		printf("Directory already exists!\n\n");
		return -1;
	}

	// Initialize an inode and data block for new directory
	struct INode in;
	in.i_atime = 0;
	in.i_blocks[0] = allocBlock(fd);
	in.i_gen = 0;
	in.i_gid = gid;
	in.i_uid = uid;
	in.i_nlinks = 0;
	in.i_mode = attributes;
	strcpy(d.d_entry.d_name, ".");
	int allocatedINode = allocINode(fd, &in);
	d.d_entry.d_inode = allocatedINode;
	d.d_offset = 0;
	allocDirEntry(fd, &in, &d);
	strcpy(d.d_entry.d_name, "..");
	d.d_entry.d_inode = parINodeNo;
	allocDirEntry(fd, &in, &d);
	writeInode(fd, allocatedINode, &in);

	// Add its DirEntry in its parent INode and rewrite Inode entry
	strcpy(d.d_entry.d_name, dname);
	d.d_entry.d_inode = allocatedINode;
	allocDirEntry(fd, &parent_in, &d);
	writeInode(fd, parINodeNo, &parent_in);
	printf("\n");
	return 0;
}

// Delete directory (if it is empty)
int removeDir(int fd, char *dname, int parINodeNo){
	/*
	int i;
	struct INode parent_in;
	// Tokenize the name and find its parent directory
	readINode(fd, parINodeNo, &parent_in);

	// Find dname in parent directory and read its inode
	struct DirEntry d;
	int linkNo = fileExists(fd, dname, parent_in, &d);
	if( linkNo==-1 ){
		printf("Directory does not exist!\n");
		return -1;
	}
	if( d.d_entry.d_inode == s.sb_rootdir ){
		printf("Cannot delete root directory!\n");
		return -1;
	}
	struct INode in;
	readINode(fd, d.d_entry.d_inode, &in);

	// On each of the link in its inode call recursive delete after taking permission
	if( in.i_nlinks==0 ){
		printf("Not a valid directory!\n");
		return -1;
	}
	if( in.i_nlinks>2 ){
		printf("Directory not empty! Do you want to proceed[y]? ");
		char c;
		scanf("%c",&c);
		if( c!='y' || c!='Y' ){
			return -1;
		}
		int i=0, j = 0, k;
		for(i=0; i<=(in.i_nlinks-1)/MAXDIRENTRIES; i++){
			lseek(fd, in.i_blocks[i]*BLOCKSIZE, SEEK_SET);
			for(k=0; k<MAXDIRENTRIES && j<in.i_nlinks; k++, j++){
				read(fd, d, sizeof(struct DirEntry));
				struct INode* temp;
				readInode(fd, d.d_entry.d_inode, &temp);
				if( temp = )
			}
		}
	return -1;
	}
	freeINode(fd, d.d_entry.d_inode);
	freeDirEntry(fd, &in, linkNo);
	printf("Directory successfully deleted!\n");
	*/
}

int openDir(int pardir, char *dname){
	// Tokenize the name and find its inode no


	// Add the inode to open file object DLL and return the reference number

}

int CloseDir(int dirhandle){
	// Find the corresponding open file object and remove it
}

int SeekDir(int dirhandle, int pos, int whence){
	// Find the corresponding open file object
	// Increase its offset
}

int readDir(int inodeNo, struct DirEntry *dent){
	// Find the corresponding open file obejct
	// Read one DirEntry
	// Increase offset
}

//============== UFS INTERNAL LOW LEVEL ALGORITHMS =============
int readINode(int fd, int inodeNo, struct INode *inode){
	lseek(fd, INODEBLOCKSTART + inodeNo*sizeof(struct INode), SEEK_SET);
	printf("Reading inode number %d at %lu\n", inodeNo, INODEBLOCKSTART + inodeNo*sizeof(struct INode));
	read(fd, inode, sizeof(struct INode));
	inode->i_atime = time(NULL);
	write(fd, inodeNo, inode);
}

int writeInode(int fd, int inodeNo, struct INode *inode){
	lseek(fd, INODEBLOCKSTART + inodeNo*sizeof(struct INode), SEEK_SET);
	printf("\tWriting inode number %d at %lu\n", inodeNo, INODEBLOCKSTART + inodeNo*sizeof(struct INode));
	inode->i_mtime = time(NULL);
	write(fd, inode, sizeof(struct INode));
	write(fd2, "\n", 1);
	write(fd2, inode, sizeof(struct INode));
}

int allocINode(int fd, struct INode* in){
	if( s.sb_freeinodeindex==CACHESIZE ){
		printf("Fetching inodes into cache\n");
		int fetched = fetchFreeINodes(fd);
		if( fetched==0 ){
			printf("Could not fetch any inodes!\n");
			return -1;
		}
		printf("Fetched %d inodes\n",fetched);
	}
	s.sb_nfreeinodes--;
	printf("Allocated inode no %d\n", s.sb_freeinodes[s.sb_freeinodeindex]);
	in->i_ctime = time(NULL);
	return s.sb_freeinodes[s.sb_freeinodeindex++];
}

int freeINode(int fd, int inodeNo){
	lseek(fd, INODEBLOCKSTART + inodeNo*sizeof(struct INode), SEEK_SET);
	write(fd, &nullINode, sizeof(struct INode));
	printf("Freed inode number %d\n", inodeNo);
	s.sb_nfreeinodes++;
}

/*
int ReadBlock(int dev, int blk, int nullbuf[BLOCKSIZE]){
	// Check for validity of the block
	// Check for validity of the device

	// If OK read the block
	lseek(device_fd[dev], blk * BLOCKSIZE, SEEK_SET);
	return read(device_fd[dev], nullbuf, BLOCKSIZE);
}

// Writing a logical block blk to device dev
int WriteBlock(int dev, int blk){
	// Check for validity of the block
	// Check for validity of the device

	// If OK write the block
	lseek(device_fd[dev], blk * BLOCKSIZE, SEEK_SET);
	return write(device_fd[dev], nullbuf, BLOCKSIZE);
}
*/

int allocBlock(int fd){
	if( s.sb_freeblockindex==CACHESIZE ){
		printf("Fetching data blocks into cache..\n");
		int fetched = fetchFreeBlocks(fd);
		if( fetched==0 ){
			printf("Could not fetch data blocks!\n");
			return -1;
		}
		printf("Fetched %d data blocks\n",fetched);
	}
	s.sb_nfreeblocks--;
	printf("Allocated block no %d\n",s.sb_freeblocks[s.sb_freeblockindex] );
	return s.sb_freeblocks[s.sb_freeblockindex++];
}

int freeBlock(int fd, int blockNo){
	lseek(fd, BLOCKSIZE*blockNo, SEEK_SET);
	write(fd, &nullbuf, BLOCKSIZE);
	printf("Freed block no %d\n", blockNo);
	s.sb_nfreeblocks++;
}

int writeDirEntry(int fd, struct INode* in, int linkNo, struct DirEntry* dent){
	lseek(fd, getDirEntryAddress(linkNo, in), SEEK_SET);
	printf("\tWriting directory entry %s at %d\n",dent->d_entry.d_name, getDirEntryAddress(linkNo, in));
	write(fd, dent, sizeof(struct DirEntry));
	write(fd2, "#", 1);
	write(fd2, dent, sizeof(struct DirEntry));
}

int readDirEntry(int fd, struct INode* in, int linkNo, struct DirEntry* dent){
	lseek(fd, getDirEntryAddress(linkNo, in), SEEK_SET);
	read(fd, dent, sizeof(struct DirEntry));
	printf("Read Dir Entry %s\n",dent->d_entry.d_name);
}

int freeDirEntry(int fd, struct INode* in, int linkNo){
	struct DirEntry last;
	readDirEntry(fd, in, in->i_nlinks, &last);
	writeDirEntry(fd, in, linkNo, &last);
}

int allocDirEntry(int fd, struct INode* in, struct DirEntry* d){
	writeDirEntry(fd, in, in->i_nlinks, d);
	in->i_nlinks++;
	in->i_size = sizeof(struct INode)*in->i_nlinks;
	printf("Allocated dir entry for %s in inode %u at link no %u\n",d->d_entry.d_name, d->d_entry.d_inode, in->i_nlinks-1);
}

int fetchFreeBlocks(int fd){
	if( s.sb_nfreeblocks==0 ){
		printf("Block memory overflow!\n");
		return 0;
	}
	lseek(fd, DATABLOCKSTART, SEEK_SET);
	int i;
	char buf[BLOCKSIZE];
	for(i = TOTALBLOCKS-ndatablocks; i<TOTALBLOCKS && s.sb_freeblockindex>0; i++){
		read(fd, &buf, BLOCKSIZE);
		if( strlen(buf)==0 ){
			s.sb_freeblocks[--s.sb_freeblockindex] = i;
		}
	}
	return CACHESIZE-s.sb_freeblockindex;
}
	
int fetchFreeINodes(int fd){
	if( s.sb_nfreeinodes==0 ){
		printf("Inode memsetmory overflow!\n");
		return 0;
	}
	lseek(fd, BLOCKSIZE*(nsuperblocks+nbootblocks), SEEK_SET);
	int i;
	struct INode in;
	for(i = 0; i<INODETABLESIZE && s.sb_freeinodeindex>0; i++){
		read(fd, &in, sizeof(struct INode));
		if( in.i_blocks[0]==0 ){
			s.sb_freeinodes[--s.sb_freeinodeindex] = i;
		}
	}
	return CACHESIZE-s.sb_freeinodeindex;
}

int updateSB(int fd){
	lseek(fd, BLOCKSIZE*nbootblocks, SEEK_SET);
	write(fd, &s, sizeof(struct SuperBlock));
}

int fileExists(int fd, char *name, struct INode in, struct DirEntry* d){
	int i=0, j = 0, k;
	for(i=0; i<=(in.i_nlinks-1)/MAXDIRENTRIES; i++){
		lseek(fd, in.i_blocks[i]*BLOCKSIZE, SEEK_SET);
		for(k=0; k<MAXDIRENTRIES && j<in.i_nlinks; k++, j++){
			read(fd, d, sizeof(struct DirEntry));
			if( strcmp(d->d_entry.d_name, name)==0 ){
				return j;
			}
		}
	}
	return -1;
}

int getDirEntryAddress(int linkNo, struct INode* in){
	return BLOCKSIZE*(in->i_blocks[linkNo/MAXDIRENTRIES]) + sizeof(struct DirEntry)*((linkNo-1)/MAXDIRENTRIES);
}

/*
//============== DEVICE DRIVER LEVEL =====================

// Reading a logical block blk from device dev
int ReadBlock(int dev, int blk, int nullbuf[BLOCKSIZE])
{
	// Check for validity of the block
	// Check for validity of the device

	// If OK read the block
	lseek(device_fd[dev], blk * BLOCKSIZE, SEEK_SET);
	return read(device_fd[dev], nullbuf, BLOCKSIZE);
}

// Writing a logical block blk to device dev
int WriteBlock(int dev, int blk)
{
	// Check for validity of the block
	// Check for validity of the device

	// If OK write the block
	lseek(device_fd[dev], blk * BLOCKSIZE, SEEK_SET);
	return write(device_fd[dev], nullbuf, BLOCKSIZE);
}
*/

char *device_name[] = {"filesystemcore.txt",NULL};
int device_fd[] = {-1, -1};

// Open the device
int openDevice(int dev){
	// Open the device related file for both reading and writing.
	if ((device_fd[dev] = open(device_name[dev], O_RDWR|O_CREAT, 0666)) < 0){
		perror("Opening device file failure:");
		exit(1);
	}
	printf("Device %s successfully mounted\n\n", device_name[dev]);
	fd2 = open("output", O_RDWR|O_CREAT, 0666);
	return device_fd[dev];
}

// Shutdown the device
int shutdownDevice(int dev){
	if (device_fd[dev] >= 0)
		close(device_fd[dev]);
	printf("Device %s is successfully shutdown!\n", device_name[dev]);
}

