//////////////////////////////////////////////////////////////////////////
///
/// Copyright (c) 2020 Prof. AJ Bieszczad. All rights reserved.
///
//////////////////////////////////////////////////////////////////////////
////
/// Code demonstrating reading and writing blocks from and to block devices
///
/// The program can be run in three different ways:
///
/// A) with a regular file name as a command line argument:
///
///      ./volume my.vol
///
/// B) Run with real block device:
///
///      sudo ./volume /dev/sdaN  # where /dev/sdaN is the name of a real disk (which is a block device)
///
///      WARNING!
///           DO NOT RUN IT WITH YOUR MAIN DISK!!
///           PLUG IN AN EMPTY USB STICK AND FIGURE OUT WHICH DEVICE IT IS, AND THEN USE IT.
///           YOU CAN USE 'df' COMMAND TO SEE WHICH DEVICE IS ADDED AFTER YOU PLUG IN THE USB STICK.
///
/// C) Run with a simulated block device.
///
/// This way needs some preparation:
///
///    1) create a file to simulate a block device with a desired block size and a desired number (count) of blocks
///
///      dd bs=16 count=4096 if=/dev/zero of=/tmp/my.vol
///
///    2) find a free "loop" to interface with the file
///
///      losetup -f
///
///    3) link the loop with the file (assuming that loop11 is free)
///
///      sudo losetup /dev/loop11 /tmp/my.vol
///
///    4) run this program with /dev/loop11 as the command line argument
///
///      sudo ./volume /dev/loop11
///
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define BLOCK_SIZE 16

typedef unsigned short BLOCK_REFERENCE_TYPE;

FILE* volume;

void checkIOError(const char* who, const char* what)
{
	int errCode = ferror(volume);
	if (errCode == 0)
		return;
	printf("%s: %s returned \"%s\"\n", who, what, strerror(errCode));
	exit(errCode);
}

void printBlockContent(const unsigned char* str)
{
	for (int i = 0; i < BLOCK_SIZE; i++)
		printf("0x%02x ", *(str + i));
}

size_t writeBlock(unsigned char* content, BLOCK_REFERENCE_TYPE blockNumber)
{
	fseek(volume, blockNumber * BLOCK_SIZE, SEEK_SET);
	checkIOError("WRITE", "fseek");
	printf("WRITE: POSITION=%5ld, ", ftell(volume));
	checkIOError("WRITE", "ftell");
	size_t len = fwrite(content, sizeof(unsigned char), BLOCK_SIZE, volume);
	checkIOError("WRITE", "fwrite");

	printf("LENGTH=%3d, CONTENT=", BLOCK_SIZE);
	printBlockContent(content);
	printf("\n");

	return len;
}

unsigned char* cifsReadBlock(BLOCK_REFERENCE_TYPE blockNumber)
{
	unsigned char* content = malloc(BLOCK_SIZE);

	fseek(volume, blockNumber * BLOCK_SIZE, SEEK_SET);
	checkIOError("READ", "fseek");
	printf("READ:  POSITION=%5ld, ", ftell(volume));
	checkIOError("READ", "ftell");
	size_t len = fread((void* restrict)content, sizeof(unsigned char), BLOCK_SIZE, volume);
	checkIOError("READ", "fread");

	printf("LENGTH=%3ld, CONTENT=", len);
	printBlockContent(content);
	printf("\n");

	return content;
}

int main(int argc, char* argv[])
{
	if (argc < 2)
	{
		printf("Error args\n");
		return 0;
	}

	// OPEN VOLUME FOR READING AND WRITING WITH APPENDING

	volume = fopen(argv[1], "w+");
	checkIOError("OPEN", "fopen");

	unsigned char buf[BLOCK_SIZE + 1];
	for (int i = 0; i < 10; i++)
	{
		memset(buf, 'a' + i, BLOCK_SIZE);

		size_t len = writeBlock(buf, i);
		if (len != BLOCK_SIZE)
			printf("ERROR writing to BLOCK #%3d\n", i);
	}

	// DONE WRITING VOLUME

	// REWIND THE VOLUME

	rewind(volume);
	checkIOError("OPEN", "rewind");
	printf("REWIND VOLUME\n");

	// READ WHAT WAS JUST WRITTEN TO THE VOLUME

	for (int i = 0; i < 5; i++)
        cifsReadBlock(i);

	// CLOSE

	fclose(volume);
	checkIOError("CLOSE", "fclose");
	printf("CLOSE VOLUME\n");

	// RE-OPEN THE VOLUME

	volume = fopen(argv[1], "rw+");
	checkIOError("OPEN", "fopen");
	printf("OPEN VOLUME\n");

	for (int i = 0; i < 5; i++)
	{
		unsigned char* content = cifsReadBlock(i);
		if (content != NULL)
		{
			content[BLOCK_SIZE] = '\0';
			printf("CONTENT=%s read from BLOCK #%3d\n", content, i);
			free(content);
		}
		else
			printf("ERROR reading from BLOCK #%3d\n", i);
	}

	fclose(volume);

	exit(EXIT_SUCCESS);
}