//////////////////////////////////////////////////////////////////////////
///
/// Copyright (c) 2020 Prof. AJ Bieszczad. All rights reserved.
/// Modified by Connor Stevenson
///
//////////////////////////////////////////////////////////////////////////
///
/// This source contains code suitable for testing without FUSE.
///
/// Integration with fuse requires that you must modify the code as needed
/// to work inside the kernel and with the FUSE framework.
///
//////////////////////////////////////////////////////////////////////////

#include "cifs.h"

//////////////////////////////////////////////////////////////////////////
///
/// cifs global variables
///
//////////////////////////////////////////////////////////////////////////

/***

 A file handle of the file system volume residing on a physical device (peripheral).

 File system structure:

 bitvector - one bit per block; i.e., (CIFS_NUMBER_OF_BLOCKS / 8 / CIFS_BLOCK_SIZE) blocks
 superblock - one block
 root descriptor block - one block
 root index block - one block (there might be more if the number of files in the root is large)
 storage blocks (folder, file, data, or index) - all remaining blocks

*/
FILE* cifsVolume;

/***

 A pointer to the in-memory file system context that holds critical information about the volume.

 The in-memory data structures are fast as they reside in the
 computer memory, and not on an external storage device;
 therefore, they are used a fast gateway to the elements of the peripheral volume.

 The context will hold:
 - a copy of the volume superblock
 - a copy of the volume bitvector
 - a hash-table-based registry of all volume files the registry is created when mounting the file system
 - a list of processes currently interacting with cifs.

 All in-memory data must be diligently synchronized with the external volume;
 for example, to find a free block this bitvector MUST be searched (for speed), and if
 a block is allocated (or deallocated), then the block that contains
 the bit flips (and NOT the whole bitvector!) must be copied to the disk.

*/

CIFS_CONTEXT_TYPE* cifsContext;

/***

 FUSE has "context" that holds a number of parameters of the process that accesses it

struct fuse *fuse {
uid_t 	uid,
gid_t 	gid,
pid_t 	pid,
void * 	private_data,
mode_t 	umask }


We need to simulate the context as long as we debug without FUSE, so for convenience we will use
an auxiliary pointer that either points to the real FUSE context or to the simulated
version.

*/

struct fuse_context* fuseContext;
/// must use
// fuseContext = fuse_get_context();
/// when the cifs is integrated with FUSE !!!

//////////////////////////////////////////////////////////////////////////
///
/// cifs function implementations
///
//////////////////////////////////////////////////////////////////////////

/***
 *
 * Allocates space for the file system and saves it to disk.
 *
 * cifsFileName must be either of the three:
 *
 *    1) a regular file
 *    2) a loop interface to a real file
 *    3) a block device name
 *
 */
CIFS_ERROR cifsCreateFileSystem(char* cifsFileName)
{
	// --- create the OS context --- needed here, since writeBlock and cifsReadBlock need the context's superblock

	printf("Size of CIFS_CONTEXT_TYPE: %ld\n", sizeof(CIFS_CONTEXT_TYPE));
	cifsContext = malloc(sizeof(CIFS_CONTEXT_TYPE));
	if (cifsContext == NULL)
		return CIFS_ALLOC_ERROR;

	// open the volume for the file system

	cifsVolume = fopen(cifsFileName, "w+"); //"w+" write and append; just for the creation of the volume
	if (cifsVolume == NULL)
		return CIFS_ALLOC_ERROR;

	// --- put the file system on the volume ---

	// initialize the bitvector

	// allocate space for the in-memory bitvector
	cifsContext->bitvector = calloc(CIFS_NUMBER_OF_BLOCKS / 8, sizeof(char)); // initially all content blocks are free

	// mark as unavailable the blocks used for the bitvector
	memset(cifsContext->bitvector, 0xFF, CIFS_SUPERBLOCK_INDEX / 8);

	// let's postpone writing of the in-memory version of the bitvector to the volume until we also set
	// the bits for the superblock and the two blocks for the root folder

	// initialize the superblock

	cifsContext->superblock = malloc(CIFS_BLOCK_SIZE); // ASSUMES: sizeof(CIFS_SUPERBLOCK_TYPE) <= CIFS_BLOCK_SIZE

	cifsContext->superblock->cifsNextUniqueIdentifier = CIFS_INITIAL_VALUE_OF_THE_UNIQUE_FILE_IDENTIFIER;
	cifsContext->superblock->cifsDataBlockSize = CIFS_BLOCK_SIZE;
	cifsContext->superblock->cifsNumberOfBlocks = CIFS_NUMBER_OF_BLOCKS - 1; // excludes the invalid block number 0xFF
	cifsContext->superblock->cifsRootNodeIndex = CIFS_SUPERBLOCK_INDEX + 1; // root file descriptor is in the next block

    // ...and set the corresponding bit in the bitvector
	cifsSetBit(cifsContext->bitvector, CIFS_SUPERBLOCK_INDEX);

	// initialize the block holding the root folders; there sre two of them: folder descriptor and the index block

	// first, initia;ize the folder description block
	CIFS_BLOCK_TYPE rootFolderBlock;
	rootFolderBlock.type = CIFS_FOLDER_CONTENT_TYPE;
	// root folder always has "0" as the identifier; it's incremented for the files created later
	rootFolderBlock.content.fileDescriptor.identifier = cifsContext->superblock->cifsNextUniqueIdentifier++;
	rootFolderBlock.content.fileDescriptor.type = CIFS_FOLDER_CONTENT_TYPE;
	strcpy(rootFolderBlock.content.fileDescriptor.name, "/");
	rootFolderBlock.content.fileDescriptor.accessRights = umask(fuseContext->umask);
	rootFolderBlock.content.fileDescriptor.owner = fuseContext->uid;
	rootFolderBlock.content.fileDescriptor.size = 0;
	struct timespec time;
	clock_gettime(CLOCK_MONOTONIC, &time);
	rootFolderBlock.content.fileDescriptor.creationTime = time.tv_sec;
	rootFolderBlock.content.fileDescriptor.lastAccessTime = time.tv_sec;
	rootFolderBlock.content.fileDescriptor.lastModificationTime = time.tv_sec;
	rootFolderBlock.content.fileDescriptor.block_ref = cifsContext->superblock->cifsRootNodeIndex + 1; // next block

	// then, initialize the index block of the root folder
	CIFS_BLOCK_TYPE rootFolderIndexBlock;
	rootFolderIndexBlock.type = CIFS_INDEX_CONTENT_TYPE;
	// no files in the root folder yet, so all entries are free
	//memset(&(rootFolderIndexBlock.content), CIFS_INVALID_INDEX, CIFS_INDEX_SIZE);
	for(int i = 0; i < CIFS_INDEX_SIZE; i++) {
		rootFolderIndexBlock.content.index[i] = CIFS_INVALID_INDEX;
	}

	// now, write the two blocks for the root folder and set the corresponding bits in the bitvector
   cifsWriteBlock((const unsigned char *) &rootFolderBlock, cifsContext->superblock->cifsRootNodeIndex);
	cifsSetBit(cifsContext->bitvector, cifsContext->superblock->cifsRootNodeIndex);

   cifsWriteBlock((const unsigned char *) &rootFolderIndexBlock, cifsContext->superblock->cifsRootNodeIndex + 1);
	cifsSetBit(cifsContext->bitvector, cifsContext->superblock->cifsRootNodeIndex + 1);
	
	// write the superblock to the volume
    cifsWriteBlock((const unsigned char *) cifsContext->superblock, CIFS_SUPERBLOCK_INDEX);

	printf("I worked\n");
	// now we can write the blocks holding the in-memory version of the bitvector to the volume

	unsigned char block[CIFS_BLOCK_SIZE]; // NOTE: we free this in mountFileSystem() below
	for (int i = 0; i < CIFS_SUPERBLOCK_INDEX; i++) // superblock is the first block after the bitvector
	{
		for (int j = 0; j < CIFS_BLOCK_SIZE; j++)
			block[j] = cifsContext->bitvector[i * CIFS_BLOCK_SIZE + j];

        cifsWriteBlock((const unsigned char *) block, i);
	}

	// create all other blocks
	memset(block, 0, CIFS_BLOCK_SIZE);
	cifsWriteBlock((const unsigned char *) block, CIFS_NUMBER_OF_BLOCKS - 1);

	fflush(cifsVolume);
	fclose(cifsVolume);

	printf("CREATED CIFS VOLUME\n%d bytes\n%d blocks\nBlock size %d bytes\n",
			CIFS_NUMBER_OF_BLOCKS * CIFS_BLOCK_SIZE,
			CIFS_NUMBER_OF_BLOCKS,
			CIFS_BLOCK_SIZE);

	return CIFS_NO_ERROR;
}

/***
 *
 * Loads the file system from a disk and constructs in-memory registry of all files is the system.
 *
 * There are two things to do:
 *    - creating an in-memory hash-table-based registry of the files in the volume
 *    - copying the bitvector from the volume to its in-memory mirror
 *
 * The creation of the registry is realized through the traversal of the whole volume:
 *
 * Starting with the file system root (pointed to from the superblock) traverses the hierarchy of directories
 * and adds an entry for each folder or file to the registry by hashing the name and adding a registry
 * entry node to the conflict resolution list for that entry. If the entry is NULL, the new node will be
 * the only element of that list. If the list contains more than one element, then multiple files hashed to
 * the same value (hash conflict). In this case, the unique file identifier can be used to resolve the conflict.
 * The identifier must be the same as the identifier in the file descriptor pointed to by the node reference.
 *
 * The function must also capture the parenthood relationship by setting the parent file handle field for
 * the file being added to the registry to the file handle of the parent folder (whish is already available since
 * the traversal is top down; i.e., parent folders are always added before their content).
 *
 * The function sets the current working directory to refer to the block holding the root of the volume. This will
 * be changed as the user navigates the file system hierarchy.
 *
 */
CIFS_ERROR cifsMountFileSystem(char* cifsFileName)
{

	cifsVolume = fopen(cifsFileName, "rw+"); // now we will be reading, writing, and appending
    cifsCheckIOError("OPEN", "fopen");

	// --- create the OS context ---

	printf("Size of CIFS_CONTEXT_TYPE: %ld\n", sizeof(CIFS_CONTEXT_TYPE));
	cifsContext = malloc(sizeof(CIFS_CONTEXT_TYPE));
	if (cifsContext == NULL)
		return CIFS_ALLOC_ERROR;

	// get the superblock of the volume
	cifsContext->superblock = (CIFS_SUPERBLOCK_TYPE*) cifsReadBlock(CIFS_SUPERBLOCK_INDEX);

	// get the bitvector of the volume
	cifsContext->bitvector = malloc(cifsContext->superblock->cifsNumberOfBlocks / 8);

	// TODONE: read the bitvector from the volume (copying block after block and freeing memory as needed after copying) (SEE ABOVE NOTE FOR WHAT)
	unsigned char* block; 
	for (int i = 0; i < CIFS_SUPERBLOCK_INDEX; i++) {
		block = (unsigned char *) cifsReadBlock(i);

		for (int j = 0; j < CIFS_BLOCK_SIZE; j++) {
			cifsContext->bitvector[i * CIFS_BLOCK_SIZE + j] = block[j];
		}
		free(block);
	}	

	// create an in-memory registry of the volume
	cifsContext->registry = calloc(CIFS_REGISTRY_SIZE, sizeof(CIFS_REGISTRY_ENTRY_TYPE)); // calloc bcs add index relies on null for no entry 

	// TODO: traverse the file system starting with the root and populate the registry

	traverseFolder(cifsContext->superblock->cifsRootNodeIndex, hash("/")); // NOTE: hard coded root. Ew!

	return CIFS_NO_ERROR;
}

/***
 *
 * Saves the file system to a disk and de-allocates the memory.
 *
 * Assumes that all synchronization has been done.
 *
 */
CIFS_ERROR cifsUmountFileSystem(char* cifsFileName)

// TODO: de-allocate all the stuff in context
{

#ifdef NO_FUSE_DEBUG
	if (fuseContext->fuse != NULL)
		free(fuseContext->fuse);

	if (fuseContext->private_data != NULL)
		free(fuseContext->private_data);

	if (fuseContext != NULL)
		free(fuseContext);
#endif


	// save the current superblock
    cifsWriteBlock((const unsigned char *) cifsContext->superblock, CIFS_SUPERBLOCK_INDEX);
	// note that all bitvector writes need to be done as needed on an ongoing basis as blocks are
	// acquired and released, so any change should have been saved already

	// TODO: make sure that all information is written off to the volume prior to closing it
	// If all is done correctly in other places, then no other writing should be needed
	fflush(cifsVolume);

	fclose(cifsVolume);

	free(cifsContext);

	return CIFS_NO_ERROR;
}

//////////////////////////////////////////////////////////////////////////

/***
 *
 * Depending on the type parameter the function creates a file or a folder in the current directory
 * of the process.
 *
 * A file can be created only in an open folder, so there must be a corresponding entry
 * in the list of processes. If not, then the function returns CIFS_OPEN_ERROR.
 *
 * Just after creation of the file system, there is only one directory, the root, and it must be opened
 * to create a file in it.
 *
 * If a file with the same name already exists in the current directory, it returns CIFS_DUPLICATE_ERROR.
 *
 * Otherwise:
 *    - sets the folder/file's identifier to the current value of the next unique identifier from the superblock;
 *      then it increments the next available value in the superblock (to prepare it for the next created file)
 *    - finds an available block in the storage using the in-memory bitvector and flips the bit to indicate
 *      that the block is taken
 *    - creates an entry in the conflict resolution list for the corresponding in-memory registry entry
 *      that includes a file descriptor and fills all information about the file in the registry (including
 *      a backpointer to the parent directory that is the reference number of the block holding the file
 *      descriptor of the current working directory)
 *    - copies the local file descriptor to the disk block that was found to be free (the one that will hold
 *      the on-volume file descriptor)
 *    - copies the relevant block of the in-memory bitvector to the corresponding bitevector blocks on the volume
 *
 *  The access rights and the the owner are taken from the context (umask and uid correspondingly).
 *
 */
CIFS_ERROR cifsCreateFile(CIFS_NAME_TYPE filePath, CIFS_CONTENT_TYPE type)
{
	// TODO: chcek for duplicate file

    // Find free block for fd and flip the bit in the bitvector (FOR NEW FILE)
	CIFS_INDEX_TYPE fdIndex = cifsFindFreeBlock(cifsContext->bitvector);
	cifsFlipBit(cifsContext->bitvector, fdIndex); 

    // Find free block for ind and flip the bit in the bitvector (FOR NEW FILE)
	CIFS_INDEX_TYPE indexBlockIndex = cifsFindFreeBlock(cifsContext->bitvector);
	cifsFlipBit(cifsContext->bitvector, indexBlockIndex); 

	// Get index of root                            (NOTE: cifsRoodNodeIndex is the fd of root)
	CIFS_INDEX_TYPE rootFdIndex = cifsContext->superblock->cifsRootNodeIndex;
	// Get root file descriptor block 
	CIFS_BLOCK_TYPE* rootFD = (CIFS_BLOCK_TYPE*) cifsReadBlock(rootFdIndex); 
	// Get root index block
	CIFS_BLOCK_TYPE* rootInd = (CIFS_BLOCK_TYPE*) cifsReadBlock(rootFD->content.fileDescriptor.block_ref);

	// Add new file fd (fdindex) too root index block
	rootInd->content.index[rootFD->content.fileDescriptor.size++] = fdIndex; 	// increments size of index in root fd

	// Fill NEW fd and NEW Ind
	// Fill fd
	CIFS_BLOCK_TYPE newFD;
	newFD.type = type;

	newFD.content.fileDescriptor.identifier = cifsContext->superblock->cifsNextUniqueIdentifier++;
	newFD.content.fileDescriptor.type = type;
	strcpy(newFD.content.fileDescriptor.name, filePath);
	newFD.content.fileDescriptor.accessRights = umask(fuseContext->umask);
	newFD.content.fileDescriptor.owner = fuseContext->uid;
	newFD.content.fileDescriptor.size = 0;
	struct timespec time;
	clock_gettime(CLOCK_MONOTONIC, &time);
	newFD.content.fileDescriptor.creationTime = time.tv_sec;
	newFD.content.fileDescriptor.lastAccessTime = time.tv_sec;
	newFD.content.fileDescriptor.lastModificationTime = time.tv_sec;
	newFD.content.fileDescriptor.block_ref = indexBlockIndex; // Index block
	newFD.content.fileDescriptor.parent_block_ref = cifsContext->superblock->cifsRootNodeIndex; // TODO: hardcoded root but will have reference to parent later when we us open() on folder/file
	newFD.content.fileDescriptor.file_block_ref = fdIndex; 

	// Fill new index block
	CIFS_BLOCK_TYPE newInd;
	newInd.type = CIFS_INDEX_CONTENT_TYPE;
	// no files in the root folder yet, so all entries are free
	for(int i = 0; i < CIFS_INDEX_SIZE; i++) {
		newInd.content.index[i] = CIFS_INVALID_INDEX;
	}

	// Add new file/folder to registry TODO: doesn't matter if folder or file bcs folder will be emtpy
	addToHashTable(&(newFD.content.fileDescriptor), hash("/")); // TODO: hardcoded root 

	// Write all the changes (write root fd/ind, new fd/ind, SB, BV)

	// NOTE replace with call to writeSBBV
	// Write superblock
	cifsWriteBlock((const unsigned char *) cifsContext->superblock, CIFS_SUPERBLOCK_INDEX);

	// Write bitvector
	unsigned char block[CIFS_BLOCK_SIZE];
	for (int i = 0; i < CIFS_SUPERBLOCK_INDEX; i++)
	{
		for (int j = 0; j < CIFS_BLOCK_SIZE; j++) {
			block[j] = cifsContext->bitvector[i * CIFS_BLOCK_SIZE + j];
		}

        cifsWriteBlock((const unsigned char *) block, i);
	}

	// Write root FD
	cifsWriteBlock((const unsigned char *) rootFD, rootFdIndex);

	// Write root Ind
	cifsWriteBlock((const unsigned char *) rootInd, rootFD->content.fileDescriptor.block_ref);
	
	// Write new FD
	cifsWriteBlock((const unsigned char *) &newFD, fdIndex);

	// Write new Ind
	cifsWriteBlock((const unsigned char *) &newInd, indexBlockIndex);

	free(rootFD);
	free(rootInd);

	return CIFS_NO_ERROR;
}

//////////////////////////////////////////////////////////////////////////

/***
 * 
 * Deletes a file from the filesystem
 * 
 * Hashes the file nameto check if the file is in the registry, if not, return CIFS_NOT_FOUND_ERROR
 * 
 * If the file is found, but the reference count is non-zero (that means a process has the file opened),
 * then return CIFS_IN_USE_ERROR
 * 
 * Otherwise,
 * - if it is a non-empty folder, return CIFS_NOT_EMPTY_ERROR
 * - check if process owner has write permission to file or folder, if not, return CIFS_ACCESS_ERROR
 * - free all blocks belonging to the file by flipping the correpsonding bits in the in-memory bitvector
 *    - data blocks, index block(s), file descriptor block
 *    - note, you don't have to clear the data
 * - clear the entry in the parent folder's index
 * - decrement the size of the parent's folder
 * - write the parent folder's file descriptor and index block to the disk
 *    - update the in-memory registry with the new information
 * - write the bitvector to the volume
 * 
*/
CIFS_ERROR cifsDeleteFile(CIFS_NAME_TYPE filePath)
{
	// TODO: implement

	return CIFS_NO_ERROR;
}

//////////////////////////////////////////////////////////////////////////


// Find or create proc control block
CIFS_PROCESS_CONTROL_BLOCK_TYPE* getProcBlock(void) {
	pid_t pid = fuseContext->pid;
	CIFS_PROCESS_CONTROL_BLOCK_TYPE* cur = cifsContext->processList;

	// Find proc control block if it exist
	while (cur != NULL) {
		if (cur->pid == pid) {
			return cur;
		}
		cur = cur->next;
	}

	// Create proc block 
	CIFS_PROCESS_CONTROL_BLOCK_TYPE* procBlock = (CIFS_PROCESS_CONTROL_BLOCK_TYPE*) malloc(sizeof(CIFS_PROCESS_CONTROL_BLOCK_TYPE));
	procBlock->pid = pid;
	// Should I set ROOT as open file procBlock->openFile->(root)

	return procBlock;
}

// TODO: Extend to check all access rights
int checkUserAccess(pid_t owner, mode_t accessRights, mode_t desiredAccess) {
	// If user


}

/*
 * Compares blockRef between parent of file to be opened and blockref of PID's open files
*/
int parentIsOpen(unsigned long long parentIdent, OPEN_FILE_TYPE* head) {
	// TODO: Check if the parent is open
	// QUESTION: How does this with root??? Will every proc have root open. Then procs can have same stuff open? No Atomicity? ORR only one write open? (too compilcated)
	OPEN_FILE_TYPE* openFile = head;
	while(openFile != NULL) {
		if (openFile->identifier == parentIdent) {
			return 1;
		}
		openFile = openFile->next;
	}
	return 0;
}

/*
 * returns reference to registry entry, NULL if not found
 */
CIFS_REGISTRY_ENTRY_TYPE* resolveCollision(CIFS_FILE_HANDLE_TYPE fileHandle, CIFS_FILE_HANDLE_TYPE parentFileHandle) {
	CIFS_REGISTRY_ENTRY_TYPE* cur = cifsContext->registry[fileHandle];

	while (cur != NULL) {
		if (cur->parentFileHandle == parentFileHandle) {
			return cur;
		}
	}
	return NULL;
}


/***
 *
 * Opens a file for reading or writing.
 *
 * If the file is not found in the in-memory registry, then the function returns CIFS_NOT_FOUND_ERROR (since only
 * existing files can be opened).
 *
 * Checks if the file is already open by any process by examining the list of processes in the context.
 * If so, then the function returns CIFS_OPEN_ERROR.
 *
 * Otherwise, a check is made whether the user can be granted the desired access. For that the file descriptor
 * that holds the user id of the owner and the file access rights must be examined and compared with the user id
 * of the owner of the process calling this function (available in the FUSE context) and the desired access passed
 * in the parameter. If the user is not granted the desired access, then the function returns CIFS_ACCESS_ERROR.
 *
 * Otherwise, an entry for the process is added to the list of processes with the file handle and the granted
 * access rights for the process. Each access to the file (read, write, get info) will need to verify that
 * the requested operation is allowed for the requesting user.
 *
 * Finally, the function increments the reference count for the file, sets the output parameter to the handle
 * for the file and returns CIFS_NO_ERROR.
 *
 *  NOTE: this function and functions called by this function rely on the cwd being the top of openFile in a PCB
 */
CIFS_ERROR cifsOpenFile(CIFS_NAME_TYPE filePath, mode_t desiredAccessRights, CIFS_FILE_HANDLE_TYPE *fileHandle)
{
	CIFS_FILE_DESCRIPTOR_TYPE* fd = malloc(sizeof(CIFS_FILE_DESCRIPTOR_TYPE));

	// Check if File exists
	if (cifsGetFileInfo(filePath, fd) == CIFS_NOT_FOUND_ERROR) {
		free(fd);
		return CIFS_NOT_FOUND_ERROR;
	}
	// Check if opened by another proc
	else if (cifsContext->registry[*fileHandle]->referenceCount) {			// NOTE: not allowing a file to be open by multiple proc
		free(fd);
		return CIFS_OPEN_ERROR;
	}
	// Check if proc has parent folder open TODO: handle opening root?
	CIFS_PROCESS_CONTROL_BLOCK_TYPE* procBlock = getProcBlock(); 
	if(cifsContext->registry[*fileHandle]->parentFileHandle == procBlock->openFiles->fileHandle) {
		return CIFS_OPEN_ERROR;
	}
	// Check if desired access is allowed 
	else if((desiredAccessRights & fd->accessRights) != desiredAccessRights) {
		free(fd);
		return CIFS_ACCESS_ERROR;
	}
		
	// Do the thing (open the file)! Add new open file to procBlock and stuff
	resolveCollision(*fileHandle, procBlock->openFiles->fileHandle)->referenceCount++;   // Increase access count in registry (relies on cwd top of openFile)

	OPEN_FILE_TYPE* newFile = (OPEN_FILE_TYPE*) malloc(sizeof(OPEN_FILE_TYPE));			// Create open file node
	newFile->identifier = fd->identifier;
	newFile->fileHandle = *fileHandle;
	newFile->processAccessRights = desiredAccessRights;
	// If we should change the current working dir (set new open file as head of open file list)
	if (fd->type == CIFS_FOLDER_CONTENT_TYPE) {
		newFile->next = procBlock->openFiles;		// Prepend new file
		procBlock->openFiles = newFile;
	}
	// Current working dir is NOT changing (put as second entry)
	else {
		OPEN_FILE_TYPE* temp = procBlock->openFiles->next;
		procBlock->openFiles->next = newFile;
		newFile->next = temp;
	}
	
	free(fd);
	return CIFS_NO_ERROR;
}

//////////////////////////////////////////////////////////////////////////

/***
 *
 * The function closes the file with the specified file handle for a specific process (obtained through
 * the FUSE context).
 *
 * If the process does not have the file with the passed file handle opened (i.e., it is not an element
 * of the process list with the file passed handle), then the function returns CIFS_ACCESS_ERROR.
 *
 * Removes the entry for the process and the file from the list of processes, and then decreases the reference
 * count for the file.
 *
 */
CIFS_ERROR cifsCloseFile(CIFS_FILE_HANDLE_TYPE fileHandle)
{
	// TODO: implement

	return CIFS_NO_ERROR;
}

//////////////////////////////////////////////////////////////////////////

/***
 *
 * Finds the file in the in-memory registry and obtains the information about the file from the file descriptor
 * block referenced from the registry.
 *
 * If the file is not found, then it returns CIFS_NOT_FOUND_ERROR
 * NOTE: relies on fact that cwd is top of openFile in PCB
 */
CIFS_ERROR cifsGetFileInfo(CIFS_NAME_TYPE filePath, CIFS_FILE_DESCRIPTOR_TYPE* infoBuffer)
{
	// NOTE: Assuming first directy fond in open file list is our current directory
	//    WHY: because I don't see how else we can know our parent file handle. 
	//    Parent file handle is not stored in any think we have access too using just our cur filepath. AAAAAAAAAAAAAAAAAA
	// I JUST SAW THAT WE ALREAADY R SUPPOSED TO MAKE THIS ASSUMTION ALIJFLKJFDSLFJSDLFKJSDFL:SDJFLSDFJ:LFSDL:

	CIFS_PROCESS_CONTROL_BLOCK_TYPE* procBlock = getProcBlock();


	CIFS_REGISTRY_ENTRY_TYPE* regEntry = resolveCollision(hash(filePath), procBlock->openFiles->fileHandle);
	if (!regEntry) {
		return CIFS_NOT_FOUND_ERROR;
	}
	else {
		infoBuffer = &(regEntry->fileDescriptor);
		return CIFS_NO_ERROR;
	}
}

//////////////////////////////////////////////////////////////////////////

/***
 *
 * The function replaces content of a file with new one pointed to by the parameter writeBuffer.
 *
 * The file must be opened by the process trying to write, so the process must have a file handle of the file
 * while attempting this operation.
 *
 * Otherwise, it checks the access rights for writing by examining the entry for the process and the file
 * in the list of processes. If the process owner (from the fuse context) is not allowed
 * to write to the file, then the function returns CIFS_ACCESS_ERROR.
 *
 * Then, the function calculates the space needed for the new content and checks if the write buffer can fit into
 * the remaining free space in the file system. If not, then the CIFS_ALLOC_ERROR is returned.
 *
 * Otherwise, the function:
 *    - acquires as many new blocks as needed to hold the new content modifying the corresponding bits in
 *      the in-memory bitvector,
 *    - copies the characters pointed to by the parameter writeBuffer (until '\0' but excluding it) to the
 *      new just acquired blocks,
 *    - copies any modified block of the in-memory bitvector to the corresponding bitvector block on the disk.
 *
 * If the new content has been written successfully, the function then removes all blocks currently held by
 * this file and modifies the file descriptor to reflect the new location, the new size of the file, and the new
 * times of last modification and access.
 *
 * This order of actions prevents file corruption, since in case of any error with writing new content, the file's
 * old version is intact. This technique is called copy-on-write and is an alternative to journaling.
 *
 * The content of the in-memory file descriptor must be replaced by the new data.
 *
 * The function returns CIFS_WRITE_ERROR in response to exception not specified earlier.
 *
 */
CIFS_ERROR cifsWriteFile(CIFS_FILE_HANDLE_TYPE fileHandle, char* writeBuffer)
{
	// Check if file is open

	// Check for access rights to write (S_IWUSR)
	
	// Calculate space needed

	// Check if there is enough space
	
	// Do the thing i.e.
	//	- Acquire as many new blocks needed (update BV)
	//	- copy writeBuffer into its spots (write the actual data)
	//	- write BV
	//	- write new fd
	



	return CIFS_NO_ERROR;
}

//////////////////////////////////////////////////////////////////////////

/***
 *
 * The function returns the complete content of the file to the caller through the parameter readBuffer.
 *
 * The file must be opened by the process trying to read, so the process must have a file handle of the file
 * while attempting this operation.
 *
 * Otherwise, it checks the access rights for reading by examining the entry for the process and the file
 * in the list of processes. If the process owner is not allowed to read from the file, then the function
 * returns CIFS_ACCESS_ERROR.
 *
 * Otherwise, the function allocates memory sufficient to hold the read content with an appended end of string
 * character; the pointer to newly allocated memory is passed back through the readBuffer parameter. All the content
 * of the blocks is concatenated using the allocated space, and an end of string character is appended at the end of
 * the concatenated content.
 *
 * The function returns CIFS_READ_ERROR in response to exception not specified earlier.
 *
 */
CIFS_ERROR cifsReadFile(CIFS_FILE_HANDLE_TYPE fileHandle, char** readBuffer)
{
	// TODO: implement

	return CIFS_NO_ERROR;
}

//////////////////////////////////////////////////////////////////////////
///
/// Functions to write and read block to and from block devices
///
//////////////////////////////////////////////////////////////////////////

void cifsCheckIOError(const char* who, const char* what)
{
	int errCode = ferror(cifsVolume);
	if (errCode == 0)
		return;
	printf("%s: %s returned \"%s\"\n", who, what, strerror(errCode));
	exit(errCode);
}

void cifsPrintBlockContent(const unsigned char *str)
{
	for (int i=0; i < CIFS_BLOCK_SIZE; i++)
		printf("0x%02x ", *(str + i));
}

/***
 *
 * Write a single block to the block device.
 *
 */
size_t cifsWriteBlock(const unsigned char* content, CIFS_INDEX_TYPE blockNumber)
{
	fseek(cifsVolume, blockNumber * CIFS_BLOCK_SIZE, SEEK_SET);
    cifsCheckIOError("WRITE", "fseek");
	printf("WRITE: POSITION=%6ld, ", ftell(cifsVolume));
    cifsCheckIOError("WRITE", "ftell");
	size_t len = fwrite((const void *)content, sizeof(unsigned char), CIFS_BLOCK_SIZE, cifsVolume);
    cifsCheckIOError("WRITE", "fwrite");
	printf("LENGTH=%4ld, CONTENT=", len); // %s will ussually not work
	cifsPrintBlockContent(content);
	printf("\n");

	return len;
}

/***
 *
 * Read a single block from a block device.
 *
 */
unsigned char* cifsReadBlock(CIFS_INDEX_TYPE blockNumber)
{
	unsigned char* content = malloc(CIFS_BLOCK_SIZE);

	fseek(cifsVolume, blockNumber * CIFS_BLOCK_SIZE, SEEK_SET);
    cifsCheckIOError("READ", "fseek");
	printf("READ : POSITION=%6ld, ", ftell(cifsVolume));
    cifsCheckIOError("READ", "ftell");
	size_t len = fread((void * restrict)content, sizeof(unsigned char), CIFS_BLOCK_SIZE, cifsVolume);
    cifsCheckIOError("READ", "fread");
	printf("LENGTH=%4ld, CONTENT=", len); // %s will usually not work
	cifsPrintBlockContent(content);
	printf("\n");

	return content;
}

//////////////////////////////////////////////////////////////////////////
///
/// some helper functions
///
//////////////////////////////////////////////////////////////////////////

/***
 *
 * Returns a hash value within the limits of the registry.
 *
 */

inline unsigned long hash(const char* str)
{

	register unsigned long hash = 5381;
	register unsigned char c;

	while ((c = *str++) != '\0')
		hash = ((hash << 5) + hash) ^ c; /* hash * 33 + c */

	return hash % CIFS_REGISTRY_SIZE;
}

/***
 *
 * Find a free block in a bit vector.
 *
 */
inline CIFS_INDEX_TYPE cifsFindFreeBlock(const unsigned char* bitvector)
{

	unsigned int i = 0;
	while (bitvector[i] == 0xFF)
		i += 1;

	register unsigned char mask = 0x80;
	CIFS_INDEX_TYPE j = 0;
	while (bitvector[i] & mask)
	{
		mask >>= 1;
		++j;
	}

	return (i * 8) + j; // i bytes and j bits are all "1", so this formula points to the first "0"
}

/***
 *
 * Three functions for bit manipulation.
 *
 */
inline void cifsFlipBit(unsigned char* bitvector, CIFS_INDEX_TYPE bitIndex)
{

	CIFS_INDEX_TYPE blockIndex = bitIndex / 8;
	CIFS_INDEX_TYPE bitShift = bitIndex % 8;

	register unsigned char mask = 0x80;
	bitvector[blockIndex] ^= (mask >> bitShift);
}

inline void cifsSetBit(unsigned char* bitvector, CIFS_INDEX_TYPE bitIndex)
{

	CIFS_INDEX_TYPE blockIndex = bitIndex / 8;
	CIFS_INDEX_TYPE bitShift = bitIndex % 8;

	register unsigned char mask = 0x80;
	bitvector[blockIndex] |= (mask >> bitShift);
}

inline void cifsClearBit(unsigned char* bitvector, CIFS_INDEX_TYPE bitIndex)
{

	CIFS_INDEX_TYPE blockIndex = bitIndex / 8;
	CIFS_INDEX_TYPE bitShift = bitIndex % 8;

	register unsigned char mask = 0x80;
	bitvector[blockIndex] &= ~(mask >> bitShift);
}

/***
 *
 * Generates random readable/printable content for testing
 *
 */

char* cifsGenerateContent(int size)
{

	size = (size <= 0 ? rand() % 1000 : size); // arbitrarily chosen as an example

	char* content = malloc(size);

	int firstPrintable = ' ';
	int len = '~' - firstPrintable;

	for (int i = 0; i < size - 1; i++)
		*(content + i) = firstPrintable + rand() % len;

	content[size - 1] = '\0';
	return content;
}


/*******
 * Some extra helper functions
 */
int doesFileExist(char* filePath) {
	// Get list of entries at index of current file in reg
	CIFS_REGISTRY_ENTRY_TYPE* cur = cifsContext->registry[hash(filePath)];

	// Traverse collision list looking for duplicates 
	while (cur != NULL) {
		// TODO: NEED TO COMPARE PARENT FILES

		// DEBUG printf("DOES FILE EXIST: %s == %s\n", cur->fileDescriptor.name, filePath);

		// If strings are the same
		if (strcmp(cur->fileDescriptor.name, filePath) == 0) {
			return CIFS_DUPLICATE_ERROR;
		}
		cur = cur->next;
	}

	return CIFS_NO_ERROR;
}

/*
* Traverses Disk and adds all fd to registry (calls function (traverseFolder) to add contents of folder)
* Note all items in index block are fd (of folder or file)
*/
//void traverseDisk(CIFS_INDEX_TYPE* index, int size, char* path) {
void addIndex(CIFS_INDEX_TYPE* index, int size, CIFS_FILE_HANDLE_TYPE parentFileHandle) {
	// TODO: implement
	// Traverse multiple index blocs (if there are)
	for (int ind = 0; ind < (size / CIFS_INDEX_SIZE) + 1; ind++) {
		// Traverse one index block adding its contents to registry 
		for (int i = 0; (i < CIFS_INDEX_SIZE) && (ind * CIFS_INDEX_SIZE + i < size); i++) {
			CIFS_BLOCK_TYPE* curFD = (CIFS_BLOCK_TYPE*) cifsReadBlock(index[i]);
			
			// If we are on the last element of an index block (TODO: Which I assume is a reference to the next index block)
			if (curFD->type == CIFS_INDEX_CONTENT_TYPE && i + 1 == CIFS_INDEX_SIZE) { 
				// Change index to next index block
				index = curFD->content.index;
			}
			// If file (add to reg) 
			else if (curFD->type == CIFS_FILE_CONTENT_TYPE) {
				addToHashTable(&(curFD->content.fileDescriptor), parentFileHandle);
			}
			// If folder (traverse folder)
			else if (curFD->type == CIFS_FOLDER_CONTENT_TYPE) {
				// Add contents of folders to registry
				traverseFolder(curFD->content.fileDescriptor.file_block_ref, parentFileHandle);
			}

			free(curFD);
		}
	}
		
}


//void addToHashTable(long index, char* filePath, CIFS_FILE_DESCRIPTOR_TYPE* fd)
//void addToHashTable(long index, CIFS_FILE_DESCRIPTOR_TYPE* fd)
void addToHashTable(CIFS_FILE_DESCRIPTOR_TYPE* fd, CIFS_FILE_HANDLE_TYPE parentFileHandle) 
{
	// Get reg index of new node
	unsigned long regIndex = hash(fd->name);

	// Create new registry node
	CIFS_REGISTRY_ENTRY_TYPE* node = calloc(1, sizeof(CIFS_REGISTRY_ENTRY_TYPE));
	node->fileDescriptor = *fd;
	node->parentFileHandle = parentFileHandle;
	node->next = cifsContext->registry[regIndex];  // Save linked list of regEntries (in case of collision)

	// Put new node in reg
	cifsContext->registry[regIndex] = node;
}

// Meant to be called on folders. Adds folder then passes its index to addIndex (addIndex calls this function on any directories found) 
void traverseFolder(CIFS_INDEX_TYPE discIndex, CIFS_FILE_HANDLE_TYPE parentFileHandle) {
	CIFS_BLOCK_TYPE* curFD = (CIFS_BLOCK_TYPE*) cifsReadBlock(discIndex); // TODO: add index (also make sure to free)
	CIFS_BLOCK_TYPE* curInd = (CIFS_BLOCK_TYPE*) cifsReadBlock(curFD->content.fileDescriptor.block_ref); 

	// Add folder to reg
	addToHashTable(&(curFD->content.fileDescriptor), parentFileHandle);

	// Add folder index to reg NOTE: add index, we pass curFD.name as parent bcs the stuff in index has curFD.name as parent
	addIndex(curInd->content.index, curFD->content.fileDescriptor.size, hash(curFD->content.fileDescriptor.name));

	// Free blocks read
	free(curFD);
	free(curInd);
}

void writeBvSb(void) {
	// write bitvector (Bv)
	unsigned char block[CIFS_BLOCK_SIZE];
	for (int i = 0; i < CIFS_SUPERBLOCK_INDEX; i++) {
		for (int j = 0; j < CIFS_BLOCK_SIZE; j++) {
			block[j] = cifsContext->bitvector[i * CIFS_BLOCK_SIZE + j];
		}
		cifsWriteBlock((const unsigned char *) block, i);
	}

	// write superblock (Sb)
	cifsWriteBlock((const unsigned char *) cifsContext->superblock, CIFS_SUPERBLOCK_INDEX);
}
