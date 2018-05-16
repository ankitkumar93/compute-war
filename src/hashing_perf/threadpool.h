#ifndef __THREADPOOL_H__
#define __THREADPOOL_H__

#include "shared.h"
#include <tbb/concurrent_queue.h>
#include <boost/thread.hpp>

typedef std::function<void()> AsyncCallback;

class ThreadPool
{
public:
    ThreadPool(uint32_t numThreads);
    void Post(AsyncCallback callback);
    void RunThread();
    void Shutdown();

private:
    boost::thread_group mThreadGroup;
    tbb::concurrent_queue<AsyncCallback> mQueue;
    mutex mMutex;
    condition_variable mConditionVariable;
    atomic<bool> mIsShutdown;
};

#endif // __THREADPOOL_H__