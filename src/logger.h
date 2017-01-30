#ifndef LOGGER_H_
#define LOGGER_H_

#include <unistd.h>
#include <stdarg.h>

#include <ctime>
#include <climits>
#include <sstream>

namespace {

class Logger {




 public:

  const static int DATE_MAX = 30;

  void Start() {

    do_logging_ = true;

    char date[DATE_MAX];
    GetDate(date, DATE_MAX);

    char hostname[HOST_NAME_MAX];
    gethostname(hostname, HOST_NAME_MAX);

    start_time_ = current_time();
    fprintf(stdout, "[{\"time\": %.3lf, \"message\": \"Start at %s on %s.\"},\n", elapsed_time(), date, hostname);
  }

  void Stop() {

    char date[DATE_MAX];
    GetDate(date, DATE_MAX);

    do_logging_ = false;
    Message("Stop at %s.", date);
    fprintf(stdout, "]\n");
  }

  void Print(FILE *stream,
             const char *obsolute,
             const char *prefix,
             const char *suffix,
             const char *format,
             va_list argp) {
    fprintf(stream, "{");
    fprintf(stream, "\"time\": %.3lf", elapsed_time());
    fprintf(stream, ", ");
    fprintf(stream, "%s", prefix);
    vfprintf(stream, format, argp);
    fprintf(stream, "%s", suffix);
    fprintf(stream, "}");
    if (do_logging_)
      fprintf(stdout, ",\n");

  }

  void Message(const char* message, ...) {
    va_list argp;
    va_start(argp, message);
    Print(stdout, "INFO", "\"message\": \"", "\"", message, argp);
    va_end(argp);
  }

  void Status(const char* status, ...) {

    va_list argp;
    va_start(argp, status);
    Print(stdout, "INFO",  "\"status\": \{", "}", status, argp);
    va_end(argp);
  }

#if 0
  void Log(const char *severity, const char *label, const char* object, ...) {

    std::ostringstream prefix;
    prefix << "\"" << label << "\": ";

    va_list argp;
    va_start(argp, object);
    Print(stdout, severity, prefix.str().c_str(), "", object, argp);
    va_end(argp);
  }
#endif

 private:

  void GetDate(char *buffer, int buffer_size) {

    time_t rawtime;
    struct tm *timeinfo;
    time(&rawtime);
    timeinfo = localtime(&rawtime);

    strftime(buffer, buffer_size, "%c", timeinfo);

  }

  inline double current_time() { //sec
    struct timespec tp;
    clock_gettime(CLOCK_REALTIME, &tp);
    return (double)tp.tv_sec + (double)tp.tv_nsec * 1e-9;
  }

  inline double elapsed_time() {
    return current_time() - start_time_;
  }

  bool do_logging_;
  double start_time_, stop_time_;

};

} // namespace ::

#endif // LOGGER_H_
