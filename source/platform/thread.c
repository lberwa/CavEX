/*
	Copyright (c) 2023 ByteBit/xtreme8000

	This file is part of CavEX.

	CavEX is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.

	CavEX is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with CavEX.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <assert.h>

#include "thread.h"

#ifdef USLEEP
#ifdef PLATFORM_WII 
#define __XSI_VISIBLE 600 // usleep
#define __POSIX_VISIBLE 200112
#include <unistd.h>
#include <time.h>
#endif /*PLATFORM_WII*/
#endif /*USLEEP*/

#ifdef PLATFORM_PC

#include <limits.h>
#ifndef _WIN32
#include <sched.h>
#endif
#include <stdlib.h>
#include <string.h>
#ifndef _WIN32
#include <sys/resource.h>
#endif
#ifdef __linux__
#include <sys/syscall.h>
#endif
#ifndef _WIN32
#include <unistd.h>
#else
#include <windows.h>
#endif

struct thread_start_args {
	void* (*entry)(void* arg);
	void* arg;
	uint8_t priority;
};

static void thread_apply_current_priority(uint8_t priority) {
	#ifdef _WIN32
	(void)priority;
	return;
	#else
	/* Wii/libogc uses the priority value directly. On PC this is best-effort:
	 * realtime pthread priority usually needs privileges, and Linux nice only
	 * allows raising priority when CAP_SYS_NICE is available. Failing either is
	 * harmless; the thread still runs with default priority. */
	if(priority == 0)
		return;

	int min_prio = sched_get_priority_min(SCHED_RR);
	int max_prio = sched_get_priority_max(SCHED_RR);
	if(min_prio >= 0 && max_prio >= min_prio) {
		struct sched_param param;
		memset(&param, 0, sizeof(param));
		param.sched_priority = min_prio
			+ (int)((unsigned int)(max_prio - min_prio) * priority / UINT8_MAX);
		if(param.sched_priority < min_prio)
			param.sched_priority = min_prio;
		if(param.sched_priority > max_prio)
			param.sched_priority = max_prio;
		if(pthread_setschedparam(pthread_self(), SCHED_RR, &param) == 0)
			return;
	}

#ifdef __linux__
	/* Per-thread nice fallback. Without CAP_SYS_NICE Linux will not let us
	 * boost the mesher above normal, so make lower-priority worker threads much
	 * nicer instead. That gives the normal-priority mesher CPU first. */
	int nice_value = (int)priority >= 16 ? 0 : (16 - (int)priority);
	if(nice_value < -15)
		nice_value = -15;
	if(nice_value > 15)
		nice_value = 15;
	pid_t tid = (pid_t)syscall(SYS_gettid);
	if(tid > 0)
		(void)setpriority(PRIO_PROCESS, tid, nice_value);
#else
	(void)priority;
#endif
	#endif
}

static void* thread_entry_wrapper(void* user) {
	struct thread_start_args start = *(struct thread_start_args*)user;
	free(user);
	thread_apply_current_priority(start.priority);
	return start.entry(start.arg);
}

void thread_create(struct thread* t, void* (*entry)(void* arg), void* arg,
				   uint8_t priority) {
	assert(t && entry);
	struct thread_start_args* start = malloc(sizeof(*start));
	if(start) {
		*start = (struct thread_start_args) {
			.entry = entry,
			.arg = arg,
			.priority = priority,
		};
		pthread_create(&t->native, NULL, thread_entry_wrapper, start);
	} else {
		pthread_create(&t->native, NULL, entry, arg);
	}
}

void thread_create_stack(struct thread* t, void* (*entry)(void* arg), void* arg,
						 uint8_t priority, size_t stack_size) {
	assert(t && entry);
	/* PC hat reichlich Stack; Default genügt. stack_size ignoriert. */
	(void)stack_size;
	struct thread_start_args* start = malloc(sizeof(*start));
	if(start) {
		*start = (struct thread_start_args) {
			.entry = entry,
			.arg = arg,
			.priority = priority,
		};
		pthread_create(&t->native, NULL, thread_entry_wrapper, start);
	} else {
		pthread_create(&t->native, NULL, entry, arg);
	}
}

void thread_join(struct thread* t) {
	assert(t);
	pthread_join(t->native, NULL);
}

void thread_msleep(size_t ms) {
	#ifdef _WIN32
	Sleep((DWORD)ms);
	#else
	usleep(ms * 1000);
	#endif
}

void tchannel_init(struct thread_channel* c, size_t count) {
	assert(c && count > 0);

	c->count = 0;
	c->length = count;
	c->data = malloc(c->length * sizeof(void*));

	pthread_mutex_init(&c->lock, NULL);
	pthread_cond_init(&c->signal, NULL);
}

void tchannel_close(struct thread_channel* c) {
	assert(c);

	free(c->data);
	pthread_cond_destroy(&c->signal);
	pthread_mutex_destroy(&c->lock);
}

bool tchannel_receive(struct thread_channel* c, void** msg, bool block) {
	assert(c && msg);

	pthread_mutex_lock(&c->lock);

	if(block) {
		while(!c->count)
			pthread_cond_wait(&c->signal, &c->lock);
	} else {
		if(!c->count) {
			pthread_mutex_unlock(&c->lock);
			return false;
		}
	}

	*msg = c->data[0];
	c->count--;

	memmove(c->data, c->data + 1, c->count * sizeof(void*));

	pthread_cond_signal(&c->signal);
	pthread_mutex_unlock(&c->lock);
	return true;
}

bool tchannel_send(struct thread_channel* c, void* msg, bool block) {
	assert(c && msg);

	pthread_mutex_lock(&c->lock);

	if(block) {
		while(c->count >= c->length)
			pthread_cond_wait(&c->signal, &c->lock);
	} else {
		if(c->count >= c->length) {
			pthread_mutex_unlock(&c->lock);
			return false;
		}
	}

	c->data[c->count++] = msg;

	pthread_cond_signal(&c->signal);
	pthread_mutex_unlock(&c->lock);
	return true;
}

#endif

#ifdef PLATFORM_WII
#include <ogc/video.h>
#include <stddef.h>

void thread_create(struct thread* t, void* (*entry)(void* arg), void* arg,
				   uint8_t priority) {
	assert(t && entry);
	LWP_CreateThread(&t->native, entry, arg, NULL, 0, priority);
}

void thread_create_stack(struct thread* t, void* (*entry)(void* arg), void* arg,
						 uint8_t priority, size_t stack_size) {
	assert(t && entry);
	/* stackbase NULL -> libogc alloziert einen Stack der gegebenen Größe */
	LWP_CreateThread(&t->native, entry, arg, NULL, stack_size, priority);
}

void thread_join(struct thread* t) {
	assert(t);

	void* unused;
	LWP_JoinThread(t->native, &unused);
}

void thread_msleep(size_t ms) {
    #ifdef USLEEP
	usleep(ms * 1000);
	#else
	// Rough estimate: 1 frame = ~16ms (assuming 60Hz)
	size_t frames = ms / 16;
    for (size_t i = 0; i < frames; ++i) {
        VIDEO_WaitVSync();
    }
	#endif
}

void tchannel_init(struct thread_channel* c, size_t count) {
	assert(c && count > 0);
	MQ_Init(&c->native, count);
}

void tchannel_close(struct thread_channel* c) {
	assert(c);
	MQ_Close(c->native);
}

bool tchannel_receive(struct thread_channel* c, void** msg, bool block) {
	return MQ_Receive(c->native, msg, block ? MQ_MSG_BLOCK : MQ_MSG_NOBLOCK);
}

bool tchannel_send(struct thread_channel* c, void* msg, bool block) {
	return MQ_Send(c->native, msg, block ? MQ_MSG_BLOCK : MQ_MSG_NOBLOCK);
}

#endif
