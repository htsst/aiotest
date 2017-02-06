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

#define MAX_FILENAME_LEN 255

void PrintUsage() {
  fprintf(stderr, "Options:\n"
          "\t-p [value]: filename prefix\n"
          "\t\tin case 'f = 1', filename_prefix is used as filename directly\n"
          "\t\te.g.) -f 1 -p /tmp/data (use /tmp/data)\n"
          "\t\te.g.) -f 2 -p /tmp/data (use /tmp/data1 and /tmp/data2)\n"
          "\t-s [value + {k,m,g}]: (data size in KB(k), MB(m), or GB(g) per event)\n"
          "\t-e [value]: number of events per file descpritor)\n"
          "\t-n [value]: number of file descpritors per file\n"
          "\t-f [value]: number of files\n"
          "\t-d: use direct IO(open with O_DIRECT)\n"
          "\t-a: use Linux's kernel asynchronous I/O\n"
          "\t-w: only write test(skip read test)\n");
}

int main(int argc, char **argv) {

  setlocale(LC_NUMERIC, "");
  setvbuf(stdout, (char *)NULL, _IONBF, 0);

  Timer timer;
  char *filename_prefix = NULL;
  int num_files = 0;
  int num_fds_per_file = 0;
  int num_events_per_fd = 0;
  int64_t event_size = 0;
  bool use_libaio = false;
  bool use_directio = false;
  bool do_read_test = true;

  int opt = 0;
  while ((opt = getopt(argc, argv, "p:s:n:e:f:dawh")) != -1) {
    switch (opt) {
    case 'p':
      filename_prefix = optarg;
      break;
    case 's':
      event_size = atoll(optarg);
      if (size_t arglen = strlen(optarg)) {
        switch (tolower(optarg[arglen - 1])) {
        case 'g':
          event_size *= 1024;
        case 'm':
          event_size *= 1024;
        case 'k':
          event_size *= 1024;
        }
      }
      break;
    case 'e':
      num_events_per_fd =  atoi(optarg);
      break;
    case 'n':
      num_fds_per_file =  atoi(optarg);
      break;
    case 'f':
      num_files =  atoi(optarg);
      break;
    case 'd':
      use_directio = true;
      break;
    case 'a':
      use_libaio = true;
      break;
    case 'w':
      do_read_test = false;
      break;
    case 'h':
      PrintUsage();
      return EXIT_FAILURE;
    }
  }

  if (num_files == 0 || num_fds_per_file == 0 || num_events_per_fd == 0 || event_size == 0 || filename_prefix == NULL) {
    PrintUsage();
    return EXIT_FAILURE;
  }
  const int block_size = 512;

  const int64_t io_size = event_size * num_events_per_fd * num_fds_per_file * num_files;
  char *buffer = NULL;

  Logger logger;

  logger.Start();
  logger.Status("\"use_libaio\": %d, \"use_directoio\": %d", use_libaio, use_directio);
  logger.Status("\"io_size\": \"%'zd\", \"event_size\": \"%'zd\", \"unit\": \"byte\"", io_size, event_size);
  logger.Status("\"is_aligned_event_buffer\": %d", event_size % block_size == 0);
  logger.Status("\"num_events_per_fd\": %d, \"num_fds_per_file\": %d, \"num_files\": %d", num_events_per_fd, num_fds_per_file, num_files);

  timer.Start();
  assert(posix_memalign((void **)&buffer, block_size, io_size) == 0);
  logger.Status("\"posix_memalign_time\": \"%'g\", \"unit\": \"ms\"", timer.Stop());

  timer.Start();
#pragma omp parallel
  {
    const int thread_id = omp_get_thread_num();
    const int num_threads = omp_get_num_threads();
    memset(buffer + thread_id * io_size / num_threads,
           time(NULL) % 256,
           (thread_id + 1) * io_size / num_threads - thread_id * io_size / num_threads);
  }
  logger.Status("\"memset_time\": \"%'g\", \"unit\": \"ms\"", timer.Stop());

  int flag_w = O_CREAT | O_TRUNC | O_WRONLY;
  if (use_directio)
    flag_w |=  O_DIRECT;
  const mode_t mode = S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH;

  int fds[num_files][num_fds_per_file];
  size_t fd_offsets[num_files][num_fds_per_file];
  char filenames[num_files][MAX_FILENAME_LEN];

  for (int i = 0; i < num_files; ++i) {
    if (num_files > 1) {
      snprintf(filenames[i], MAX_FILENAME_LEN, "%s%d", filename_prefix, i);
    } else {
      snprintf(filenames[i], MAX_FILENAME_LEN, "%s", filename_prefix);
    }
    for (int j = 0; j < num_fds_per_file; ++j) {
      fd_offsets[i][j] = j * event_size * num_events_per_fd;
      fds[i][j] = open(filename_prefix, flag_w, mode);
      assert(fds[i][j] != -1);
    }
  }

#if 0
  printf("fallocate...");
  timer.Start();
  for (int i = 0; i < num_fds_per_file; ++i) {
    assert(fallocate(fds[i],
                     0, //mode
                     0, //offset
                     event_size * num_events_per_fd //size
                     )
           == 0);
  }
  printf("takes %'g ms\n", timer.Stop());
#endif

  timer.Start();
  for (int i = 0; i < num_files; ++i)
    for (int j = 0; j < num_fds_per_file; ++j)
      assert(fsync(fds[i][j]) != -1);

  system("sudo sync > /dev/null");
  system("sudo sysctl -w vm.drop_caches=3 > /dev/null");
  logger.Status("\"pre_fsync_time\": \"%'g\", \"unit\": \"ms\"", timer.Stop());

  io_context_t ctx;
  const int num_events = num_events_per_fd * num_fds_per_file * num_files;
  io_event events[num_events];
  iocb iocbs[num_events];
  iocb *iocbps[num_events];

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
      for (int j = 0; j < num_fds_per_file; ++j) {
        for (int k = 0; k < num_events_per_fd; ++k) {
          const int idx = (i * num_fds_per_file + j) * num_events_per_fd + k;
          const int64_t buffer_offset = event_size * idx;
          const int64_t file_offset = fd_offsets[i][j] + k * event_size;
          io_prep_pwrite(&iocbs[idx],
                         fds[i][j],
                         buffer + buffer_offset,
                         event_size,
                         file_offset);
        }
      }
    }

    /*
     * aio [step2]: Call "io_submit" to submit asynchronous I/O blocks for processing
     */ 
    assert(io_submit(ctx, num_events,(iocb **)&iocbps) == num_events);
    logger.Status("\"write_io_submit_time\": \"%'g\", \"unit\": \"ms\"", timer.Stop());
    logger.Status("\"write_io_submit_throughput\": \"%'g\", \"unit\": \"GiB/s\"",
                  io_size / timer.elapsed_time() / (1 << 30) * 1000);

  /*
     * aio [step3]: Call "io_getevents" to read asynchronous I/O events from the completion queue
     */
    timer.Start();
    assert(io_getevents(ctx, num_events, num_events, events, NULL) == num_events);
    logger.Status("\"write_io_getevents_time\": \"%'g\", \"unit\": \"ms\"", timer.Stop());
    logger.Status("\"write_io_getevents_throughput\": \"%'g\", \"unit\": \"GiB/s\"",
                  io_size / timer.elapsed_time() / (1 << 30) * 1000);

  } else {
    /*
     * psync: Call "pwrite" to write to a file descriptor at a given offset
     */
    timer.Start();
    for (int i = 0; i < num_files; ++i) {
      for (int j = 0; j < num_fds_per_file; ++j) {
        for (int k = 0; k < num_events_per_fd; ++k) {
          const int idx = (i * num_fds_per_file + j) * num_events_per_fd + k;
          const int64_t buffer_offset = event_size * idx;
          const int64_t file_offset = fd_offsets[i][j] + k * event_size;
          int64_t written = pwrite(fds[i][j],
                                   buffer + buffer_offset,
                                   event_size,
                                   file_offset);
          assert(written == event_size);
        }
      }
    }
    logger.Status("\"pwrite_time\": \"%'g\", \"unit\": \"ms\"", timer.Stop());
    logger.Status("\"pwrite_throughput\": \"%'g\", \"unit\": \"GiB/s\"",
                  io_size / timer.elapsed_time() / (1 << 30) * 1000);

  }

  timer.Start();
  for (int i = 0; i < num_files; ++i)
    for (int j = 0; j < num_fds_per_file; ++j)
      assert(fsync(fds[i][j]) != -1);

  system("sudo sync > /dev/null");
  system("sudo sysctl -w vm.drop_caches=3 > /dev/null");
  logger.Status("\"write_fsync_time\": \"%'g\", \"unit\": \"ms\"", timer.Stop());

  /*
   * aio [step4]: Call "io_destroy" to destroy an asynchrnous I/O context.
   */
  if (use_libaio) {
    for (int i = 0; i < num_events; ++i)
      assert(events[i].res == events[i].obj->u.c.nbytes);

    io_destroy(ctx);
  }

  for (int i = 0; i < num_files; ++i)
    for (int j = 0; j < num_fds_per_file; ++j)
      close(fds[i][j]);

  if (do_read_test) {

    int flag_r = O_RDONLY;
    if (use_directio)
      flag_r |=  O_DIRECT;

    for (int i = 0; i < num_files; ++i) {
      for (int j = 0; j < num_fds_per_file; ++j) {
        fds[i][j] = open(filename_prefix, flag_r);
        assert(fds[i][j] != -1);
      }
    }

    /*
     * Read Kernel
     */
    if (use_libaio) {

      /*
       * aio [step0]: Call "io_queue_init" to initialize asynchornous io state machine
       */
      assert(io_queue_init(num_events, &ctx) == 0);
      for (int i = 0; i < num_events; ++i)
        iocbps[i] = &iocbs[i];

      /*
       * aio [step1]: Call "io_prep_pread" to set up iocb for asynchronous reads
       */ 
      timer.Start();
      for (int i = 0; i < num_files; ++i) {
        for (int j = 0; j < num_fds_per_file; ++j) {
          for (int k = 0; k < num_events_per_fd; ++k) {
            const int idx = (i * num_fds_per_file + j) * num_events_per_fd + k;
            const int64_t buffer_offset = event_size * idx;
            const int64_t file_offset = fd_offsets[i][j] + k * event_size;
            io_prep_pread(&iocbs[idx],
                          fds[i][j],
                          buffer + buffer_offset,
                          event_size,
                          file_offset);
          }
        }
      }

      /*
       * aio [step2]: Call "io_submit" to submit asynchronous I/O blocks for processing
       */ 
      assert(io_submit(ctx, num_events,(iocb **)&iocbps) == num_events);
      logger.Status("\"read_io_submit_time\": \"%'g\", \"unit\": \"ms\"", timer.Stop());
      logger.Status("\"read_io_submit_throughput\": \"%'g\", \"unit\": \"GiB/s\"",
                    io_size / timer.elapsed_time() / (1 << 30) * 1000);

    /*
       * aio [step3]: Call "io_getevents" to read asynchronous I/O events from the completion queue
       */
      timer.Start();
      assert(io_getevents(ctx, num_events, num_events, events, NULL) == num_events);
      logger.Status("\"read_io_getevents_time\": \"%'g\", \"unit\": \"ms\"", timer.Stop());
      logger.Status("\"read_io_getevents_throughput\": \"%'g\", \"unit\": \"GiB/s\"",
                    io_size / timer.elapsed_time() / (1 << 30) * 1000);

    } else {
      /*
       * psync: Call "pread" to wread to a file descriptor at a given offset
       */
      timer.Start();
      for (int i = 0; i < num_files; ++i) {
        for (int j = 0; j < num_fds_per_file; ++j) {
          for (int k = 0; k < num_events_per_fd; ++k) {
            const int idx = (i * num_fds_per_file + j) * num_events_per_fd + k;
            const int64_t buffer_offset = event_size * idx;
            const int64_t file_offset = fd_offsets[i][j] + k * event_size;
            int64_t read = pread(fds[i][j],
                                 buffer + buffer_offset,
                                 event_size,
                                 file_offset);
            assert(read == event_size);
          }
        }
      }
      logger.Status("\"pread_time\": \"%'g\", \"unit\": \"ms\"", timer.Stop());
      logger.Status("\"pread_throughput\": \"%'g\", \"unit\": \"GiB/s\"",
                    io_size / timer.elapsed_time() / (1 << 30) * 1000);

    }

    timer.Start();
    for (int i = 0; i < num_files; ++i)
      for (int j = 0; j < num_fds_per_file; ++j)
        assert(fsync(fds[i][j]) != -1);

    system("sudo sync > /dev/null");
    system("sudo sysctl -w vm.drop_caches=3 > /dev/null");
    logger.Status("\"read_fsync_time\": \"%'g\", \"unit\": \"ms\"", timer.Stop());

    /*
     * aio [step4]: Call "io_destroy" to destroy an asynchrnous I/O context.
     */
    if (use_libaio) {
      for (int i = 0; i < num_events; ++i)
        assert(events[i].res == events[i].obj->u.c.nbytes);

      io_destroy(ctx);
    }

    for (int i = 0; i < num_files; ++i)
      for (int j = 0; j < num_fds_per_file; ++j)
        close(fds[i][j]);
  }

  free(buffer);

  logger.Stop();

  return EXIT_SUCCESS;
}

