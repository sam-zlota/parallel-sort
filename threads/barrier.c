// Author: Nat Tuck
// CS3650 starter code

#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <assert.h>
#include <unistd.h>

#include "barrier.h"

//inspired by byronlai.com implementation of POSIX Thread Barrier

barrier*
make_barrier(int nn)
{
    barrier* bb = malloc(sizeof(barrier));
    assert(bb != 0);

    bb->threads_required = nn; 
    bb->threads_left  = nn;
    bb->cycle = 0;


    pthread_mutex_init(&bb->mutex, NULL);
    pthread_cond_init(&bb->cond_var, NULL);
    return bb;
}

void
barrier_wait(barrier* bb)
{
    pthread_mutex_lock(&bb->mutex);

    if(--bb->threads_left == 0) {
	bb->threads_left = bb->threads_required;
	bb->cycle++;
	pthread_cond_broadcast(&bb->cond_var);
	pthread_mutex_unlock(&bb->mutex);
    }
    else {
	unsigned int c = bb->cycle;

	while(c == bb->cycle) {
		pthread_cond_wait(&bb->cond_var, &bb->mutex);
	}

	pthread_mutex_unlock(&bb->mutex);
    }
}

void
free_barrier(barrier* bb)
{
    free(bb);
}

