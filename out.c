#include "arch/x86/include/generated/uapi/asm/unistd_64.h"
#include <stdio.h>
#include <stdlib.h>

int get(char* buffer, int length)
{
	return syscall(__NR_msg_get, buffer, length);
}

int main(int argc, char ** argv)
{
	int count = 0;
	int size = 1 << 10;
    char *buf = (char *)malloc(size * sizeof(char));

	while (get(buf, size) >= 0)
	{
		printf("%d: \"%s\"\n", ++count, buf);
	}
}
