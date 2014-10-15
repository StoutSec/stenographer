// Copyright 2014 Google Inc. All rights reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef STENOGRAPHER_UTIL_H_
#define STENOGRAPHER_UTIL_H_

// Contains small helper functions and types used throughout stenographer code.

#include <stdint.h>
#include <stdio.h>
#include <time.h>  // strftime(), gmtime(), time(),
#include <sys/time.h>  // gettimeofday()
#include <pthread.h>  // pthread_self()
#include <stdlib.h>
#include <string.h>
#include <execinfo.h>  // backtrace(), backtrace_symbols()

#include <iostream>  // cerr
#include <string>  // string
#include <sstream>  // stringstream
#include <iomanip>  // setw, setfill
#include <deque>
#include <memory>

using namespace std;

namespace {

struct timespec clock_realtime, clock_monotonic;
clockid_t clock_mono_id = CLOCK_MONOTONIC;
bool InitTime() {
  clock_gettime(CLOCK_REALTIME, &clock_realtime);
#ifdef CLOCK_MONOTONIC_RAW
  // If monotinic raw clock is supported and available, let's use that.
  if (!clock_gettime(CLOCK_MONOTONIC_RAW, &clock_monotonic)) {
    clock_mono_id = CLOCK_MONOTONIC_RAW;
    return true;
  }
#endif
  clock_gettime(CLOCK_MONOTONIC, &clock_monotonic);
  return true;
}
bool run_init_time = InitTime();

}  // namespace

namespace st {

#define DISALLOW_COPY_AND_ASSIGN(name) \
  name(const name&); \
  name(const name&&); \
  void operator=(const name&)

const int64_t kNumNanosPerMicro = 1000;
const int64_t kNumMicrosPerMilli = 1000;
const int64_t kNumMillisPerSecond = 1000;
const int64_t kNumNanosPerMilli = kNumNanosPerMicro * kNumMicrosPerMilli;
const int64_t kNumMicrosPerSecond = kNumMicrosPerMilli * kNumMillisPerSecond;
const int64_t kNumNanosPerSecond =
    kNumMillisPerSecond * kNumMicrosPerMilli* kNumNanosPerMicro;

inline int64_t GetCurrentTimeNanos() {
  struct timespec tv;
  clock_gettime(clock_mono_id, &tv);
  int64_t secs = clock_realtime.tv_sec - clock_monotonic.tv_sec + tv.tv_sec;
  int64_t nsecs = clock_realtime.tv_nsec - clock_monotonic.tv_nsec + tv.tv_nsec;
  return secs * 1000000000 + nsecs;
}
inline int64_t GetCurrentTimeMicros() {
  return GetCurrentTimeNanos() / 1000;
}
inline void SleepForNanoseconds(int64_t nanos) {
  if (nanos <= 0) { return; }
  struct timespec tv;
  tv.tv_sec = nanos / kNumNanosPerSecond;
  tv.tv_nsec = nanos % kNumNanosPerSecond;
  while (EINTR == clock_nanosleep(CLOCK_MONOTONIC, 0, &tv, &tv)) {}
}
inline void SleepForMicroseconds(int64_t micros) {
  SleepForNanoseconds(micros * kNumNanosPerMicro);
}
inline void SleepForSeconds(double seconds) {
  SleepForNanoseconds(seconds * kNumNanosPerSecond);
}

////////////////////////////////////////////////////////////////////////////////
//// Logging helpers
//
// Simple methods for logging intersting events.
// LOG(INFO) << "blah";  // normal logging message
// LOG(FATAL) << "blah";  // log, then crash the program.  something is wrong.
// CHECK(b) << "blah";  // if 'b' is false, crash with given message.

namespace {

const size_t kTimeBufferSize = 20;
const char* kTimeFormat = "%Y-%m-%dT%H:%M:%S";

class LogLine {
 public:
  LogLine(bool crash, const char* file, int line) : crash_(crash) {
    FillTimeBuffer();
    ss_ << setfill('0')
      << time_buffer_ << "." << setw(6) << tv_.tv_usec << setw(0) << "Z T:"
      << setw(5) << uint16_t(pthread_self()) << setw(0) << " ["
      << file << ":" << line << "] ";
  }
  ~LogLine() {
    ss_ << "\n";
    cerr << ss_.str() << flush;
    if (crash_) {
      cerr << "ABORTABORTABORT" << endl;
      void* backtraces[32];
      int size = backtrace(backtraces, 32);
      char** symbols = backtrace_symbols(backtraces, size);
      for (int i = 0; i < size; i++) {
        cerr << symbols[i] << endl;
      }
      free(symbols);
      abort();
    }
  }

  template <class T>
  LogLine& operator<<(const T& data) {
    ss_ << data;
    return *this;
  }

 private:
  void FillTimeBuffer() {
    gettimeofday(&tv_, NULL);
    struct tm* timeinfo = gmtime(&tv_.tv_sec);
    size_t len = strftime(time_buffer_, kTimeBufferSize, kTimeFormat, timeinfo);
    if (len + 1 != kTimeBufferSize) {  // returned num bytes doesn't include \0
      strcpy(time_buffer_, "STRFTIME_ERROR");
    }
  }

  stringstream ss_;
  struct timeval tv_;
  char time_buffer_[kTimeBufferSize];
  bool crash_;

  DISALLOW_COPY_AND_ASSIGN(LogLine);
};

}  // namespace

extern int logging_verbose_level;
#define LOGGING_FATAL_CRASH true
#define LOGGING_ERROR_CRASH false
#define LOGGING_INFO_CRASH false
#define LOGGING_V1_CRASH false
#define LOGGING_V2_CRASH false
#define LOGGING_V3_CRASH false

#define LOGGING_FATAL_LOG true
#define LOGGING_ERROR_LOG true
#define LOGGING_INFO_LOG (logging_verbose_level >= 0)
#define LOGGING_V1_LOG (logging_verbose_level > 0)
#define LOGGING_V2_LOG (logging_verbose_level > 1)
#define LOGGING_V3_LOG (logging_verbose_level > 2)

#ifndef LOG
#define LOG(level) if (LOGGING_ ## level ## _LOG) \
    LogLine(LOGGING_ ## level ## _CRASH, __FILE__, __LINE__)
#endif
#ifndef CHECK
#define CHECK(expr) if (!(expr)) LOG(FATAL) << "CHECK(" #expr ") "
#endif

typedef unique_ptr<string> Error;

#define SUCCEEDED(x) ((x).get() == NULL)
#define SUCCESS NULL

#define ERROR(x) Error(new string(x))

#define RETURN_IF_ERROR(status, msg) do { \
  Error __return_if_error_status__ = (status); \
  if (!SUCCEEDED(__return_if_error_status__)) { \
    __return_if_error_status__->append(" <- "); \
    __return_if_error_status__->append(msg); \
    return move(__return_if_error_status__); \
  } \
} while (false)

#define LOG_IF_ERROR(status, msg) do { \
  Error __log_if_error_status__ = (status); \
  if (!SUCCEEDED(__log_if_error_status__)) { \
    LOG(ERROR) << msg << ": " << *__log_if_error_status__; \
  } \
} while (false)

#define CHECK_SUCCESS(x) do { \
  Error __check_success_error__ = (x); \
  CHECK(SUCCEEDED(__check_success_error__)) \
      << #x << ": " << *__check_success_error__; \
} while (false)

#define REPLACE_IF_ERROR(initial, replacement) do { \
  Error __replacement_error__ = (replacement); \
  if (!SUCCEEDED(__replacement_error__)) { \
    if (!SUCCEEDED(initial)) { \
      LOG(ERROR) << "replacing error: " << *initial; \
    } \
    initial = move(__replacement_error__); \
  } \
} while (false)

////////////////////////////////////////////////////////////////////////////////
//// Synchronization primitives
//
// The very simple Mutex class, and its RAII locker Mutex::Locker provide very
// simple blocking locks/unlocks wrapping a pthreads mutex.

#define CHECK_PTHREAD(expr, mu) do { \
    LOG(V3) << "PTHREAD: " << #expr << " " << intptr_t(mu); \
    int ret = (expr); \
    CHECK(ret == 0) << #expr << ": " << strerror(ret); \
  } while (false)

// Mutex provides a simple, usable wrapper around a mutex.
class Mutex {
 public:
  Mutex() {
    pthread_mutexattr_t attr;
    CHECK_PTHREAD(pthread_mutexattr_init(&attr), &attr);
    CHECK_PTHREAD(pthread_mutexattr_settype(
          &attr, PTHREAD_MUTEX_ERRORCHECK), &attr);
    CHECK_PTHREAD(pthread_mutex_init(&mu_, &attr), &mu_);
  }
  ~Mutex() {
    CHECK_PTHREAD(pthread_mutex_destroy(&mu_), &mu_);
  }
  inline void Lock() {
    CHECK_PTHREAD(pthread_mutex_lock(&mu_), &mu_);
  }
  inline void Unlock() {
    CHECK_PTHREAD(pthread_mutex_unlock(&mu_), &mu_);
  }

  class Locker {
   public:
    Locker(Mutex* mu) : mu_(mu) {
      mu_->Lock();
    }
    ~Locker() { mu_->Unlock(); }
   private:
    Mutex* mu_;
    DISALLOW_COPY_AND_ASSIGN(Locker);
  };
 private:
  pthread_mutex_t mu_;
  DISALLOW_COPY_AND_ASSIGN(Mutex);
};

// Barrier provides a barrier for multiple threads.
class Barrier {
 public:
  explicit Barrier(int threads) {
    CHECK_PTHREAD(pthread_barrier_init(&barrier_, NULL, threads), &barrier_);
  }
  ~Barrier() {
    CHECK_PTHREAD(pthread_barrier_destroy(&barrier_), &barrier_);
  }
  void Block() {
    int ret = pthread_barrier_wait(&barrier_);
    CHECK(ret == PTHREAD_BARRIER_SERIAL_THREAD || ret == 0)
        << "pthread_barrier_wait failed: " << strerror(ret);
  }
 private:
  pthread_barrier_t barrier_;

  DISALLOW_COPY_AND_ASSIGN(Barrier);
};

// Notification allows multiple threads to wait for one thread to do something.
class Notification {
 public:
  Notification() : waiting_(true) {
    CHECK_PTHREAD(pthread_mutex_init(&mu_, NULL), &mu_);
    CHECK_PTHREAD(pthread_cond_init(&cond_, NULL), &cond_);
  }
  ~Notification() {
    CHECK_PTHREAD(pthread_cond_destroy(&cond_), &cond_);
    CHECK_PTHREAD(pthread_mutex_destroy(&mu_), &mu_);
  }
  void WaitForNotification() {
    CHECK_PTHREAD(pthread_mutex_lock(&mu_), &mu_);
    while (waiting_) {
      CHECK_PTHREAD(pthread_cond_wait(&cond_, &mu_), &cond_);
    }
    CHECK_PTHREAD(pthread_mutex_unlock(&mu_), &mu_);
  }
  void Notify() {
    CHECK_PTHREAD(pthread_mutex_lock(&mu_), &mu_);
    waiting_ = false;
    CHECK_PTHREAD(pthread_cond_broadcast(&cond_), &cond_);
    CHECK_PTHREAD(pthread_mutex_unlock(&mu_), &mu_);
  }
 private:
  bool waiting_;
  pthread_mutex_t mu_;
  pthread_cond_t cond_;

  DISALLOW_COPY_AND_ASSIGN(Notification);
};

// ProducerConsumerQueue is a very simple thread-safe FIFO queue.
class ProducerConsumerQueue {
 public:
  ProducerConsumerQueue() {
    pthread_mutexattr_t attr;
    // For some very strange reason, if we use "NORMAL" mutexes here, the
    // pthread_mutex_destroy call in the destructor CHECK-fails.  The second
    // I switched to using ERRORCHECK, no problems.  So, I'm sticking with
    // ERRORCHECK ;)
    CHECK_PTHREAD(pthread_mutexattr_init(&attr), &attr);
    CHECK_PTHREAD(pthread_mutexattr_settype(
          &attr, PTHREAD_MUTEX_ERRORCHECK), &attr);
    CHECK_PTHREAD(pthread_mutex_init(&mu_, &attr), &mu_);
    CHECK_PTHREAD(pthread_cond_init(&cond_, NULL), &cond_);
  }
  ~ProducerConsumerQueue() {
    pthread_mutex_unlock(&mu_);
    CHECK_PTHREAD(pthread_cond_destroy(&cond_), &cond_);
    CHECK_PTHREAD(pthread_mutex_destroy(&mu_), &mu_);
  }

  void Put(void* val) {
    CHECK_PTHREAD(pthread_mutex_lock(&mu_), &mu_);
    d_.push_back(val);
    CHECK_PTHREAD(pthread_cond_signal(&cond_), &cond_);
    CHECK_PTHREAD(pthread_mutex_unlock(&mu_), &mu_);
  }
  void* Get() {
    CHECK_PTHREAD(pthread_mutex_lock(&mu_), &mu_);
    while (d_.empty()) {
      CHECK_PTHREAD(pthread_cond_wait(&cond_, &mu_), &cond_);
    }
    void* ret = d_.front();
    d_.pop_front();
    CHECK_PTHREAD(pthread_mutex_unlock(&mu_), &mu_);
    return ret;
  }
  bool TryGet(void** val) {
    CHECK_PTHREAD(pthread_mutex_lock(&mu_), &mu_);
    if (d_.empty()) { return false; }
    *val = d_.front();
    d_.pop_front();
    CHECK_PTHREAD(pthread_mutex_unlock(&mu_), &mu_);
    return true;
  }
 private:
  pthread_mutex_t mu_;
  pthread_cond_t cond_;
  deque<void*> d_;
  DISALLOW_COPY_AND_ASSIGN(ProducerConsumerQueue);
};

#undef CHECK_PTHREAD

// Errno returns a util::Status based on the current value of errno and the
// success flag.  If success is true, returns OK.  Otherwise, returns a
// FAILED_PRECONDITION error based on errno.
inline Error Errno(bool success = false) {
  if (success) {
    return SUCCESS;
  }
  return ERROR(strerror(errno));
}

// Linux libaio uses a different errno convention than normal syscalls.  Instead
// of returning -1 and setting errno, they return -errno on errors.  This
// function takes in that return value and returns OK if ret >= 0 or a
// FAILED_PRECONDITION error based on -ret otherwise.
inline Error AIOErrno(int ret) {
  if (ret < 0) {
    return ERROR(strerror(-ret));
  }
  return SUCCESS;
}

////////////////////////////////////////////////////////////////////////////////
//// Filesystem helpers.

string Basename(const string& filename);
string Dirname(const string& filename);
Error MkDirRecursive(const string& dirname);
inline string HiddenFile(const string& dirname, int64_t micros) {
  CHECK(dirname[dirname.size() - 1] == '/');
  return dirname + "." + to_string(micros);
}
inline string UnhiddenFile(const string& dirname, int64_t micros) {
  CHECK(dirname[dirname.size() - 1] == '/');
  return dirname + to_string(micros);
}

}  // namespace st

#endif // STENOGRAPHER_UTIL_H_