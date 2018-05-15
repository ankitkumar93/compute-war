/*
 * Copyright 2018 NetApp, Inc. All rights reserved.
 */

#ifndef _HASHOFFLOAD_H_
#define _HASHOFFLOAD_H_

#include <cstddef>

#include <boost/function.hpp>
#include <boost/thread/thread.hpp>

typedef enum {hInit, hQueued, hOffloaded, hComplete} HashOffloadState;

class HashOffload
{
public:
    HashOffload(int nBlocks) : offloadCount(nBlocks), data(nullptr), results(nullptr), state(hInit) {};
    
    void Enqueue()
    {
        assert(state == hInit);
        state = hQueued;
    };

    void Start()
    {
        assert(state == hQueued);
        state = hOffloaded;
        // start the offload (xfer data, load kernel)
    };

    // May block on data availability and transfer
    void Complete()
    {
        assert(state == hOffloaded);
        // wait for and reap the results
        state = hComplete;
        onComplete();
    };

    bool Completed() const { return (state == hComplete); };
    
    void DoOffload()
    {
        Start();
        Complete();
    }
    
    void Reset(char* d, char* r, boost::function<void()> f)
    {
        data = d;
        results = r;
        state = hInit;
        onComplete = f;
    };

private:
    int offloadCount;
    char* data;
    char* results;
    HashOffloadState state;
    boost::function<void()> onComplete;
};

#endif /* _HASHOFFLOAD_H_ */
