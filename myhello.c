/*
 * myhello.c - MYHELLO device driver
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

#include <linux/module.h>
#include <linux/kernel.h>

/* myhello module parameters */
static char *mystring = "pippo";
module_param(mystring, charp, 0644);
MODULE_PARM_DESC(mystring, "myhello messages (default pippo)");

static int hello_init(void)
{
    printk(KERN_ALERT "hello world: %s\n", mystring);
    return 0;
}

static void hello_exit(void)
{
    printk(KERN_ALERT "goodbye cruel world\n");
    return;
}

/*
 * The following instructions are needed to specify who is the "init" function 
 * and who is the "exit" function
 */

module_init(hello_init);
module_exit(hello_exit);

MODULE_DESCRIPTION("Ericsson AB - MYHELLO device driver");
MODULE_AUTHOR("Matteo Betti <matteo.betti@ericsson.com>");
MODULE_LICENSE("GPL");


