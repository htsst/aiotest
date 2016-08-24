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
    struct timeval t;
    gettimeofday(&t, NULL);
    return static_cast<double>(t.tv_sec) * 1e+3 + static_cast<double>(t.tv_usec) * 1e-3;
  }

  double start_, stop_;
};

} // namespace ::

#endif // TIMER_H_
