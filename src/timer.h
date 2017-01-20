#ifndef TIMER_H_
#define TIMER_H_

#include <sys/time.h>

namespace {

class Timer {

 public:
  Timer() {
    start_ = 0.0;
    stop_ = 0.0;
  }

  inline void Start() {
    start_ = GetMilliSecond();
  }

  inline double Stop() {
    stop_ = GetMilliSecond();
    return elapsed_time();
  }

  inline double elapsed_time() {
    return stop_ - start_;
  }

 private:

  inline double GetMilliSecond() {
    struct timespec tp;
    clock_gettime(CLOCK_REALTIME, &tp);
    return (double)tp.tv_sec * 1e+3 + (double)tp.tv_nsec * 1e-6;
  }

  double start_, stop_;
};

} // namespace ::

#endif // TIMER_H_
