/*
  File: queue.h

  Copyright (C) 2008 Andreas Gruenbacher <agruen@suse.de>, SUSE Labs

  License: GPLv2 or later
*/

#ifndef __FIFO_H
#define __FIFO_H

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

#endif  /* __FIFO_H */
