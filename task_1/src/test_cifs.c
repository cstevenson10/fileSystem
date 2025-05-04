//////////////////////////////////////////////////////////////////////////
///
/// Copyright (c) 2020 Prof. AJ Bieszczad. All rights reserved.
///
//////////////////////////////////////////////////////////////////////////
///
/// This source contains code that can be used only during testing without
/// FUSE.
///
/// Integration with fuse requires that you utilize the code from cifs.c
/// as needed in the FUSE framework.
///
//////////////////////////////////////////////////////////////////////////
///
/// The program can be run in three different ways:
///
/// A) with a regular file name as a command line argument:
///
///      ./cifs cifs.vol
///
/// B) Run with real block device:
///
///      sudo ./cifs /dev/sdaN  # where /dev/sdaN is the name of a real disk (which is a block device)
///
///      WARNING!
///           DO NOT RUN IT WITH YOUR MAIN DISK!!
///           PLUG IN AN EMPTY USB STICK AND FIGURE OUT WHICH DEVICE IT IS, AND THEN USE IT.
///           YOU CAN USE 'df' COMMAND TO SEE WHICH DEVICE IS ADDED AFTER YOU PLUG IN THE USB STICK.
///
/// C) Run with a simulated block device.
///
///    This way needs some preparation:
///
///    1) create a file to simulate a block device with a desired block size and a desired number (count) of blocks
///
///      dd bs=256 count=65536 if=/dev/zero of=/tmp/cifs.vol
///
///    2) find a free "loop" to interface with the file
///
///      losetup -f
///
///    3) link the loop with the file (assuming that loop11 is free)
///
///      sudo losetup /dev/loop11 /tmp/cifs.vol
///
///    4) run this program with /dev/loop11 as the command line argument
///
///      sudo ./cifs /dev/loop11
///
//////////////////////////////////////////////////////////////////////////
#define NO_FUSE_DEBUG
#ifdef NO_FUSE_DEBUG

#include "cifs.h"

extern struct fuse_context* fuseContext;
/// must use
// fuseContext = fuse_get_context();
/// when the cifs is integrated with FUSE

int main(int argc, char** argv)
{
	printf("sizeof(CIFS_BLOCK_TYPE) = %ld\n\n", sizeof(CIFS_BLOCK_TYPE));

	printf("sizeof(CIFS_CONTENT_TYPE) = %ld\n", sizeof(CIFS_CONTENT_TYPE));
	printf("sizeof(CIFS_DATA_TYPE) = %ld\n", sizeof(CIFS_DATA_TYPE));
	printf("sizeof(CIFS_INDEX_TYPE) = %ld\n\n", sizeof(CIFS_INDEX_TYPE));

	printf("sizeof(CIFS_FILE_DESCRIPTOR_TYPE) = %ld\n\n", sizeof(CIFS_FILE_DESCRIPTOR_TYPE));

	printf("sizeof(unsigned long long) = %ld\n", sizeof(unsigned long long));
	printf("sizeof(CIFS_CONTENT_TYPE) = %ld\n", sizeof(CIFS_CONTENT_TYPE));
	printf("sizeof(CIFS_NAME_TYPE) = %ld\n", sizeof(CIFS_NAME_TYPE));
	printf("sizeof(time_t) = %ld\n", sizeof(time_t));
	printf("sizeof(mode_t) = %ld\n", sizeof(mode_t));
	printf("sizeof(uid_t) = %ld\n", sizeof(uid_t));
	printf("sizeof(size_t) = %ld\n", sizeof(size_t));
	printf("sizeof(CIFS_INDEX_TYPE) = %ld\n", sizeof(CIFS_INDEX_TYPE));


	srand(time(NULL)); // uncomment to get true random values in get_context()

	// the following is just some sample code for simulating user and process identifiers that are
	// needed in the cifs functions
	fuseContext = (struct fuse_context*)malloc(sizeof(struct fuse_context));
	fuseContext->fuse = NULL;                   // struct fuse   *fuse
	fuseContext->uid = 1000 + (uid_t)rand() % 10 + 1;  // uid_t  uid
	fuseContext->gid = 1000 + (gid_t)rand() % 10 + 1;  // gid_t  gid
	fuseContext->pid = 1000 + (pid_t)rand() % 10 + 1;  // pid_t  pid
	fuseContext->private_data = NULL;                   // void   *private_data
	fuseContext->umask = S_IRUSR | S_IWUSR;             // mode_t umask

	printf("FUSE CONTEXT:\nuser ID = %02i\nprocess ID = %02i\ngroup ID = %02i\numask = %04o\n\n",
			fuseContext->uid, fuseContext->pid, fuseContext->gid, fuseContext->umask);

	if (cifsCreateFileSystem("cifs.vol") != CIFS_NO_ERROR)
		exit(EXIT_FAILURE);

	if (cifsMountFileSystem("cifs.vol") != CIFS_NO_ERROR)
		exit(EXIT_FAILURE);

	testSamples();

	// TODO: implement thorough testing of all the functionality

	testStep1();
	testStep2();
	fuseContext = (struct fuse_context*)malloc(sizeof(struct fuse_context));
	fuseContext->fuse = NULL;                   // struct fuse   *fuse
	fuseContext->uid = 1000 + (uid_t)rand() % 10 + 1;  // uid_t  uid
	fuseContext->gid = 1000 + (gid_t)rand() % 10 + 1;  // gid_t  gid
	fuseContext->pid = 1000 + (pid_t)rand() % 10 + 1;  // pid_t  pid
	fuseContext->private_data = NULL;                   // void   *private_data
	fuseContext->umask = S_IRUSR | S_IWUSR;             // mode_t umask

	testStep3();

	if (cifsUmountFileSystem("cifs.vol") != CIFS_NO_ERROR)
		exit(EXIT_FAILURE);

	return EXIT_SUCCESS;
}

/***
 *
 * develop this test function successively as you test the development of step 1
 *
 */
void testSamples()
{
	printf("\n\nSAMPLE TESTS\n============\n\n");

	unsigned long long test = INT_MAX;
	printf("MAX unique identifier: %llu\n", test);

	int count = 10;
	char* content;
	for (int i = 0; i < count; i++)
	{
		content = cifsGenerateContent(i * 10);
		printf("content = \"%s\"\nhash(content) = %ld\n", content, hash((char*)content));
	}

	unsigned char testBitVector[] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
	cifsFlipBit(testBitVector, 44);
	printf("Found free block at %d\n", cifsFindFreeBlock(testBitVector));
	cifsClearBit(testBitVector, 33);
	printf("Found free block at %d\n", cifsFindFreeBlock(testBitVector));
	cifsSetBit(testBitVector, 33);
	printf("Found free block at %d\n", cifsFindFreeBlock(testBitVector));

}

/***
 *
 * develop this test function successively as you test the development of step 1
 *
 */
void testStep1()
{
	printf("\n\nTESTS FOR STEP #1\n=================\n\n");

	free(cifsReadBlock(CIFS_SUPERBLOCK_INDEX + 2));


	char file1[128] = "file1";
	cifsCreateFile(file1, CIFS_FOLDER_CONTENT_TYPE);
	free(cifsReadBlock(CIFS_SUPERBLOCK_INDEX + 2));

	char file2[128] = "file2";
	cifsCreateFile(file2, CIFS_FOLDER_CONTENT_TYPE);
	free(cifsReadBlock(CIFS_SUPERBLOCK_INDEX + 2));

	char file3[128] = "file3";
	cifsCreateFile(file3, CIFS_FOLDER_CONTENT_TYPE);
	free(cifsReadBlock(CIFS_SUPERBLOCK_INDEX + 2));
}


/***
 *
 * develop this test function successively as you test the development of step 2
 *
 */
void testStep2()
{
	printf("\n\nTESTS FOR STEP #2\n=================\n\n");

	// TODO: implement
}

/***
 *
 * develop this test function successively as you test the development of step 3
 *
 */
void testStep3()
{
	printf("\n\nTESTS FOR STEP #3\n=================\n\n");

	// TODO: implement
}

#endif
