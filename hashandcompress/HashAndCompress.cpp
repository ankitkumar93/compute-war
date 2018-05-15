// Copyright 2018 NetApp, Inc. All rights reserved.

#include "HashAndCompress.h"
#include "HashOffload.h"

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <queue>
#include <utility>

#include <boost/bind.hpp>
#include <boost/program_options.hpp>
#include <boost/range/irange.hpp>
#include <boost/thread/thread.hpp>

using namespace boost::program_options;

boost::mutex ioLock;

size_t blockSize;
int blocksPerOffload;

void initializeGpu()
{
    // TODO: entry point to initiate simple access to setup GPU
}

boost::mutex hashLock;
bool allWorkFinished = false;
std::queue<HashOffload*> hashQueue;
boost::condition_variable hashCV;

void hashing_offload_entry_point()
{
    boost::unique_lock<boost::mutex> hLock(hashLock);

    while (!allWorkFinished)
    {
        if (hashQueue.size() == 0)
        {
            hashCV.wait(hLock);
            continue;
        }

        HashOffload* h = hashQueue.front();
        hashQueue.pop();

        hLock.unlock();

        h->DoOffload();

        hLock.lock();
    }

    // TODO: any shutdown operations to close out a connection to the GPU should happen here
}

boost::mutex fileLock;
std::vector<std::string> inputFiles;

// TODO: Temporarily keep things compiling
typedef int Hash;

void ProcessFile(const std::string& f, int i, bool offload)
{
    size_t readSize = blockSize;
    size_t hashSize = sizeof (Hash);

    // Adjust target buffer sizes to account for block aggregation
    if (offload)
    {        
        readSize *= blocksPerOffload;
        hashSize *= blocksPerOffload;
    }

    // Buffers to hold input data, compressed data, and hashes
    // Neither compression nor hashing is actually verified
    char rawData[readSize];
    char compressedData[readSize];
    char hashes[hashSize];

    HashOffload h;
    boost::mutex myOffloadLock;
    size_t bytesRead;
    
    std::ifstream fs(f, std::ifstream::binary);
    if (!fs)
    {
        boost::unique_lock<boost::mutex> lock(ioLock);
        std::cerr << "warning: worker thread " << i << " unable to open " << f << std::endl;
        return;
    }

    // TODO: start per-file timer
    
    while (!fs.eof())
    {
        fs.read(&rawData[0], readSize);

        bytesRead = fs.gcount();
        
        if (bytesRead == 0)
        {
            continue;
        }

        if (offload)
        {
            h.Reset(rawData, hashes, boost::bind(&boost::mutex::unlock, &myOffloadLock));
            boost::unique_lock<boost::mutex> hLock(hashLock);
            hashQueue.push(&h);
            myOffloadLock.lock();
            h.Enqueue();
            hLock.unlock();
            hashCV.notify_one();
        }

        char* thisBlock = &rawData[0];
        char* thisCompressed = &compressedData[0];
        char* thisHash = &hashes[0];

        while (bytesRead > 0) 
        {
            size_t thisBlockSize = std::min(bytesRead, blockSize);

            // XXX zero-fill to end of block?

            assert(bytesRead <= readSize);
            
            if (!offload)
            {
                // TODO: call single-block hash function to store hash of data from thisBlock into thisHash
                
                thisHash += sizeof (Hash);
            }

            {
                // TODO: call single-block compression function to compress data from thisBlock into
                // thisCompressed. Limit size of output to blockSize.
                //
                // We're only interested in the time it takes to compress the data, not in the actual
                // compression ratios or results.

                thisCompressed += blockSize;
            }

            if (bytesRead <= blockSize)
            {
                break;
            }
            
            thisBlock += blockSize;
            bytesRead -= blockSize;
        }

        if (offload)
        {
            boost::unique_lock<boost::mutex> oLock(myOffloadLock);
            assert(h.Completed());
        }
    }

    // TODO: stop per-file timer and report
}

void PopAndProcessFiles(int i, bool offload)
{
    // Lazy, perhaps, but the file vector is completely initialized prior to spawning the worker threads, so
    // we need only wait on an empty vector. There's no vector-filling thread, so no call for synchronization
    // therewith.

    while (1) {
        boost::unique_lock<boost::mutex> lock(fileLock);

        if (inputFiles.size() == 0)
        {
            break;
        }
        
        std::string f = inputFiles.back();
        inputFiles.pop_back();
        lock.unlock();
        ProcessFile(f, i, offload);
    }
}

int main(int argc, const char* argv[])
{
    int nCompressionThreads;
    bool offloadHashing;

    variables_map vm;

    try
    {
        options_description desc{"Options"};
        desc.add_options()
            ("help,h", "usage")
            ("c-threads,c", value<int>()->default_value(DEFAULT_THREADS), "compression threads")
            ("gpu-offload,g", value<int>()->default_value(DEFAULT_OFFLOAD), "block count for each GPU offload")
            ("block-size,b", value<size_t>()->default_value(DEFAULT_BLOCK_SIZE), "bytes per block")
            ;

        options_description hidden{"Hidden options"};
        hidden.add_options()
            ("input-file,i", value<std::vector<std::string> >(), "input file")
            ;
        
        options_description all{"All options"};
        all.add(desc).add(hidden);

        positional_options_description p;
        p.add("input-file", -1);

        store(command_line_parser(argc, argv).options(all).positional(p).run(), vm);

        if (vm.count("help"))
        {
            std::cout << "Usage: " << argv[0] << " [Options] [input-file]..." << std::endl;
            std::cout << desc << std::endl;
            exit(0);
        }

        nCompressionThreads = vm["c-threads"].as<int>();
        blockSize = vm["block-size"].as<size_t>();
        blocksPerOffload = vm["gpu-offload"].as<int>();
        offloadHashing = (blocksPerOffload > 0);
    }
    catch (const error& ex)
    {
        std::cerr << ex.what() << std::endl;
    }
    
    boost::thread initGpuThread;
    boost::thread offloadThread;

    if (offloadHashing)
    {
        boost::thread iGpu(initializeGpu);
        initGpuThread = move(iGpu);

        boost::thread oHash(&hashing_offload_entry_point);
        offloadThread = move(oHash);
    }

    if (vm.count("input-file"))
    {
        inputFiles = vm["input-file"].as<std::vector<std::string>>();
    }
    else
    {
        inputFiles.push_back(std::string("-"));
    }

    boost::thread_group workers;
    {
        boost::unique_lock<boost::mutex> lock(fileLock);

        for (int i : boost::irange(0, nCompressionThreads))
        {
            workers.create_thread(boost::bind(&PopAndProcessFiles, i, offloadHashing));
        }

        // Wait for GPU initialization to complete; this is done with the fileLock held so that, on exit from
        // this scope, we're ready to start the master timer.
        initGpuThread.join();
    }

    // TODO: start master timer

    // Wait for all work to complete
    workers.join_all();

    // TODO: stop master timer and report

    if (!offloadHashing)
    {
        exit(0);
    }
    
    // Clean up hashing offload thread
    boost::unique_lock<boost::mutex> hLock(hashLock);
    allWorkFinished = true;
    hLock.unlock();
    hashCV.notify_one();
    offloadThread.join();
}
