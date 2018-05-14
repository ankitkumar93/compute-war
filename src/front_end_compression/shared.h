#ifndef __SHARED_H__
#define __SHARED_H__

#include <iostream>
#include <cstdint>
#include <chrono>
#include <string>
#include <queue>
#include <condition_variable>
#include <mutex>
#include <atomic>

#include <boost/bind.hpp>

// File I/O
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

using namespace std;

// Globals
static constexpr uint64_t kBlockSize = 4096;
static constexpr uint64_t kNumThreads = 14;
static const string kLogFileName = "results.log";

// Helpers
#define ASSERT(condition)               \
while (!(condition))                    \
{                                       \
    cerr << "Assert failed: "           \
         << #condition                  \
         << " at: "                     \
         << __FILE__                    \
         << ":"                         \
         << __LINE__;                   \
    abort();                            \
}

#endif // __SHARED_H__