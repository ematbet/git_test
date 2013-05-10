/* 
 * my_mydev.h - MYDEV device driver interface
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

#ifndef __MY_MYDEV_H__
#define __MY_MYDEV_H__

#define MYDEV_IOCTL_MAGIC 'm'
#define MYDEV_IOCTL_SIZE_GET _IOR(MYDEV_IOCTL_MAGIC, 0, int)
#define MYDEV_IOCTL_FREE_GET _IOR(MYDEV_IOCTL_MAGIC, 1, int)
#define MYDEV_IOCTL_READY_GET _IOR(MYDEV_IOCTL_MAGIC, 2, int)

#endif /* __MY_MYDEV_H__ */
