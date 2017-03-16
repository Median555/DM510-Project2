#include <stdio.h>
#include <errno.h>
#include "arch/x86/include/generated/uapi/asm/unistd_64.h"
#include <stdlib.h>
#include <sys/mman.h>
#include <malloc.h>
#include <unistd.h>
#include <string.h>

// Put a message in the message box
int put(char* buffer, int length)
{
	return syscall(__NR_msg_put, buffer, length);
}

// Get a message from the message box
int get(char* buffer, int length)
{
	return syscall(__NR_msg_get, buffer, length);
}

// Removes all messages from stack
void flush()
{
	int size = 1 << 10;
    char *buf = (char *)malloc(size * sizeof(char));

	while (get(buf, size) >= 0){;}
}

// TEST: proper message transfer
void testFunctional()
{
	char *msg1 = "test1";
    put(msg1, 6);

    char *msg2 = "test2";
    put(msg2, 6);

    char *rec = (char *)malloc(6 * sizeof(char));

	// Expect to get the second message because of stack structure
    get(rec, 6);
    if (strcmp(msg2, rec) != 0)
	{
		if (strcmp(msg1, rec) == 0)
		{
			printf("FAILED: \"Functionality test\", order incorrect");
		}
		else
		{
			printf("FAILED: \"Functionality test\", unexpected message received");
		}
		exit(EXIT_FAILURE);
	}

    get(rec, 6);
	if (strcmp(msg1, rec) != 0)
	{
		printf("FAILED: \"Functionality test\", unexpected message received");
		exit(EXIT_FAILURE);
	}

	printf("DONE: \"Functionality test\"");
}

// TEST: Message length > 0
void testMessageLength(char *msg)
{
	int ret = put(msg, -10);
	if (ret != -EINVAL)
	{
		printf("FAILED: \"Message length > 0\" with return value %d, expected %d\n", ret, -EINVAL);
		exit(EXIT_FAILURE);
	}
	else
	{
		printf("DONE: \"Message length > 0\"\n");
	}
}

// TEST: kmalloc size check
//Kmalloc should return an error if trying to allocate too much memory
void testAllocationSize(char *msg)
{
	int ret = put(msg, 1 << 30); // 1 Gig of memory only to high in UML
	if (ret != -ENOMEM)
	{
		printf("FAILED: \"kmalloc allocate error\" with return value %d, expected %d\n", ret, -ENOMEM);
		exit(EXIT_FAILURE);
	}
	else
	{
		printf("DONE: \"kmalloc allocate error\"\n");
	}
}

// TEST: No messages (implies that no other program has used the system call before, leaving a message)
void testEmptyStack()
{
	int ret = get(NULL, 1);
	if (ret != -1)
	{
		printf("FAILED: \"No messages error\" with return value %d, expected %d\n", ret, -1);
		exit(EXIT_FAILURE);
	}
	else
	{
		printf("DONE: \"No messages error\"\n");
	}
}


// TEST: receiving buffer to small for message
void testBufferSize(char *msg, int msgLen)
{
	int ret = put(msg, msgLen); // Succesfully put message on stack
	if (ret != 0)
	{
		printf("FAILED: Setup in \"receving bufffer to small\" test with return value %d\n", ret);
		exit(EXIT_FAILURE);
	}
	ret = get(msg, -100);
	if (ret != -EMSGSIZE)
	{
		printf("FAILED: \"Receiving buffer too small\" with return value %d, expected %d\n", ret, -EMSGSIZE);
		exit(EXIT_FAILURE);
	}
	else
	{
		printf("DONE: \"Receiving buffer too small\"\n");
		char *buffer = (char *)malloc(7);
		ret = get(buffer, msgLen);
	}
}

// TEST: receiving buffer write-able
void testWriteAccess()
{
	// Code inspired from https://linux.die.net/man/2/mprotect
	/*int pagesize = sysconf(_SC_PAGESIZE); // Get the system page size
	char *buffer = memalign(pagesize, pagesize); // Allocate 2nd-argument amount of bytes with alignment a multiple of 1st-argument
	mprotect(buffer, pagesize, PROT_READ); // Only enable reading on the buffer allocated (Can only protect one whole page at a time)
	int ret = get(buffer, pagesize);
	if (ret != -EACCES)
	{
		printf("FAILED: \"Acces write check\" with return value %d, expected %d\n", ret, -EACCES);
		exit(EXIT_FAILURE);
	}
	else
	{
		printf("DONE: \"Acces write check\"\n");
		free(buffer);
	}*/
}

// Aux function used in the concurrency test
void childProcessPutChar(char c, int amount)
{
	// Char buffer
	char *b;

	// Put amount of 'c' chars in message box
	while (amount > 0)
	{
		b = (char *)malloc(1);
		*b = c;
		put(b, 1);

		free(b);
		amount--;
	}

	// Put poison pill in the end to notify parent process
	b = (char *)malloc(1);
	*b = '#';
	put(b, 1);
	free(b);

	// Kill the child
	_exit(EXIT_SUCCESS);
}

// returns 0 for success
// returns 1 if test result not valid
/*
	The test tries to provoke a race condition by simultaneously putting and
	getting from the message box. Two child processes are created, one puts '+'
	chars in the message box, the other '-' chars. They both put the same amount
	of messages in the message box. Without waiting for the children to finish,
	the parent process gets messages from the message box, incrementing a sum
	when it gets a '+', decrementing on '-'. At the end, the sum should be zero.
*/
int runConcurrently(int amount)
{
	// Create the child process that will put '+' into the message box
	pid_t pidUp = fork();
	if (pidUp == -1)
	{
		printf("FAILED: \"Concurrency test\", forking of first child failed\n");
		exit(EXIT_FAILURE);
	}
	else if (pidUp == 0)
	{
		// "Up" child process
		childProcessPutChar('+', amount);
	}
	else
	{
		// Create the child process that will put '-' into the message box
		pid_t pidDown = fork();
		if (pidDown == -1)
		{
			printf("FAILED: \"Concurrency test\", forking of second child failed\n");
			exit(EXIT_FAILURE);
		}
		else if (pidDown == 0)
		{
			// "Down" child process
			childProcessPutChar('-', amount);
		}
		else
		{
			// Sum the stack
			int poisonPillsReceived = 0;
			long count = 0;
			int ret = 0; // return value from get

			while (poisonPillsReceived < 2 || ret != -1)
			{
				char *rec = (char *)malloc(2);
				ret = get(rec, 2);
				if (ret < -1)
				{
					printf("FAILED: \"Concurrency test\", error while getting from mailbox, return value %d\n", ret);
					exit(EXIT_FAILURE);
				}

				// Handle the message
				switch (*rec)
				{
					case '+': count++; break;
					case '-': count--; break;
					case '#': poisonPillsReceived++; break;
				}

				free(rec);

				// If we received all of one char, we know that no concurrent
				//  work occurred, therefore the test did not fufill its
				//  purpose. The outer function should rerun the test then.
				if (count == amount || count == -amount)
				{
					printf("WARNING: \"Failed premiss for concurrency test\"\n");

					// Don't leave children running
					waitpid(pidUp, NULL, 0);
					waitpid(pidDown, NULL, 0);
					return 1;
				}
			}

			if (count != 0)
			{
				printf("FAILED: \"Concurrency test\", non-zero sum (%ld)\n", count);
				exit(EXIT_FAILURE);
			}
			else
			{
				printf("DONE: \"Concurrency test\"\n");
				return 0;
			}
		}
	}
}

int main(int argc, char ** argv)
{
	char *msg = "!TEST!";
	int msgLen = 7;
	int ret;

	// This test should be tested without other messages in the message box,
	//  so to be sure, remove any.
	flush();

	testFunctional();

	// Bounds tests:
	testMessageLength(msg);
	testAllocationSize(msg);
	testEmptyStack();
	testBufferSize(msg, msgLen);
	// testWriteAccess();  Does not currently work

	// Test concurrency
	while (runConcurrently(10000))
	{
		flush();
	}
}
