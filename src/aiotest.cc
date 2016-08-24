#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <libaio.h>
#include <unistd.h>
#include <omp.h>

#include <cstdio>
#include <cstdlib>
#include <cassert>
#include <clocale>
#include <ctime>
#include <cctype>

#include "timer.h"
#include "logger.h"

const int MAX_FILENAME_LEN = 255;

void PrintUsage() {
  fprintf(stderr, "Options:\n"
	  "\t-f [value]: filename prefix\n"
	  "\t-s [value + {k,m,g}]: (data size in KB(k), MB(m), or GB(g) per event)\n"
	  "\t-e [value]: number of event per file)\n"
	  "\t-n [value]: number of files\n"	 
  	  "\t-d: use direct io\n"
	  "\t-a: use Linux's kernel asynchronous I/O\n");
}

int main(int argc, char **argv) {

  setlocale(LC_NUMERIC, "");
  setvbuf(stdout, (char *)NULL, _IONBF, 0);

  Timer timer;
  char *filename_prefix = NULL;
  int num_files = 0;
  int num_events_per_file = 0;
  int64_t event_size = 0;
  bool use_libaio = false;
  bool use_directio = false;

  int opt = 0;
  while ((opt = getopt(argc, argv, "f:s:n:e:da")) != -1) {
    switch (opt) {
    case 'f':
      filename_prefix = optarg;
      break;
    case 's':
      event_size = atoll(optarg);
      switch (tolower(optarg[strlen(optarg) - 1])) {
      case 'g':
	event_size *= 1024;
      case 'm':
	event_size *= 1024;
      case 'k':
	event_size *= 1024;
      }
      break;
    case 'e':
      num_events_per_file =  atoi(optarg);
      break;
    case 'n':
      num_files =  atoi(optarg);
      break;
    case 'd':
      use_directio = true;
      break;
    case 'a':
      use_libaio = true;
      break;
    case 'h':
      PrintUsage();
      return EXIT_FAILURE;
    }
  }

  if (num_files == 0 || event_size == 0 || filename_prefix == NULL || num_events_per_file == 0) {
    PrintUsage();
    return EXIT_FAILURE;
  }
  const int block_size = 512;

  const int64_t write_size = event_size * num_events_per_file * num_files;
  char *buffer = NULL;

  Logger logger;

  logger.Start();
  logger.Status("\"use_libaio\": %d, \"use_directoio\": %d", use_libaio, use_directio);
  logger.Status("\"write_size\": \"%'zd\", \"event_size\": \"%'zd\", \"unit\": \"byte\"", write_size, event_size);

  timer.Start();
  assert(posix_memalign((void **)&buffer, block_size, write_size) == 0);
  logger.Status("\"posix_memalign_time\": \"%'g\", \"unit\": \"ms\"", timer.Stop());

  timer.Start();
#pragma omp parallel
  {
    const int thread_id = omp_get_thread_num();
    const int num_threads = omp_get_num_threads();
    memset(buffer + thread_id * write_size / num_threads,
           time(NULL) % 256,
           (thread_id + 1) * write_size / num_threads - thread_id * write_size / num_threads);
  }
  logger.Status("\"memset_time\": \"%'g\", \"unit\": \"ms\"", timer.Stop());

  int flag_w;
  if (use_directio)
    flag_w = O_CREAT | O_TRUNC | O_WRONLY | O_DIRECT;
  else
    flag_w = O_CREAT | O_TRUNC | O_WRONLY;
  const mode_t mode = S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH;

  int fds[num_files];
  char filenames[num_files][MAX_FILENAME_LEN];

  for (int i = 0; i < num_files; ++i) {
    snprintf(filenames[i], MAX_FILENAME_LEN, "%sdata%d", filename_prefix, i);
    fds[i]= open(filenames[i], flag_w, mode);
    assert(fds[i] != -1);
  }

#if 0
  printf("fallocate...");
  timer.Start();
  for (int i = 0; i < num_files; ++i) {
    assert(fallocate(fds[i],
                     0, //mode
                     0, //offset
                     event_size * num_events_per_file //size
                     )
           == 0);
  }
  printf("takes %'g ms\n", timer.Stop());
#endif

  timer.Start();
  for (int i = 0; i < num_files; ++i)
    assert(fsync(fds[i]) != -1);
  system("sudo sync > /dev/null");
  system("sudo sysctl -w vm.drop_caches=3 > /dev/null");
  logger.Status("\"fsync_time\": \"%'g\", \"unit\": \"ms\"", timer.Stop());

  io_context_t ctx;
  const int num_events = num_events_per_file * num_files;
  io_event events[num_events];
  iocb iocbs[num_events];
  iocb *iocbps[num_events];

  /*
   * aio: Call "io_queue_init" to initialize asynchornous io state machine
   */
  if (use_libaio) {


    for (int i = 0; i < num_events; ++i)
      iocbps[i] = &iocbs[i];
  }

  /*
   * Write Kernel
   */
  if (use_libaio) {

    /*
     * aio [step0]: Call "io_queue_init" to initialize asynchornous io state machine
     */
    assert(io_queue_init(num_events, &ctx) == 0);
    for (int i = 0; i < num_events; ++i)
      iocbps[i] = &iocbs[i];

    /*
     * aio [step1]: Call "io_prep_pwrite" to set up iocb for asynchronous writes
     */ 
    timer.Start();
    for (int i = 0; i < num_files; ++i) {
      for (int j = 0; j < num_events_per_file; ++j) {
	const int idx = num_events_per_file * i + j;
	const int64_t offset = event_size * idx;
	io_prep_pwrite(&iocbs[idx], fds[i], buffer + offset, event_size, offset);
      }
    }

    /*
     * aio [step2]: Call "io_submit" to submit asynchronous I/O blocks for processing
     */ 
    assert(io_submit(ctx, num_events,(iocb **)&iocbps) == num_events);
    logger.Status("\"io_submit_time\": \"%'g\", \"unit\": \"ms\"", timer.Stop());
    logger.Status("\"io_submit_throughput\": \"%'g\", \"unit\": \"GiB/s\"",
		  write_size / timer.elapsed_time() / (1 << 30) * 1000);
    
    /*
     * aio [step3]: Call "io_getevents" to read asynchronous I/O events from the completion queue
     */
    timer.Start();
    assert(io_getevents(ctx, num_events, num_events, events, NULL) == num_events);
    logger.Status("\"io_getevents_time\": \"%'g\", \"unit\": \"ms\"", timer.Stop());
    logger.Status("\"io_getevents_throughput\": \"%'g\", \"unit\": \"GiB/s\"",
		  write_size / timer.elapsed_time() / (1 << 30) * 1000);

  } else {
    /*
     * psync: Call "pwrite" to write to a file descriptor at a given offset
     */
    timer.Start();
    for (int i = 0; i < num_files; ++i) {
      for (int j = 0; j < num_events_per_file; ++j) {
	const int idx = num_events_per_file * i + j;
	const int64_t offset = event_size * idx;
        int64_t written = pwrite(fds[i], buffer + offset, event_size, offset);
        assert(written == event_size);
      }
    }
    logger.Status("\"psync_time\": \"%'g\", \"unit\": \"ms\"", timer.Stop());
    logger.Status("\"psync_throughput\": \"%'g\", \"unit\": \"GiB/s\"",
		  write_size / timer.elapsed_time() / (1 << 30) * 1000);
    
  }

  timer.Start();
  for (int i = 0; i < num_files; ++i)
    assert(fsync(fds[i]) != -1);

  system("sudo sync > /dev/null");
  system("sudo sysctl -w vm.drop_caches=3 > /dev/null");
  logger.Status("\"fsync_time\": \"%'g\", \"unit\": \"ms\"", timer.Stop());

  /*
   * aio [step4]: Call "io_destroy" to destroy an asynchrnous I/O context.
   */
  if (use_libaio) {
    for (int i = 0; i < num_events; ++i)
      assert(events[i].res == events[i].obj->u.c.nbytes);

    io_destroy(ctx);
  }

  for (int i = 0; i < num_files; ++i)
    close(fds[i]);

  free(buffer);

  logger.Stop();

  return EXIT_SUCCESS;
}

