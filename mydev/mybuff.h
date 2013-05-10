/*
 * mybuff.c - MYBUFF device
 *
 * Copyright (C) 2011 Ericsson AB
 *
 * Author: Paolo Rovelli <paolo.rovelli@ericsson.com>
 */

#ifndef __MYBUFF_H__
#define __MYBUFF_H__

#ifdef __KERNEL__
	#include <linux/slab.h>
	#define alloc(size) kmalloc(size, GFP_KERNEL)
	#define free(ptr) kfree(ptr)
#else
	#include <stdlib.h>
	#define alloc(size) malloc(size)
#endif

struct mybuff;

extern int mybuff_create(struct mybuff **mybuff, int size);
extern int mybuff_delete(struct mybuff *mybuff);
extern int mybuff_read(struct mybuff *mybuff, char *buff, int count);
extern int mybuff_write(struct mybuff *mybuff, char *buff, int count);
extern int mybuff_clear(struct mybuff *mybuff);
extern int mybuff_size(struct mybuff *mybuff);
extern int mybuff_free(struct mybuff *mybuff);
extern int mybuff_ready(struct mybuff *mybuff);

#endif /* __MYBUFF_H__ */

