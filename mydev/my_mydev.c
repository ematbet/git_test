/* 
 * my_mydev.c - MYDEV device driver
 *
 * Copyright (C) 2013 Ericsson AB
 *
 * Author: Matteo Betti <matteo.betti@ericsson.com>
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


#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/cdev.h>
#include <linux/fs.h>
#include <linux/errno.h>
#include <linux/slab.h>  /* for malloc api */ 


#define MYDEV_MAX_NUM 10
#define MYDEV_MAX_SIZE (100*1024) /* max 100KB */
#define MYDEV_NAME "mydev"
#define MYDEV_NAMES "mydev%d"

/* mydev module parameters */

static unsigned int mydev_no = 1;
module_param(mydev_no, uint, 0644);
MODULE_PARAM_DESC(mydev_no, "mydev intances (default 1)");

static unsigned int mydev_size = 1024;
module_param(mydev_size, uint, 0644);
MODULE_PARM_DESC(mydev_size, "mydev buffer size (default 1 KByte)");

/* define major and minor number */
static dev_t mydev_id;
static unsigned int mydev_major = 0; /* automatically choosen by the system */
static unsigned int mydev_minor = 0; /* start from minor 0 */

/* embedding my cdev structure within a device specific structure */
static struct mydev_own {
    struct mybuff* mybuff;
    struct cdev cdev; /*char device structure */
    
    /* some others (mutex and queues) */

} *mydev;





/* file operation initialization */
static const struct file_operation mydev_fops = {
    .owner = THIS_MODULE,
	.open = mydev_open,
	.release = mydev_close,
	.read = mydev_read,
	.write = mydev_write
};

/**
 * mydev_init(): initialize MYDEV device.
 *
 * Returns 0 if no error, standard error number otherwise.
 */
static int __init mydev_init(void)
{
    int i, err, retval = 0;

    /* check module parameters */
    if (mydev_no == 0 || mydev_no > MYDEV_MAX_NUM){
        pr_err("%s(): invalid mydev_no=%d \n", __func__, mydev_no);
        retval = -EINVAL;
        goto err1;
    }
    if (mydev_size > MYDEV_MAX_SIZE){
        pr_err("%s(): invalid mydev_size=%d \n", __func__, mydev_no);
        retval = -EINVAL;
        goto err1;
    }

    /* register the char device region, get the major number */
    if (mydev_major){
        mydev_id = MKDEV(mydev_major, mydev_minor);
        err = register_chrdev_region(mydev_id, mydev_no, MYDEV_NAME ) 
            } else { /*if you haven't set the major number */
        err = alloc_chrdev_region(&mydev_id, mydev_minor, mydev_no, MYDEV_NAME);
        mydev_major = MAJOR(mydev_id);
    }
    if(err < 0) { /* error occurs in register */
        pr_err("%s(): can't get major number %d \n", __func__, mydev_major);
        retval = _ENODEV;
        goto err1;
    }
    pr_debug("%s(): allocated major number %d\n", __func__, mydev_major);

    /* memory alloc befor cdev_add() */
    mydev = kzalloc(mydev_no * sizeof(struct mydev_own), GFP_KERNEL);
    if(!mydev){
        pr_err("%s(): can't create mydev\n", __func__);
        retval = -ENOMEM;
        goto err2;
    }
    /* allocate the nuffer "my_buffer" to every char device */
    for (i = 0; i < mydev_no; i++) {
		err =  mybuff_create(&(mydev[i].mybuff), mydev_size);  /* array-like */
		if (err < 0) {
			pr_err("%s(): can't create mybuff for device %d, %d\n",
                   __func__, mydev_major, mydev_minor + i);
			retval = -ENOMEM;
			goto err3;
        } 
		pr_debug("%s(): created mybuff for device %d, %d\n",
                 __func__, mydev_major, mydev_minor + i);

 




