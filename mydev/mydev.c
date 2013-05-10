/* 
 * mydev.c - MYDEV device driver
 *
 * Copyright (C) 2011 Ericsson AB
 *
 * Author: Paolo Rovelli <paolo.rovelli@ericsson.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#define DEBUG

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/wait.h>
#include <linux/poll.h>
#include <linux/fcntl.h>
#include <asm/uaccess.h>

#include "mydev.h"
#include "mybuff.h"

#define MYDEV_MAX_NUM 64
#define MYDEV_MAX_SIZE (128*1024) /* max 128 Kbyte (kmalloc limit) */

#define MYDEV_NAME "mydev"
#define MYDEV_NAMES "mydev%d"

/* mydev module parameters */
static unsigned int mydev_no = 1;
module_param(mydev_no, uint, 0644);
MODULE_PARM_DESC(mydev_no, "mydev instances (default 1)");

static unsigned int mydev_size = 1024;
module_param(mydev_size, uint, 0644);
MODULE_PARM_DESC(mydev_size, "mydev buffer size (default 1 KByte)");

static dev_t mydev_id;
static unsigned int mydev_major = 0; /* let the system to choose the major */
static unsigned int mydev_minor = 0; /* start allocating from minor 0 */

static struct class *mydev_class;
static struct device *mydev_device;

static struct mydev_info {
	struct mybuff *mybuff;
	struct cdev cdev;
	struct mutex lock;
	wait_queue_head_t in_queue; /* queue of processes suspended on read */
	wait_queue_head_t out_queue; /* queue of processes suspended on write */
	struct fasync_struct *async_queue; /* asynchronous readers */
} *mydev;

/**
 * mydev_open - Opens the MYDEV device.
 * @inode: inode pointer
 * @file: file pointer
 * 
 * Returns 0 if no error, standard error number otherwise.
 */
static int mydev_open(struct inode *inode, struct file *file)
{
	struct mydev_info *mydev;

	pr_debug("%s(inode %p, file %p, flags %x)\n",
		__func__, inode, file, file->f_flags);

	/* retrieve the reference to mydev from the inode and save it */
	mydev = container_of(inode->i_cdev, struct mydev_info, cdev);
	file->private_data = mydev;

	return 0;
}

/**
 * mydev_close - Closes the MYDEV device.
 * @inode: inode pointer
 * @file: file pointer
 * 
 * Returns 0 if no error, standard error number otherwise.
 */
static int mydev_close(struct inode *inode, struct file *file)
{
	struct mydev_info *mydev = file->private_data;

	pr_debug("%s(inode %p, file %p)\n",  __func__, inode, file);

	return fasync_helper(-1, file, 0, &mydev->async_queue);
}

/**
 * mydev_read - Reads from the MYDEV device.
 * @file: file pointer
 * @buf: user buffer pointer
 * @count: size of the requested data transfer
 * @offp: file position pointer (not used)
 * 
 * Returns 0 if no error, standard error number otherwise.
 */
static ssize_t mydev_read(struct file *file, char __user *buf,
	size_t count, loff_t *offp)
{
	struct mydev_info *mydev = file->private_data;
	char *tmpbuf;
	ssize_t retval;

	pr_debug("%s(file %p, buf %p, size %d, off %p)\n",
		__func__, file, buf, count, offp);

	if (mutex_lock_interruptible(&mydev->lock)) {
		retval = -ERESTARTSYS;
		goto err1;
	}

	/* while there is no data in the buffer */
	while (mybuff_ready(mydev->mybuff) == 0) {
		mutex_unlock(&mydev->lock);
		/* return immediately if mydev is open in non-blocking mode */
		if (file->f_flags & O_NONBLOCK) {
			pr_debug("%s(): no data, return on reading\n", __func__);
			retval = -EAGAIN;
			goto err1;
		}
		/* else suspend if mydev is open in blocking (default) mode */
		pr_debug("%s(): no data, \"%s\" reading, going to sleep\n",
			__func__, current->comm);
		if (wait_event_interruptible(mydev->in_queue, 
			mybuff_ready(mydev->mybuff) != 0)) {
			retval = -ERESTARTSYS;
			goto err1;
		}
		/* reaquire the lock to check that data is really in the buffer */
		if (mutex_lock_interruptible(&mydev->lock)) {
			retval = -ERESTARTSYS;
			goto err1;
		}
	}

	tmpbuf = kmalloc(count, GFP_KERNEL);
	if (!tmpbuf) {
		retval = -ENOMEM;
		goto err2;
	}
	retval = mybuff_read(mydev->mybuff, tmpbuf, count);
	pr_debug("%s(): read %d bytes of %d \n", __func__, retval, count);
	if (copy_to_user(buf, tmpbuf, retval)) {
		retval = -EFAULT;
		goto err3;
	}
	kfree(tmpbuf);

	mutex_unlock(&mydev->lock);

	/* awake any writer, there is now room in the buffer */
	wake_up_interruptible(&mydev->out_queue);
	/* and signal asynchronous writers */
	if (mydev->async_queue)
		kill_fasync(&mydev->async_queue, SIGIO, POLL_OUT);

	return retval;

err3:
	kfree(tmpbuf);
err2:
	mutex_unlock(&mydev->lock);
err1:
	return retval;
}

/**
 * mydev_write - Writes to the MYDEV device.
 * @file: file pointer
 * @buf: user buffer pointer
 * @count: size of the requested data transfer
 * @offp: file position pointer (not used)
 * 
 * Returns 0 if no error, standard error number otherwise.
 */
static ssize_t mydev_write(struct file *file, const char __user *buf,
	size_t count, loff_t *offp)
{
	struct mydev_info *mydev = file->private_data;
	char *tmpbuf;
	ssize_t retval;

	pr_debug("%s(file %p, buf %p, size %d, off %p)\n",
		__func__, file, buf, count, offp);

	if (mutex_lock_interruptible(&mydev->lock)) {
		retval = -ERESTARTSYS;
		goto err1;
	}

	/* while there is no room in the buffer */
	while (mybuff_free(mydev->mybuff) == 0) {
		/* release the lock */
		mutex_unlock(&mydev->lock);
		/* return immediately if mydev is open in non-blocking mode */
		if (file->f_flags & O_NONBLOCK) {
			pr_debug("%s(): no room, return on writing\n", __func__);
			retval = -EAGAIN;
			goto err1;
		}
		/* suspend if mydev is open in blocking (default) mode */
		pr_debug("%s(): no room, \"%s\" writing, going to sleep\n",
			__func__, current->comm);
		if (wait_event_interruptible(mydev->out_queue, 
			mybuff_free(mydev->mybuff) != 0)) {
			retval = -ERESTARTSYS;
			goto err1;
		}
		/* reaquire the lock to check that data is really in the buffer */
		if (mutex_lock_interruptible(&mydev->lock)) {
			retval = -ERESTARTSYS;
			goto err1;
		}
	}

	tmpbuf = kmalloc(count, GFP_KERNEL);
	if (!tmpbuf) {
		retval = -ENOMEM;
		goto err2;
	}
	if (copy_from_user(tmpbuf, buf, count)) {
		retval = -EFAULT;
		goto err3;
	}
	retval = mybuff_write(mydev->mybuff, tmpbuf, count);
	pr_debug("%s(): written %d bytes of %d \n", __func__, retval, count);
	kfree(tmpbuf);

	mutex_unlock(&mydev->lock);
	
	/* awake any reader, there is now data in the buffer */
	wake_up_interruptible(&mydev->in_queue);
	/* and signal asynchronous readers */
	if (mydev->async_queue)
		kill_fasync(&mydev->async_queue, SIGIO, POLL_IN);
	
	return retval;

err3:
	kfree(tmpbuf);
err2:
	mutex_unlock(&mydev->lock);
err1:
	return retval;
}

/**
 * mydev_poll - Poll the MYDEV device.
 * @file: file pointer
 * @wait: poll table pointer
 * 
 * Returns a bit mask describing which operations could be completed immediately.
 */
static unsigned int mydev_poll(struct file *file, poll_table *wait)
{
	struct mydev_info *mydev = file->private_data;
	unsigned int retval = 0;

	pr_debug("%s(file %p, polltable %p)\n", __func__, file, wait);

	if (mutex_lock_interruptible(&mydev->lock)) {
		retval = POLLERR;
		goto err1;
	}

	poll_wait(file, &mydev->in_queue, wait);
	poll_wait(file, &mydev->out_queue, wait);

	if (mybuff_ready(mydev->mybuff) != 0) {
		retval |= POLLIN | POLLRDNORM; /* readable */
	}
	if (mybuff_free(mydev->mybuff) != 0) {
		retval |= POLLOUT | POLLWRNORM; /* writable */
	}

	mutex_unlock(&mydev->lock);
	return retval;

err1:
	return retval;
}

/**
 * mydev_ioctl - Controls and queries the MYDEV device.
 * @inode: inode pointer
 * @file: file pointer
 * @cmd: ioctl command code
 * @arg: ioctl command argument
 * 
 * Returns 0 if no error, standard error number otherwise.
 */
static int mydev_ioctl(struct inode *inode, struct file *file,
	unsigned int cmd, unsigned long arg)
{
	struct mydev_info *mydev = file->private_data;
	int retval = 0;

	pr_debug("%s(inode %p, file %p, cmd %d, arg %lx)\n",
		__func__, inode, file, cmd, arg);

	if (mutex_lock_interruptible(&mydev->lock)) {
		retval = -ERESTARTSYS;
		goto err1;
	}

	switch (cmd) {
	case MYDEV_IOCTL_SIZE_GET:
		if (put_user(mybuff_size(mydev->mybuff), (int __user *)arg)) {
			retval = -EFAULT;
			goto err2;
		}
		break;
	case MYDEV_IOCTL_FREE_GET:
		if (put_user(mybuff_free(mydev->mybuff), (int __user *)arg)) {
			retval = -EFAULT;
			goto err2;
		}
		break;
	case MYDEV_IOCTL_READY_GET:
		if (put_user(mybuff_ready(mydev->mybuff), (int __user *)arg)) {
			retval = -EFAULT;
			goto err2;
		}
		break;
	default:
		retval = -ENOTTY; /* invalid ioctl command (hystoric) */
		goto err2;
	}

	mutex_unlock(&mydev->lock);
	return retval;

err2:
	mutex_unlock(&mydev->lock);
err1:
	return retval;
}

/**
 * mydev_fasync - Notify to the MYDEV device a change in its FASYNC flag.
 * @fd:
 * @file: file pointer
 * @mode:
 * 
 * Returns 0 if no error, standard error number otherwise.
 */
static int mydev_fasync(int fd, struct file *file, int mode)
{
	struct mydev_info *mydev = file->private_data;

	pr_debug("%s(fd %d, file %p, mode %x)\n",  __func__, fd, file, mode);

	return fasync_helper(fd, file, mode, &mydev->async_queue);
}

static const struct file_operations mydev_fops = {
	.owner = THIS_MODULE,
	.open = mydev_open,
	.release = mydev_close,
	.read = mydev_read,
	.write = mydev_write,
	.ioctl = mydev_ioctl,
	.poll = mydev_poll,
	.fasync = mydev_fasync
};

/**
 * mydev_init(): Initializes the MYDEV device.
 * 
 * Returns 0 if no error, standard error number otherwise.
 */
static int __init mydev_init(void)
{
	int i, err, retval = 0;

	pr_debug("%s()\n", __func__);

	/* check module parameters */
	if (mydev_no == 0 || mydev_no > MYDEV_MAX_NUM) {
		pr_err("%s(): invalid mydev_no=%d \n", __func__, mydev_no);
		retval = -EINVAL;
		goto err1;
	}
	if (mydev_size > MYDEV_MAX_SIZE) {
		pr_err("%s(): invalid mydev_size=%d \n", __func__, mydev_no);
		retval = -EINVAL;
		goto err1;
	}
	
	/* register chrdev region, get the major number */
	if (mydev_major) {
		mydev_id = MKDEV(mydev_major, mydev_minor);
		err = register_chrdev_region(mydev_id, mydev_no, MYDEV_NAME);
	} else {
		err = alloc_chrdev_region(&mydev_id, mydev_minor, mydev_no, MYDEV_NAME);
		mydev_major = MAJOR(mydev_id);
	}
	if (err < 0) {
		pr_err("%s(): can't get major number %d\n", __func__, mydev_major);
		retval = -ENODEV;
		goto err1;
        } 
	pr_debug("%s(): allocated major number %d\n", __func__, mydev_major);

	/* init mydev - to be done before calling cdev_add() */
	mydev = kzalloc((mydev_no * sizeof(struct mydev_info)), GFP_KERNEL);
	if (!mydev) {
		pr_err("%s(): can't create mydev\n", __func__);
		retval = -ENOMEM;
		goto err2;
	}
	for (i = 0; i < mydev_no; i++) {
		err =  mybuff_create(&mydev[i].mybuff, mydev_size);
		if (err < 0) {
			pr_err("%s(): can't create mybuff for device %d, %d\n",
			__func__, mydev_major, mydev_minor + i);
			retval = -ENOMEM;
			goto err3;
	        } 
		pr_debug("%s(): created mybuff for device %d, %d\n",
			__func__, mydev_major, mydev_minor + i);
		
		init_waitqueue_head(&mydev[i].in_queue);
		init_waitqueue_head(&mydev[i].out_queue);
		mutex_init(&mydev[i].lock);
	}

	/* init and add cdev */
	for (i = 0; i < mydev_no; i++) {
		cdev_init(&mydev[i].cdev, &mydev_fops);
		mydev[i].cdev.owner = THIS_MODULE;
		err = cdev_add(&mydev[i].cdev, MKDEV(mydev_major, mydev_minor + i), 1);
		if (err < 0) {
			pr_err("%s(): can't create cdev for device %d, %d\n",
			__func__, mydev_major, mydev_minor + i);
			retval = -ENODEV;
			goto err4;
	        } 
		pr_debug("%s(): created cdev for device %d, %d\n",
			__func__, mydev_major, mydev_minor + i);
	}

	/* register to sysfs and send uevents to create dev nodes */
	mydev_class = class_create(THIS_MODULE, MYDEV_NAME);
	for (i = 0; i < mydev_no; i++) {
		mydev_device = device_create(mydev_class, NULL, 
			MKDEV(mydev_major, mydev_minor + i), NULL, MYDEV_NAMES, i);
		pr_debug("%s(): created device node for device %d, %d\n",
			__func__, mydev_major, mydev_minor + i);
	}

        return retval;

err4:
err3:
	kfree(mydev);
err2:
	unregister_chrdev_region(mydev_id, mydev_no);
err1:
	return retval;
}

/**
 * mydev_exit(): Terminates the MYDEV device.
 */
static void __exit mydev_exit(void)
{
	int i;

	pr_debug("%s()\n", __func__);

	/* unregister from sysfs and send uevents to destroy dev nodes */
	for (i = 0; i < mydev_no; i++) {
		device_destroy(mydev_class, MKDEV(mydev_major, mydev_minor + i));
		pr_debug("%s(): deleted device node for device %d, %d\n",
			__func__, mydev_major, mydev_minor + i);
	}
	class_destroy(mydev_class);

	/* delete cdev */
	for (i = 0; i < mydev_no; i++) {
		cdev_del(&mydev[i].cdev);
		pr_debug("%s(): deleted cdev for device %d, %d\n",
			__func__, mydev_major, mydev_minor + i);
	}

	/* delete mydev */
	for (i = 0; i < mydev_no; i++) {
		mybuff_delete(mydev[i].mybuff);
		pr_debug("%s(): deleted mybuff for device %d, %d\n",
			__func__, mydev_major, mydev_minor + i);
	}
	kfree(mydev);
	
	/* unregister chrdev region, release the major number */
	unregister_chrdev_region(mydev_id, mydev_no);
	pr_debug("%s(): released major number %d\n", __func__, mydev_major);

	return;
}

module_init(mydev_init);
module_exit(mydev_exit);

MODULE_DESCRIPTION("Ericsson AB - MYDEV device driver");
MODULE_AUTHOR("Paolo Rovelli <paolo.rovelli@ericsson.com>");
MODULE_LICENSE("GPL");

