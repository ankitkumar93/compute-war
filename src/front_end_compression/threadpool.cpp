#include "threadpool.h"

ThreadPool::ThreadPool(uint32_t numThreads)
{
    ASSERT_OP(numThreads, !=, 0);
    mIsShutdown = false;
    for (uint32_t threadIndex = 0; threadIndex < numThreads; ++threadIndex)
    {
        mThreadGroup.create_thread(boost::bind(&ThreadPool::RunThread, this));
    }
}

void ThreadPool::Post(AsyncCallback callback)
{
    mQueue.push(callback);
    mConditionVariable.notify_all();
}

void ThreadPool::RunThread()
{
    while (!mIsShutdown)
    {
        AsyncCallback callback;
        if (mQueue.try_pop(callback))
        {
            callback();
        }
        else
        {
            unique_lock<std::mutex> ax(mMutex);
            mConditionVariable.wait(ax);
        }
    }

    cout <<"Exiting thread." << endl;
    ASSERT(mIsShutdown);
}

void ThreadPool::Shutdown()
{
    mIsShutdown = true;
    mThreadGroup.join_all();
}