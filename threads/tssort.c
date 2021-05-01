#include <fcntl.h>
#include <float.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "barrier.h"
#include "float_vec.h"
#include "utils.h"

// struct to pass args into thread
struct args_struct {
  int threadnum;
  floats* fvec;
  long size;
  int T;
  floats* samps;
  long* sizes;
  barrier* bb;
  const char* output_fname;

} args_struct;

static int cmp_floats(const void* p1, const void* p2) {
  // comparing floats, accounting for precision and truncation errors
  return (*(float*)p1 - *(float*)p2) * 100000;
}

void qsort_floats(floats* xs) {
  qsort(xs->data, xs->size, sizeof(float), cmp_floats);
}

floats* sample(float* data, long size, int P) {
  long nn = 3 * (P - 1);
  long indices[nn];

  // getting random indices, ensuring no repeats
  long ii = 0;
  while (ii < nn) {
    long num = (rand() % size);

    // checking for repeats
    for (long jj = 0; jj < ii; jj++) {
      if (indices[jj] == num) {
        continue;
      }
    }

    indices[ii] = num;
    ii++;
  }

  // getting the elements at the random indices
  float values[nn];
  for (long jj = 0; jj < nn; jj++) {
    values[jj] = data[indices[jj]];
  }

  qsort(values, nn, sizeof(float), cmp_floats);

  // getting the medians of each group of three in the
  // sorted array of random values
  // appending 0 to beggining and FLT_MAX to end
  floats* samples = make_floats(0);
  floats_push(samples, 0);

  for (long kk = 1; kk < nn; kk += 3) {
    floats_push(samples, values[kk]);
  }

  floats_push(samples, FLT_MAX);

  return samples;
}

void* sort_worker(void* arguments) {
  struct args_struct* args = (struct args_struct*)arguments;

  floats* xs = make_floats(0);

  float low = args->samps->data[args->threadnum];
  float high = args->samps->data[args->threadnum + 1];

  for (long ii = 0; ii < args->size; ii++) {
    if (args->fvec->data[ii] < high && args->fvec->data[ii] >= low) {
      // getting data between low and high values
      floats_push(xs, args->fvec->data[ii]);
    }
  }
  printf("%d: start %0.4f, count %ld\n", args->threadnum, low, xs->size);
  // storing the amount of elements in this interval
  int nn = xs->size;
  args->sizes[args->threadnum] = nn;

  // sorting this interval
  qsort_floats(xs);

  // barrier wait so that all the threads get here before
  // writing to shared data
  barrier_wait(args->bb);

  // getting the start index to insert this interval
  // into the oriignal data list
  long start = 0;
  for (int jj = 0; jj < args->threadnum; jj++) {
    start += args->sizes[jj];
  }

  int fd_out = open(args->output_fname, O_WRONLY);
  check_rv(fd_out);
  int rv_lseek = lseek(fd_out, start * sizeof(float) + sizeof(long), SEEK_SET);
  check_rv(rv_lseek);

  int rv_write;

  rv_write = write(fd_out, xs->data, nn * sizeof(float));
  check_rv(rv_write);

  close(fd_out);
  free_floats(xs);
  free(args);
  return 0;
}

void run_sort_workers(floats* fvec, long size, int T, floats* samps,
                      long* sizes, barrier* bb, const char* output_fname) {
  pthread_t kids[T];
  (void)kids;

  for (int ii = 0; ii < T; ii++) {
    struct args_struct* args = malloc(sizeof(args_struct));
    args->threadnum = ii;
    args->fvec = fvec;
    args->size = size;
    args->T = T;
    args->samps = samps;
    args->sizes = sizes;
    args->bb = bb;
    args->output_fname = output_fname;

    int rv = pthread_create(&kids[ii], NULL, sort_worker, (void*)args);

    check_rv(rv);
  }

  for (int ii = 0; ii < T; ii++) {
    int rv = pthread_join(kids[ii], NULL);
    check_rv(rv);
  }
}

void sample_sort(floats* fvec, long size, int T, long* sizes, barrier* bb,
                 const char* output_fname) {
  floats* samps = sample(fvec->data, size, T);
  run_sort_workers(fvec, size, T, samps, sizes, bb, output_fname);
  free_floats(samps);
}

int main(int argc, char* argv[]) {
  alarm(120);

  if (argc != 4) {
    printf("Usage:\n");
    printf("T inputfile outputfile\n");
    return 1;
  }

  const int T = atoi(argv[1]);
  const char* input_fname = argv[2];
  const char* output_fname = argv[3];

  seed_rng();

  int rv;
  struct stat st;
  rv = stat(input_fname, &st);
  check_rv(rv);

  const int fsize = st.st_size;
  if (fsize < 8) {
    printf("File too small.\n");
    return 1;
  }

  int fd_in = open(input_fname, O_RDONLY);
  check_rv(fd_in);

  long count;
  rv = read(fd_in, &count, sizeof(long));
  check_rv(rv);

  floats* fvec = make_floats(count);
  rv = read(fd_in, fvec->data, sizeof(float) * count);
  check_rv(rv);

  long sizes_bytes = T * sizeof(long);
  long* sizes = malloc(sizes_bytes);

  int fd_out = open(output_fname, O_CREAT | O_RDWR, 0664);
  check_rv(fd_out);

  long trunc_rv = ftruncate(fd_out, sizeof(long) + count * sizeof(float));
  check_rv(trunc_rv);

  long write_rv = write(fd_out, &count, sizeof(long));
  check_rv(write_rv);
  close(fd_out);

  barrier* bb = make_barrier(T);
  sample_sort(fvec, count, T, sizes, bb, output_fname);

  free_barrier(bb);
  free_floats(fvec);
  free(sizes);
  close(fd_in);

  return 0;
}
