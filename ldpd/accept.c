// SPDX-License-Identifier: ISC
/*	$OpenBSD$ */

/*
 * Copyright (c) 2012 Claudio Jeker <claudio@openbsd.org>
 */

#include <zebra.h>

#include "ldpd.h"
#include "ldpe.h"
#include "log.h"

struct accept_ev {
	LIST_ENTRY(accept_ev)	 entry;
	struct thread		*ev;
	void (*accept_cb)(struct thread *);
	void			*arg;
	int			 fd;
};

struct {
	LIST_HEAD(, accept_ev)	 queue;
	struct thread		*evt;
} accept_queue;

static void	accept_arm(void);
static void	accept_unarm(void);
static void accept_cb(struct thread *);
static void accept_timeout(struct thread *);

void
accept_init(void)
{
	LIST_INIT(&accept_queue.queue);
}

int accept_add(int fd, void (*cb)(struct thread *), void *arg)
{
	struct accept_ev	*av;

	if ((av = calloc(1, sizeof(*av))) == NULL)
		return (-1);
	av->fd = fd;
	av->accept_cb = cb;
	av->arg = arg;
	LIST_INSERT_HEAD(&accept_queue.queue, av, entry);

	thread_add_read(master, accept_cb, av, av->fd, &av->ev);

	log_debug("%s: accepting on fd %d", __func__, fd);

	return (0);
}

void
accept_del(int fd)
{
	struct accept_ev	*av;

	LIST_FOREACH(av, &accept_queue.queue, entry)
		if (av->fd == fd) {
			log_debug("%s: %d removed from queue", __func__, fd);
			THREAD_OFF(av->ev);
			LIST_REMOVE(av, entry);
			free(av);
			return;
		}
}

void
accept_pause(void)
{
	log_debug(__func__);
	accept_unarm();
	thread_add_timer(master, accept_timeout, NULL, 1, &accept_queue.evt);
}

void
accept_unpause(void)
{
	if (accept_queue.evt != NULL) {
		log_debug(__func__);
		THREAD_OFF(accept_queue.evt);
		accept_arm();
	}
}

static void
accept_arm(void)
{
	struct accept_ev	*av;
	LIST_FOREACH(av, &accept_queue.queue, entry) {
		thread_add_read(master, accept_cb, av, av->fd, &av->ev);
	}
}

static void
accept_unarm(void)
{
	struct accept_ev	*av;
	LIST_FOREACH(av, &accept_queue.queue, entry)
		THREAD_OFF(av->ev);
}

static void accept_cb(struct thread *thread)
{
	struct accept_ev	*av = THREAD_ARG(thread);
	thread_add_read(master, accept_cb, av, av->fd, &av->ev);
	av->accept_cb(thread);
}

static void accept_timeout(struct thread *thread)
{
	accept_queue.evt = NULL;

	log_debug(__func__);
	accept_arm();
}
