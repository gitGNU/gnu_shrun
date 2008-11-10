/*
  File: queue.c

  Copyright (C) 2008 Andreas Gruenbacher <agruen@suse.de>, SUSE Labs

  This program is free software; you can redistribute it and/or
  modify it under the terms of the GNU Library General Public
  License as published by the Free Software Foundation; either
  version 2 of the License, or (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  Library General Public License for more details.

  You should have received a copy of the GNU Library General Public
  License along with this library; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
*/

#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "queue.h"

void queue_init(struct queue *queue)
{
	queue->buffer = queue->read = queue->write = NULL;
	queue->size = 0;
}

void queue_destroy(struct queue *queue)
{
	free(queue->buffer);
}

char *queue_write_pos(struct queue *queue, size_t size, ssize_t *pavail)
{

	for(;;) {
		char *buffer;
		size_t used = queue->write - queue->buffer;

		if (queue->size - used >= size)
			break;
		if (queue->read != queue->buffer) {
			memmove(queue->buffer, queue->read,
				queue->write - queue->read);
			queue->write -= queue->read - queue->buffer;
			queue->read = queue->buffer;
		} else {
			if (!queue->size)
				queue->size = 16384;
			while (queue->size - used < size)
				queue->size *= 2;

			buffer = realloc(queue->buffer, queue->size);
			if (!buffer)
				return NULL;
			queue->read += buffer - queue->buffer;
			queue->write += buffer - queue->buffer;
			queue->buffer = buffer;
			break;
		}
	}
	if (pavail)
		*pavail = queue->buffer + queue->size - queue->write;
	return queue->write;
}

void queue_advance_write(struct queue *queue, size_t size)
{
	queue->write += size;
}

int queue_empty(struct queue *queue)
{
	return queue->write == queue->read;
}

size_t queue_length(struct queue *queue)
{
	return queue->write - queue->read;
}

char *queue_read_pos(struct queue *queue, ssize_t *pavail)
{
	size_t avail = queue->write - queue->read;

	if (pavail)
		*pavail = avail;
	return avail ? queue->read : NULL;
}

void queue_advance_read(struct queue *queue, size_t size)
{
	queue->read += size;
}

void queue_reset(struct queue *queue)
{
	queue->write = queue->read = queue->buffer;
}

void queue_erase_tail(struct queue *queue, size_t sz)
{
	queue->write -= sz;
}

int queue_append(struct queue *queue, const char *str)
{
	ssize_t sz = strlen(str);
	char *buf;

	buf = queue_write_pos(queue, sz, NULL);
	if (!buf)
		return -1;
	memcpy(buf, str, sz);
	queue_advance_write(queue, sz);
	return 0;
}
