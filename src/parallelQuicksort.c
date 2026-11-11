/*
  Original Author: Joshua Stough, Washington and Lee University This
  code was initially obtained from
  http://sc12.supercomputing.org/hpceducator/PythonForParallelism/codes/parallelQuicksort.c.
  Later, Arnaud Legrand mainly made a few cosmetic changes so that it
  is easier to use for performance evaluation purposes.

  This code quicksorts a random list of size given by the argument
  (default 1M) and times both sequential quicksort and parallel (using
  Pthreads).

  Usage: parallelQuicksort [size [thread_level [seed [order]]]]
  The seed fixes both the input array and the pivot choices; when
  omitted it is drawn from the clock. It is printed for reproducibility.
  order is a permutation of "spl" (default): sequential, parallel, libc.
*/

#define _POSIX_C_SOURCE 200809L /* rand_r, clock_gettime */
#define _DARWIN_C_SOURCE        /* CLOCK_MONOTONIC_RAW on macOS */

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>

#define DNUM 1000000
#define THREAD_LEVEL 10

//for sequential and parallel implementation
//Pivots are drawn with rand_r on a caller-owned state, so that threads
//never share the hidden (non thread-safe) state of rand().
void swap(double lyst[], int i, int j);
int partition(double lyst[], int lo, int hi, unsigned *rng);
void quicksortHelper(double lyst[], int lo, int hi, unsigned *rng);
void quicksort(double lyst[], int size, unsigned seed);
int isSorted(double lyst[], int size);
double now(void);

//for parallel implementation
void parallelQuicksort(double lyst[], int size, int tlevel, unsigned seed);
void *parallelQuicksortHelper(void *threadarg) __attribute__ ((noreturn));
struct thread_data {
  double *lyst;
  int low;
  int high;
  int level;
  unsigned rng;                 //private RNG state of this thread
};
//thread_data should be thread-safe, since while lyst is 
//shared, [low, high] will not overlap among threads.

//for the builtin qsort, for fun:
int compare_doubles(const void *a, const void *b);

/*
Main method:
-generate random list
-time sequential quicksort, parallel quicksort and standard qsort
*/
int main(int argc, char *argv[])
{
  double start, diff;

  int NUM = DNUM;
  int level = THREAD_LEVEL;
  unsigned seed = (unsigned) (unsigned long long) (now() * 1e9); //clock ns, differs between runs
  if (argc >= 2)                //user specified list size.
    NUM = atoi(argv[1]);
  if (argc >= 3)                //depth of the thread spawning tree
    level = atoi(argv[2]);
  if (argc >= 4)                //fixed seed, for reproducibility
    seed = (unsigned) strtoul(argv[3], NULL, 10);
  const char *order = argc >= 5 ? argv[4] : "spl"; //algorithms to run, in order
  printf("Size: %d, Level: %d, Seed: %u, Order: %s\n", NUM, level, seed, order);

  //Want to compare sorting on the same list,
  //so backup.
  double *lystbck = (double *) malloc(NUM * sizeof(double));
  double *lyst = (double *) malloc(NUM * sizeof(double));

  //Populate random original/backup list.
  unsigned rng = seed;
  for (int i = 0; i < NUM; i++) {
    lystbck[i] = 1.0 * rand_r(&rng) / RAND_MAX;
  }

  //Time each algorithm on a fresh copy of the same list, in the given
  //order (s: sequential, p: parallel, l: libc qsort).
  for (const char *a = order; *a; a++) {
    memcpy(lyst, lystbck, NUM * sizeof(double));
    start = now();
    switch (*a) {
    case 's': quicksort(lyst, NUM, seed); break;
    case 'p': parallelQuicksort(lyst, NUM, level, seed); break;
    case 'l': qsort(lyst, NUM, sizeof(double), compare_doubles); break;
    default: fprintf(stderr, "Unknown algorithm '%c'\n", *a); return 1;
    }
    diff = now() - start;

    const char *name = *a == 's' ? "Sequential" : *a == 'p' ? "Parallel" : "Built-in";
    if (!isSorted(lyst, NUM)) {
      printf("Oops, lyst did not get sorted by %s quicksort.\n", name);
    }
    printf("%s quicksort took: %.9f sec.\n", name, diff);
  }

  free(lyst);
  free(lystbck);
  return 0;
}

//Monotonic clock with ns resolution (gettimeofday is in us and may jump).
double now(void)
{
  struct timespec ts;
#ifdef CLOCK_MONOTONIC_RAW
  clock_gettime(CLOCK_MONOTONIC_RAW, &ts); //ns on macOS, not NTP-slewed on Linux
#else
  clock_gettime(CLOCK_MONOTONIC, &ts);
#endif
  return ts.tv_sec + ts.tv_nsec * 1e-9;
}

void quicksort(double lyst[], int size, unsigned seed)
{
  quicksortHelper(lyst, 0, size - 1, &seed);
}

void quicksortHelper(double lyst[], int lo, int hi, unsigned *rng)
{
  if (lo >= hi)
    return;
  int b = partition(lyst, lo, hi, rng);
  quicksortHelper(lyst, lo, b - 1, rng);
  quicksortHelper(lyst, b + 1, hi, rng);
}

void swap(double lyst[], int i, int j)
{
  double temp = lyst[i];
  lyst[i] = lyst[j];
  lyst[j] = temp;
}

int partition(double lyst[], int lo, int hi, unsigned *rng)
{
  int b = lo;
  int r = (int) (lo + (hi - lo) * (1.0 * rand_r(rng) / RAND_MAX));
  double pivot = lyst[r];
  swap(lyst, r, hi);
  for (int i = lo; i < hi; i++) {
    if (lyst[i] < pivot) {
      swap(lyst, i, b);
      b++;
    }
  }
  swap(lyst, hi, b);
  return b;
}


/*
parallel quicksort top level:
instantiate parallelQuicksortHelper thread, and that's 
basically it.
*/
void parallelQuicksort(double lyst[], int size, int tlevel, unsigned seed)
{
  int rc;
  void *status;

  //Want joinable threads (usually default).
  pthread_attr_t attr;
  pthread_attr_init(&attr);
  pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);

  //pthread function can take only one argument, so struct.
  struct thread_data td;
  td.lyst = lyst;
  td.low = 0;
  td.high = size - 1;
  td.level = tlevel;
  td.rng = seed;

  //The top-level thread.
  pthread_t theThread;
  rc = pthread_create(&theThread, &attr, parallelQuicksortHelper,
                      (void *) &td);
  if (rc) {
    printf("ERROR; return code from pthread_create() is %d\n", rc);
    exit(-1);
  }
  //Now join the thread (wait, as joining blocks) and quit.
  pthread_attr_destroy(&attr);
  rc = pthread_join(theThread, &status);
  if (rc) {
    printf("ERROR; return code from pthread_join() is %d\n", rc);
    exit(-1);
  }
  //printf("Main: completed join with top thread having a status of %ld\n",
  //              (long)status);

}

/*
parallelQuicksortHelper
-if the level is still > 0, then partition and make 
parallelQuicksortHelper threads to solve the left and 
right-hand sides, then quit. Otherwise, call sequential.
*/
void *parallelQuicksortHelper(void *threadarg)
{
  int mid, t, rc;
  void *status;

  struct thread_data *my_data;
  my_data = (struct thread_data *) threadarg;

  //fyi:
  //printf("Thread responsible for [%d, %d], level %d.\n",
  //              my_data->low, my_data->high, my_data->level);

  if (my_data->level <= 0 || my_data->low == my_data->high+1) {
    //We have plenty of threads, finish with sequential.
    quicksortHelper(my_data->lyst, my_data->low, my_data->high,
                    &my_data->rng);
    pthread_exit(NULL);
  }
  //Want joinable threads (usually default).
  pthread_attr_t attr;
  pthread_attr_init(&attr);
  pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);

  //Now we partition our part of the lyst.
  mid = partition(my_data->lyst, my_data->low, my_data->high, &my_data->rng);

  //At this point, we will create threads for the 
  //left and right sides.  Must create their data args.
  struct thread_data thread_data_array[2];

  for (t = 0; t < 2; t++) {
    thread_data_array[t].lyst = my_data->lyst;
    thread_data_array[t].level = my_data->level - 1;
    thread_data_array[t].rng = (unsigned) rand_r(&my_data->rng); //child seed
  }
  thread_data_array[0].low = my_data->low;
  thread_data_array[0].high = mid - 1;
  thread_data_array[1].low = mid + 1;
  thread_data_array[1].high = my_data->high;

  //Now, instantiate the threads.
  //In quicksort of course, due to the transitive property,
  //no elements in the left and right sides of mid will have
  //to be compared again.
  pthread_t threads[2];
  for (t = 0; t < 2; t++) {
    rc = pthread_create(&threads[t], &attr, parallelQuicksortHelper,
                        (void *) &thread_data_array[t]);
    if (rc) {
      printf("ERROR; return code from pthread_create() is %d\n", rc);
      exit(-1);
    }
  }

  pthread_attr_destroy(&attr);
  //Now, join the left and right sides to finish.
  for (t = 0; t < 2; t++) {
    rc = pthread_join(threads[t], &status);
    if (rc) {
      printf("ERROR; return code from pthread_join() is %d\n", rc);
      exit(-1);
    }
  }

  pthread_exit(NULL);
}

//check if the elements of lyst are in non-decreasing order.
//one is success.
int isSorted(double lyst[], int size)
{
  for (int i = 1; i < size; i++) {
    if (lyst[i] < lyst[i - 1]) {
      printf("at loc %d, %e < %e \n", i, lyst[i], lyst[i - 1]);
      return 0;
    }
  }
  return 1;
}

//for the built-in qsort comparator
//from http://www.gnu.org/software/libc/manual/html_node/Comparison-Functions.html#Comparison-Functions
int compare_doubles(const void *a, const void *b)
{
  const double *da = (const double *) a;
  const double *db = (const double *) b;

  return (*da > *db) - (*da < *db);
}
