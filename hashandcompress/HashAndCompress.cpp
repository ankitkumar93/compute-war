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
#include <boost/function.hpp>
#include <boost/program_options.hpp>
#include <boost/range/irange.hpp>
#include <boost/thread/thread.hpp>
#include <boost/timer/timer.hpp>

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

void fakeCompression(const char* src, char* dst, size_t len)
{
}

boost::function<void(const char*, char*, size_t)> doCompression = boost::bind(&fakeCompression, _1, _2, _3);

void fakeHash(const char* src, char* dst, size_t len)
{
}

boost::function<void(const char*, char*, size_t)> doHash = boost::bind(&fakeHash, _1, _2, _3);

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
    char* rawData = static_cast<char*>(malloc(readSize));
    char* compressedData = static_cast<char*>(malloc(readSize));
    char* hashes = static_cast<char*>(malloc(hashSize));

    HashOffload h(blocksPerOffload);
    boost::mutex myOffloadLock;
    size_t bytesRead;
    
    std::ifstream fs(f, std::ifstream::binary);
    if (!fs)
    {
        boost::unique_lock<boost::mutex> lock(ioLock);
        std::cerr << "warning: worker thread " << i << " unable to open " << f << std::endl;
        return;
    }

    boost::timer::auto_cpu_timer fTimer(f + " %s\n");
    
    while (!fs.eof())
    {
        fs.read(rawData, readSize);

        bytesRead = fs.gcount();

        // truncate the non-offloaded-block-size last reads
        if (bytesRead != readSize)
        {
            if (bytesRead > 0)
            {
                boost::unique_lock<boost::mutex> lock(ioLock);
                std::cerr << "truncating partial read from end of " << f << std::endl;
            }

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

        char* thisBlock = rawData;
        char* thisCompressed = compressedData;
        char* thisHash = hashes;

        while (bytesRead > 0) 
        {
            size_t thisBlockSize = std::min(bytesRead, blockSize);

            // XXX zero-fill to end of block?

            assert(bytesRead <= readSize);
            
            if (!offload)
            {
                doHash(thisBlock, thisHash, thisBlockSize);
                thisHash += sizeof (Hash);
            }

            // We're only interested in the time it takes to compress the data,
            // not in the actual compression ratios or results.
            doCompression(thisBlock, thisCompressed, thisBlockSize);
            thisCompressed += blockSize;

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

    fTimer.stop();

    {
        boost::unique_lock<boost::mutex> reportLock(ioLock);
        fTimer.report();
    }
    
    free(rawData);
    free(compressedData);
    free(hashes);
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

void usage(const char* n, const options_description& o, const char* msg = nullptr)
{
    int code = 0;
    
    if (msg != nullptr)
    {
        std::cerr << msg << std::endl;
        code = 1;
    }

    std::cerr << "Usage: " << n << " [Options] [input-file]..." << std::endl << o << std::endl;

    exit(code);
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
            ("comp-alg,C", value<std::string>()->default_value(DEFAULT_COMPRESSION_ALG), "compression algorithm")
            ("hash-alg,H", value<std::string>()->default_value(DEFAULT_HASHING_ALG), "hashing algorithm")
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
            usage(argv[0], desc);
        }

        nCompressionThreads = vm["c-threads"].as<int>();
        blockSize = vm["block-size"].as<size_t>();
        blocksPerOffload = vm["gpu-offload"].as<int>();
        offloadHashing = (blocksPerOffload > 0);

        if (vm["comp-alg"].as<std::string>() == "lzf")
        {
        }
        else if (vm["comp-alg"].as<std::string>() == "lz4")
        {
        }
        else
        {
            usage(argv[0], desc, "invalid compression algorithm specified; please use either \"lzf\" or \"lz4\"");
        }

        if (vm["hash-alg"].as<std::string>() == "skein")
        {
        }
        else if (vm["hash-alg"].as<std::string>() == "sha256mb")
        {
        }
        else
        {
            usage(argv[0], desc, "invalid hashing algorithm specified; please use either \"skein\" or \"sha256mb\"");
        }
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
    boost::unique_lock<boost::mutex> startLock(fileLock);

    for (int i : boost::irange(0, nCompressionThreads))
    {
        workers.create_thread(boost::bind(&PopAndProcessFiles, i, offloadHashing));
    }

    initGpuThread.join();

    {
        // We don't need to hold the ioLock here because none of the worker threads has been released when we
        // generate the summary line, and all of the worker threads have been joined before mTimer goes out of
        // scope.

        std::cout << argv[0];
        if (vm["comp-alg"].as<std::string>() == DEFAULT_COMPRESSION_ALG)
        {
            std::cout << " --comp-alg=" << DEFAULT_COMPRESSION_ALG;
        }
        if (vm["hash-alg"].as<std::string>() == DEFAULT_HASHING_ALG)
        {
            std::cout << " --hash-alg=" << DEFAULT_HASHING_ALG;
        }
        for (int a : boost::irange(1, argc))
        {
            std::cout << " " << argv[a];
        }
        std::cout << std::endl;
        
        boost::timer::auto_cpu_timer mTimer("total %s\n");
        startLock.unlock();
        workers.join_all();
    }

    if (!offloadHashing)
    {
        exit(0);
    }
    
    // Clean up hashing offload thread
    {
        boost::unique_lock<boost::mutex> hLock(hashLock);
        allWorkFinished = true;
    }
    
    hashCV.notify_one();
    offloadThread.join();
}
