#include "linux/kernel.h"
#include "linux/unistd.h"
#include <linux/slab.h> // Kernel memory management
#include <linux/uaccess.h> // User address translation and stuff
#include "linux/errno.h"

typedef struct _msg_t msg_t;

struct _msg_t
{
	msg_t* previous;
	int length;
	char* message;
};

static msg_t *bottom = NULL;
static msg_t *top = NULL;

asmlinkage
int dm510_msgbox_put( char *buffer, int length )
{
	unsigned long flags;
	// Length check
	if (length < 0)
	{
		return -EINVAL;
	}

	// Access check, read from buffer
	if (!access_ok(VERIFY_READ, buffer, length))
	{
		return -EACCES;
	}

	msg_t* msg = kmalloc(sizeof(msg_t), GFP_KERNEL);
	// Check return value of kmalloc
	if (msg == NULL)
	{
		return -ENOMEM;
	}
	msg->previous = NULL;
	msg->length = length;
	msg->message = kmalloc(length, GFP_KERNEL);
	// Check return value of kmalloc
	if (msg->message == NULL)
	{
		kfree(msg);
		return -ENOMEM;
	}
	copy_from_user(msg->message, buffer, length);

	// Lock critical region
	local_irq_save(flags);
	if (bottom == NULL)
	{
		bottom = msg;
		top = msg;
	}
	else
	{
		// not empty stack
		msg->previous = top;
		top = msg;
	}
	local_irq_restore(flags);

	return 0;
}

asmlinkage
int dm510_msgbox_get( char* buffer, int length )
{
	unsigned long flags;
	// Lock critical region
	local_irq_save(flags);
	if (top != NULL)
	{
		if (top->length > length)
		{
			// Return error code for receiving buffer to small
			// Unlock before returning
			local_irq_restore(flags);
			return -EMSGSIZE;
		}

		// Access check, write from buffer
		if (!access_ok(VERIFY_WRITE, buffer, length))
		{
			local_irq_restore(flags);
			return -EACCES;
		}

		msg_t* msg = top;
		top = msg->previous;
		local_irq_restore(flags);

		int mlength = msg->length;

		// copy message
		copy_to_user(buffer, msg->message, mlength);

		// free memory
		kfree(msg->message);
		kfree(msg);

		return mlength;
	}
	local_irq_restore(flags);
	return -1;
}
