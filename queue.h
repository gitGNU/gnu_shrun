/*
  File: queue.h

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
  License along with this program. If not, see http://www.gnu.org/licenses/.
*/

#ifndef __QUEUE_H
#define __QUEUE_H

struct queue {
	char *buffer, *read, *write;
	size_t size;
};

extern void queue_init(struct queue *queue);
extern void queue_destroy(struct queue *queue);
extern char *queue_write_pos(struct queue *queue, size_t size, ssize_t *pavail);
extern void queue_advance_write(struct queue *queue, size_t size);
extern int queue_empty(struct queue *queue);
extern size_t queue_length(struct queue *queue);
extern char *queue_read_pos(struct queue *queue, ssize_t *pavail);
extern void queue_advance_read(struct queue *queue, size_t size);
extern void queue_reset(struct queue *queue);
extern void queue_erase_tail(struct queue *queue, size_t sz);
extern int queue_append(struct queue *queue, const char *str);

#endif  /* __QUEUE_H */
