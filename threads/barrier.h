// Author: Nat Tuck
// CS3650 starter code

#ifndef BARRIER_H
#define BARRIER_H

#include <pthread.h>

typedef struct barrier {
    // TODO: Need some synchronization stuff.

	//threads_required
    //	int   count;
	//threas_left = count - seen threads_left 
    //	int   seen;
    	int threads_required;
	int threads_left;
	pthread_mutex_t mutex;
	pthread_cond_t cond_var;
	unsigned int cycle;

} barrier;

barrier* make_barrier(int nn);
void barrier_wait(barrier* bb);
void free_barrier(barrier* bb);


#endif

