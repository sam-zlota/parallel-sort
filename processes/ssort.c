#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <fcntl.h>
#include <math.h>
#include <float.h>


#include "float_vec.h"
#include "barrier.h"
#include "utils.h"

static int 
cmp_floats(const void *p1, const void *p2) {
	//comparing floats, accounting for precision and truncation errors
	return (*(float*)p1 - *(float*)p2) * 1000000;
}

void
qsort_floats(floats* xs)
{
	qsort(xs->data, xs->size, sizeof(float), cmp_floats);
}

floats*
sample(float* data, long size, int P)
{
	long nn = 3 * (P - 1);
	long indices[nn];

	//getting random indices, ensuring no repeats
	long ii = 0;
	while (ii < nn) {
		long num = (rand() % size);

		//checking for repeats
		for(long jj = 0; jj < ii; jj++) {
			if(indices[jj] == num) {
				continue;
			}
		}

		indices[ii] = num;
		ii++;
	}

	//getting the elements at the random indices
	float values[nn];
	for (long jj = 0; jj < nn; jj++) {
		values[jj] = data[indices[jj]];
	}

	qsort(values, nn, sizeof(float), cmp_floats);

	//getting the medians of each group of three in the
	//sorted array of random values
	//appending 0 to beggining and FLT_MAX to end
	floats* samples = make_floats(0);
	floats_push(samples, 0);

	for(long kk = 1; kk < nn; kk+=3) {
		floats_push(samples, values[kk]);
	}
	
	floats_push(samples, FLT_MAX);

		
    return samples;
}

void
sort_worker(int pnum, float* data, long size, int P, floats* samps, long* sizes, barrier* bb)
{

	floats* xs = make_floats(0);
 
	
	float low = samps->data[pnum];
	float high = samps->data[pnum + 1];

	for(long ii = 0; ii < size; ii++) {
		if(data[ii] < high && data[ii] >= low) {
			//getting data between low and high values
			floats_push(xs, data[ii]);
		}	
	}

		printf("%d: start %.04f, count %ld\n", pnum, samps->data[pnum], xs->size);
	
	//storing the amount of elements in this interval
	int nn = xs->size;	
	sizes[pnum] = nn;
   
	//sorting this interval
   	qsort_floats(xs);

	//barrier wait so that all the procsesses get hear before 
	//writing to shared data
    	barrier_wait(bb);
    
   
	//getting the start index to insert this interval
	//into the oriignal data list
    	long start = 0;
    	for(int jj = 0; jj < pnum; jj++) {
		start += sizes[jj];
    	}

	//writing to shared data list
    	for(long kk = 0; kk < nn; kk++) {
		data[start + kk] = xs->data[kk];
    	}

    	free_floats(xs);
}

void
run_sort_workers(float* data, long size, int P, floats* samps, long* sizes, barrier* bb)
{

   	 pid_t kids[P];
    	(void) kids; // suppress unused warning


    	//inspired by Nat Tuck Lecture Notes
    	//scratch-2021-01/notes-3650/13-data-races/lock-sum101.c

    	for (int ii = 0; ii < P; ii++) {
		if((kids[ii] = fork())) {
		

		}
		else {
			sort_worker(ii,	data, size, P, samps, sizes, bb);
			exit(0);	
		}

    	}

   
    	for (int jj = 0; jj < P; ++jj) {	
        	int rv = waitpid(kids[jj], 0, 0);
        	check_rv(rv);
    	}

}

void
sample_sort(float* data, long size, int P, long* sizes, barrier* bb)
{
    floats* samps = sample(data, size, P);
    run_sort_workers(data, size, P, samps, sizes, bb);
    free_floats(samps);
}

int
main(int argc, char* argv[])
{
    	alarm(120);

    	if (argc != 3) {
        	printf("Usage:\n");
        	printf("\t%s P data.dat\n", argv[0]);
       		return 1;
    	}

    	const int P = atoi(argv[1]);
    	const char* fname = argv[2];

    	seed_rng();

    	int rv;
    	struct stat st;
    	rv = stat(fname, &st);
    	check_rv(rv);

   	const int fsize = st.st_size;
    	if (fsize < 8) {
        	printf("File too small.\n");
       		return 1;
    	}

    	int fd = open(fname, O_RDWR);
    	check_rv(fd);

    	void* file = mmap(0, fsize, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    
   	(void) file; 

    
   	long count = ((long*)file)[0];
   	float* data = (float*)(file + sizeof(long));

  	long sizes_bytes = P * sizeof(long);
	long* sizes = (long*) mmap(0, sizes_bytes, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);

    	barrier* bb = make_barrier(P);

    	sample_sort(data, count, P, sizes, bb);

    	free_barrier(bb);


    	munmap(file, fsize);
    	munmap(sizes, sizes_bytes);

    	return 0;
}

