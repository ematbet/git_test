/* 
 * mybuff.c - MYBUFF device
 *
 * Copyright (C) 2011 Ericsson AB
 *
 * Author: Paolo Rovelli <paolo.rovelli@ericsson.com>
 */

#include "mybuff.h"

struct mybuff {
        char *buff;
        int write_idx;
        int read_idx;
        int size;
        int status;
};

enum {
        MYBUFF_EMPTY,
        MYBUFF_DATA,
        MYBUFF_FULL
};

int mybuff_create(struct mybuff **mybuff, int size)
{
	if (size <= 0)
		return -1; /* failure */

	*mybuff = alloc(sizeof(struct mybuff));
	if (*mybuff) {
		(*mybuff)->buff = alloc(size);
		if ((*mybuff)->buff) {
			(*mybuff)->size = size;
			(*mybuff)->read_idx = 0;
			(*mybuff)->write_idx = 0;
			(*mybuff)->status = MYBUFF_EMPTY;
			return 0; /* success */
		} else {
			free(*mybuff);
			*mybuff = NULL;
		}
	}
	return -1; /* failure */
}

int mybuff_delete(struct mybuff *mybuff)
{
	if (mybuff) {
		if (mybuff->buff) {
			free(mybuff->buff);
			mybuff->buff = NULL;
		}
		free(mybuff);
		mybuff = NULL;
	}
	return 0;
}

int mybuff_read(struct mybuff *mybuff, char *buff, int count)
{
	int read = 0;

	if (mybuff) {
		while ((mybuff->status != MYBUFF_EMPTY) && count > 0) {
			buff[read] = mybuff->buff[mybuff->read_idx];
			read++;
			count--;

			mybuff->read_idx = (mybuff->read_idx + 1)
				% mybuff->size;
			if (mybuff->read_idx == mybuff->write_idx)
				mybuff->status = MYBUFF_EMPTY;
			else
				mybuff->status = MYBUFF_DATA;
		}
	}
	return read;
}

int mybuff_write(struct mybuff *mybuff, char *buff, int count)
{
	int written = 0;

	if (mybuff) {
		while ((mybuff->status != MYBUFF_FULL) && count > 0) {
			mybuff->buff[mybuff->write_idx] = buff[written];
			written++;
			count--;

			mybuff->write_idx = (mybuff->write_idx + 1)
				% mybuff->size;
			if (mybuff->write_idx == mybuff->read_idx)
				mybuff->status = MYBUFF_FULL;
			else
				mybuff->status = MYBUFF_DATA;
		}
	}
	return written;
}

int mybuff_clear(struct mybuff *mybuff)
{
	if (mybuff) {
		mybuff->read_idx = 0;
		mybuff->write_idx = 0;
		mybuff->status = MYBUFF_EMPTY;
	}
	return 0;
}

int mybuff_size(struct mybuff *mybuff)
{
	if (mybuff) {
		return mybuff->size;
	}
	return 0;
}

int mybuff_free(struct mybuff *mybuff)
{
	if (mybuff) {
		switch(mybuff->status) {
		case MYBUFF_EMPTY:
			return mybuff->size;
		case MYBUFF_FULL:
			return 0;
		case MYBUFF_DATA:
		default:
			return mybuff->read_idx >= mybuff->write_idx ?
				mybuff->read_idx - mybuff->write_idx :
				mybuff->size - mybuff->write_idx
					+ mybuff->read_idx;
		}
	}
	return 0;
}

int mybuff_ready(struct mybuff *mybuff)
{
	if (mybuff) {
		return mybuff_size(mybuff) - mybuff_free(mybuff);
	}
	return 0;
}

