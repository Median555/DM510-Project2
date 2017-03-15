#include <stdio.h>
#include <errno.h>
#include "arch/x86/include/generated/uapi/asm/unistd_64.h"
#include <stdlib.h>
#include <sys/mman.h>
#include <malloc.h>
#include <unistd.h>

int put(char* buffer, int length)
{
	return syscall(__NR_msg_put, buffer, length);
}

int get(char* buffer, int length)
{
	return syscall(__NR_msg_get, buffer, length);
}

// removes all messages from stack
void flush()
{
	int size = 1 << 10;
    char *buf = (char *)malloc(size * sizeof(char));

	while (get(buf, size) >= 0){;}
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
	//code inspired from https://linux.die.net/man/2/mprotect
	//with some changes from http://www.tutorialspoint.com/unix_system_calls/mprotect.htm
	int pagesize = sysconf(_SC_PAGESIZE); // Get the system page size
	char *buffer = (char *)malloc(1024 + pagesize - 1); // Allocate 2nd-argument amount of bytes with alignment a multiple of 1st-argument
	buffer = (char *)(((int) buffer + pagesize - 1) & ~(pagesize - 1));
	mprotect(buffer, 1024, PROT_READ); // Only enable reading on the buffer allocated (Can only protect one whole page at a time)
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
	}
}

void childProcessPutChar(char c, int amount)
{
	// Put poison pill first, will be last to counter because of stack structure
	char *b = (char *)malloc(1);
	*b = '#';
	put(b, 1);
	free(b);

	while (amount > 0)
	{
		b = (char *)malloc(1);
		*b = c;
		put(b, 1);

		free(b);
		amount--;
	}

	b = (char *)malloc(1);
	*b = '#';
	put(b, 1);
	free(b);

	_exit(EXIT_SUCCESS);
}

// returns 0 for success
// returns 1 if test result not valid
int runConcurrently(int amount)
{
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

			while (poisonPillsReceived < 4)
			{
				char *rec = (char *)malloc(2);
				int temp = get(rec, 2);
				if (temp < -1)
				{
					printf("FAILED: \"Concurrency test\", error while getting from mailbox, return value %d\n", temp);
					exit(EXIT_FAILURE);
				}

				switch (*rec)
				{
					case '+': count++; break;
					case '-': count--; break;
					case '#': poisonPillsReceived++; break;
				}

				if (count == amount || count == -amount)
				{
					printf("FAILED: \"Failed premiss for concurrency test\"\n");
					waitpid(pidUp, NULL, 0);
					waitpid(pidDown, NULL, 0);
					return 1;
				}

				free(rec);
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

	flush();

	//Bounds tests:
	testMessageLength(msg);
	testAllocationSize(msg);
	testEmptyStack();
	testBufferSize(msg, msgLen);
	// testWriteAccess();  Does not currently work

	// test concurrency
	while (runConcurrently(10000))
	{
		flush();
	}



	// TEST: proper message transfer

	// TODO: test if ==

	// concurrency test


    /*char *msg = "Hej!";
    syscall(__NR_msg_put, msg, 5);

    msg = "Kage!";
    syscall(__NR_msg_put, msg, 6);

    char *get = (char *)malloc(6 * sizeof(char));
    syscall(__NR_msg_get, get, 6);
    printf("Msg: %s\n", get);

    free(get);
    get = (char *)malloc(5 * sizeof(char));
    syscall(__NR_msg_get, get, 5);
    printf("Msg: %s\n", get);*/
}






/*

Checks:

Put:
length < 0
access ok for reading
kmalloc success

Get:
any messages
destination buffer to small for message
access ok for destination buffer writing


Test for concurrency
*/
