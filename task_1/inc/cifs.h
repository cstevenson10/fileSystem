//////////////////////////////////////////////////////////////////////////
///
/// Copyright (c) 2020 Prof. AJ Bieszczad. All rights reserved.
///
//////////////////////////////////////////////////////////////////////////

#ifndef __CIFS_H_
#define __CIFS_H_

#include <time.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <limits.h>

// can also use -D flag to pass the flag to gcc: gcc -DNO_FUSE_DEBUG ...
//#define NO_FUSE_DEBUG // TODO: comment out when integrated with FUSE

#ifdef __linux__
#include <fuse.h>
#else
#include <sys/types.h>
#include <sys/stat.h>
typedef struct fuse_context
{
    void *fuse; // in place of struct fuse *fuse,
    uid_t uid;
    gid_t gid;
    pid_t pid;
    void * private_data;
    mode_t umask;
} fuse_context;
#endif

//////////////////////////////////////////////////////////////////////////
/***

 defines sizes for the volume

 To accommodate less capable systems, the numbers are lower than desired
 that are commented out

*/
//////////////////////////////////////////////////////////////////////////

#define CIFS_BLOCK_SIZE 256 // has to be large enough to hold the superblock
#define CIFS_NUMBER_OF_BLOCKS 65535 // 2^16 - 1
#define CIFS_MAX_NAME_LENGTH 128
#define CIFS_DATA_SIZE 254 // CIFS_BLOCK_SIZE - sizeof(CIFS_CONTENT_TYPE)
#define CIFS_INDEX_SIZE 127 // two bytes => x0000 - xFFFF => 2^16 range

//////////////////////////////////////////////////////////////////////////
/***

 each file/folder gets a unique identifier, and this is the initial value
 the unique identifier of the root folder will be assigned this value

*/
//////////////////////////////////////////////////////////////////////////

#define CIFS_INITIAL_VALUE_OF_THE_UNIQUE_FILE_IDENTIFIER 0

//////////////////////////////////////////////////////////////////////////
/***

 defines for the in-memory data structures

*/
//////////////////////////////////////////////////////////////////////////

#define CIFS_REGISTRY_SIZE 65537 // prime number for the size of the registry (it is larger than 2^16)

//////////////////////////////////////////////////////////////////////////
/***

 data structures for "physical" file system

*/
//////////////////////////////////////////////////////////////////////////

enum
{
	CIFS_FOLDER_CONTENT_TYPE,
	CIFS_FILE_CONTENT_TYPE,
	CIFS_INDEX_CONTENT_TYPE,
	CIFS_DATA_CONTENT_TYPE,
	CIFS_INVALID_CONTENT_TYPE
};

typedef unsigned short CIFS_CONTENT_TYPE;

typedef unsigned short CIFS_INDEX_TYPE; // is used to index blocks in the file system
#define CIFS_INVALID_INDEX CIFS_NUMBER_OF_BLOCKS // must be excluded from the range for block indices

/***

 superblock starting block in the whole file system

 cifsNextUniqueIdentifier
        is the next available value fot the unique identifier for a new folder or file
        it is a unique very large value from [0, UINTMAX_MAX] same as [0, -1]
        UINTMAX_MAX == 2^64 - 1 == 18,446,744,073,709,551,615
 cifsRootNodeIndex points to the block which is the root folder of the files system
 numberOfBlock determines the size of the file system
 cifsDataBlockSize is the size of a single block of the file system

 */

// superblock is located in the first block after the bitvector
#define CIFS_SUPERBLOCK_INDEX CIFS_NUMBER_OF_BLOCKS/8/CIFS_BLOCK_SIZE

typedef struct cifs_superblock_type
{
	unsigned long long cifsNextUniqueIdentifier; // unique identifier generator for files and folders
	CIFS_INDEX_TYPE cifsNumberOfBlocks;
	CIFS_INDEX_TYPE cifsDataBlockSize;
	CIFS_INDEX_TYPE cifsRootNodeIndex; // should point to the first block after the last bitvector block
} CIFS_SUPERBLOCK_TYPE;

/***

 file descriptor node for blocks holding folder or file information

   for files:
       te size indicates the size of the file
       the block reference is initialized to CIFS_INVALID_INDEX
           - it will point to an index block when the file has content

   for directories:
       the size indicates the number of files or directories in this folder
       the block reference points to an index block that holds references to the file and folder blocks

*/
typedef char CIFS_NAME_TYPE[CIFS_MAX_NAME_LENGTH]; // for folder and file names

typedef struct cifs_file_descriptor_type
{
	unsigned long long identifier; // unique folder/file identifier
	CIFS_CONTENT_TYPE type; // folder or file
	CIFS_NAME_TYPE name;
	time_t creationTime; // creation time
	time_t lastAccessTime; // last access
	time_t lastModificationTime; // last modification
	mode_t accessRights; // access rights for the file
	uid_t owner; // owner ID
	size_t size;
	CIFS_INDEX_TYPE block_ref; // reference to the data or index block
   CIFS_INDEX_TYPE parent_block_ref; // reference to the holding folder
	CIFS_INDEX_TYPE file_block_ref; // reference to the block itself
} CIFS_FILE_DESCRIPTOR_TYPE;

/***

 a block for holding data

*/
typedef char CIFS_DATA_TYPE[CIFS_DATA_SIZE];

/***

 various interpretations of a file system block

*/
typedef struct cifs_block_type
{
	CIFS_CONTENT_TYPE type;
	union
	{ // content depends on the type
		CIFS_FILE_DESCRIPTOR_TYPE fileDescriptor __attribute__((packed)); // for directories and files
		CIFS_DATA_TYPE data; // for data
		CIFS_INDEX_TYPE index[CIFS_INDEX_SIZE];  // for indices; all indices but the last point to data blocks
		// the last points to another index block
	} content;
} CIFS_BLOCK_TYPE;

//////////////////////////////////////////////////////////////////////////
/***

 definitions for in-memory data structures supporting the file system

*/
//////////////////////////////////////////////////////////////////////////

/***
 *
 * file handle is simply an index to the registry for that file
 */
typedef int CIFS_FILE_HANDLE_TYPE;

/***

 file system registry

 registry entry in the conflict resolution linked list with the head in the hash table slot for
 the corresponding name

*/
typedef struct cifs_registry_ent_type
{
    // a copy of the file descriptor node from the volume
    CIFS_FILE_DESCRIPTOR_TYPE fileDescriptor;
    // file handle of the holding folder
    CIFS_FILE_HANDLE_TYPE parentFileHandle;
	// reference count; increased on each new process opening the file; decreased on file close
	int referenceCount; // if not zero, cannot delete file
	struct cifs_registry_ent_type* next;
} CIFS_REGISTRY_ENTRY_TYPE;

/***

 registry implemented as a hash table

 slots are heads to resolution lists for conflicting names

 created on filesystem mount

*/
typedef CIFS_REGISTRY_ENTRY_TYPE* CIFS_REGISTRY;

/***
 *
 * When a file is opened, an entry is added to this list.
 * When the file is closed, the corresponding entry is removed from the list.
 *
 */
typedef struct open_file_type
{
    unsigned long long identifier; // unique folder/file identifier
    CIFS_FILE_HANDLE_TYPE fileHandle; // index to the entry for the file in the in-memory registry; set on open
    mode_t processAccessRights; // must be computed and set when opening the file
    struct open_file_type *next;
} OPEN_FILE_TYPE;

/***
 *
 * In an actual OS, we would have access to the Process Control Block (PCB) that would have all
 * information about processes (including open files, etc.)
 *
 * The following is just a small subset that is needed to manage open files and keeping track of
 * the current directory.
 *
 */
typedef struct cifs_process_control_block_type
{
    pid_t pid; // process identifier
    // a list of references to all open files for the process
    OPEN_FILE_TYPE *openFiles;
    // arbitrarily, we assume that the first entry is always the current working directory 	// current directory of a process should be initialized to the root of the volume
    // and then changed on each 'cd'
    // 'cd' generates 'get attribute' system call for that directory that is redirected
    // to the corresponding FUSE function
    struct cifs_process_control_block_type* next;
} CIFS_PROCESS_CONTROL_BLOCK_TYPE;

/***

 file system context

 holds the registry and an in-memory copy of the volume bitvector

 the registry is created when mounting the system (through traversal)
 the bitvector is copied from the volume when mounting

 all access to the files is through the registry (create, delete, get info, read, write)

 the in-memory bitvector must be used when the system is mounted, but updated on the disc
 on every successful creation, deletion, write, and read

 elements are added to the list of processes when they successfully open files; when a file is closed
 (by the same process), then the entry is removed

*/
typedef struct cifs_context_type
{
	CIFS_SUPERBLOCK_TYPE* superblock; // holds a copy of the volume superblock
	unsigned char* bitvector; // an in-memory copy of the bitvector of the volume
	CIFS_REGISTRY* registry; // the hashtable-based in-memory registry
	CIFS_PROCESS_CONTROL_BLOCK_TYPE* processList; // a list of processes that opened files
} CIFS_CONTEXT_TYPE;

//////////////////////////////////////////////////////////////////////////
/***

 file system function declarations

*/
//////////////////////////////////////////////////////////////////////////

typedef enum cifs_error
{
	CIFS_NO_ERROR,
	CIFS_ALLOC_ERROR,
	CIFS_DUPLICATE_ERROR,
	CIFS_NOT_FOUND_ERROR,
	CIFS_NOT_EMPTY_ERROR,
	CIFS_ACCESS_ERROR,
	CIFS_WRITE_ERROR,
	CIFS_READ_ERROR,
	CIFS_IN_USE_ERROR,
	CIFS_OPEN_ERROR,
	CIFS_SYSTEM_ERROR
} CIFS_ERROR;

CIFS_ERROR cifsCreateFileSystem(char* cifsFileSystemName);

CIFS_ERROR cifsUmountFileSystem(char* cifsFileSystemName);

CIFS_ERROR cifsMountFileSystem(char* cifsFileSystemName);

CIFS_ERROR cifsCreateFile(CIFS_NAME_TYPE filePath, CIFS_CONTENT_TYPE type);

CIFS_ERROR cifsDeleteFile(CIFS_NAME_TYPE filePath);

CIFS_ERROR cifsOpenFile(CIFS_NAME_TYPE filePath, mode_t desiredAccessRights, CIFS_FILE_HANDLE_TYPE *fileHandle);

CIFS_ERROR cifsCloseFile(CIFS_FILE_HANDLE_TYPE fileHandle);

CIFS_ERROR cifsGetFileInfo(CIFS_NAME_TYPE filePath, CIFS_FILE_DESCRIPTOR_TYPE* infoBuffer);

CIFS_ERROR cifsWriteFile(CIFS_FILE_HANDLE_TYPE fileHandle, char* writeBuffer);

CIFS_ERROR cifsReadFile(CIFS_FILE_HANDLE_TYPE fileHandle, char** readBuffer);

/***
 *
 * Functions for reading and writing a single block from and to a block device.
 *
 */
size_t cifsWriteBlock(const unsigned char* content, CIFS_INDEX_TYPE blockNumber);
unsigned char* cifsReadBlock(CIFS_INDEX_TYPE blockNumber);
void cifsCheckIOError(const char* who, const char* what);
void cifsPrintBlockContent(const unsigned char *str);

/***
 *
 * the following are utility functions that will come handy in the implementation
 *
 */
unsigned long hash(const char* str);

CIFS_INDEX_TYPE cifsFindFreeBlock(const unsigned char* bitvector);

void cifsFlipBit(unsigned char* bitvector, unsigned short bitIndex);

void cifsSetBit(unsigned char* bitvector, unsigned short bitIndex);

void cifsClearBit(unsigned char* bitvector, unsigned short bitIndex);


// Extra Helper Functions
void traverseDisk(CIFS_INDEX_TYPE* index, int size, char* path);
void addToHashTable(long index, char* filePath, CIFS_FILE_DESCRIPTOR_TYPE* fd);
int doesFileExist(char* filePath);
void writeBvSb(void);


/***
 * The following functions can be used to simulate FUSE context's user and process identifiers for testing.
 *
 * These identifiers are obtainable by calling fuse_get_context() the fuse library.
 */

#ifdef NO_FUSE_DEBUG

char* cifsGenerateContent(int size);
void testSamples();
void testStep1();
void testStep2();
void testStep3();

#endif
#endif

