#ifndef __SHARED_H__
#define __SHARED_H__

#include <iostream>
#include <cstdint>
#include <cstring>
#include <chrono>
#include <string>
#include <cmath>
#include <queue>
#include <map>
#include <condition_variable>
#include <mutex>
#include <atomic>
#include <errno.h>

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
         << __LINE__                    \
         << endl;                       \
    abort();                            \
}

#define ASSERT_OP(var1_, op_, var2_)    \
while (!((var1_) op_ (var2_)))          \
{                                       \
    cerr << "Assert failed: "           \
         << #var1_ << "=" << (var1_)    \
         << ": "                        \
         << #var2_ << "=" << (var2_)    \
         << " at: "                     \
         << __FILE__                    \
         << ":"                         \
         << __LINE__                    \
         << endl;                       \
    abort();                            \
}

#define REPORT_ERR(msg)                                                 \
{                                                                       \
    cout << "Error: "                                                   \
         << strerror(errno)                                             \
         << ", msg: "                                                   \
         << msg                                                         \
         << ", at: "                                                    \
         << __FILE__                                                    \
         << ":"                                                         \
         <<__LINE__                                                     \
         << endl;                                                       \
    exit(EXIT_FAILURE);                                                 \
}                                                                       \

#define PANIC_BAD_VALUE(val_)      \
{                                       \
    cerr << "Bad value: "               \
         << #val_                       \
         << " at: "                     \
         << __FILE__                    \
         << ":"                         \
         << __LINE__                    \
         << endl;                       \
    abort();                            \
}

// Compression alg
enum CompressionAlgorithmType
{
    LZF,
    LZ4,
    LZ4Fast,
    LZO
};

static string AlgToString(CompressionAlgorithmType alg)
{
    string str;
    switch (alg)
    {
        case LZF:
            str = "LZF";
            break;
        case LZ4:
            str = "LZ4";
            break;
        case LZ4Fast:
            str = "LZ4Fast";
            break;
        case LZO:
            str = "LZO";
            break;
        default:
            PANIC_BAD_VALUE(alg);
    }

    return str;
}

#endif // __SHARED_H__