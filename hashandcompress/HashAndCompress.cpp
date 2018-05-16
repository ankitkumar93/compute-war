// Copyright 2018 NetApp, Inc. All rights reserved.

#include "HashAndCompress.h"
#include "HashOffload.h"

#include <lzf/lzf.h>
#include <lz4/lz4.h>

#include <hash.h>

#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <numeric>
#include <queue>
#include <utility>

#include <boost/bind.hpp>
#include <boost/function.hpp>
#include <boost/program_options.hpp>
#include <boost/range/irange.hpp>
#include <boost/thread/thread.hpp>
#include <boost/timer/timer.hpp>

using namespace boost::program_options;

class ThroughputTracker
{
public:
    ThroughputTracker() : blocks(0), microseconds(0) {};

    void Reset() 
    {
        blocks = 0;
        microseconds = 0;
    };
    
    void Track(unsigned long b, unsigned long u) 
    {
        boost::unique_lock<boost::mutex> g(guard);
        blocks += b;
        microseconds += u;
    };

    // Guarded (for use in aggregating numbers)
    void Track(const ThroughputTracker& t)
    {
        boost::unique_lock<boost::mutex> g(guard);
        blocks += t.Blocks();
        microseconds += t.Time();
    };

    unsigned long Blocks() const 
    {
        return blocks;
    };

    unsigned long Time() const
    {
        return microseconds;
    };
    
    double Throughput() const 
    {
        // 4kB per block, 1000000 microseconds per second, 1/1024 mB per kB
        return blocks * 4 * 1000000 / 1024 / microseconds;
    };

    void Report(const std::string& name) const 
    {
        std::cout << name << " 4KB_blocks=" << blocks << " microseconds=" << microseconds << " MB/s=" << Throughput() << std::endl;
    };

private:
    boost::mutex guard;
    unsigned long blocks;
    unsigned long microseconds;
};

ThroughputTracker totalTp;
        
boost::mutex ioLock;

size_t blockSize = 4096;
size_t singleHashSize;

int readBlockFactor;
int hashBlockFactor;

void initializeGpu()
{
    // TODO: entry point to initiate simple access to setup GPU
}

boost::mutex hashLock;
bool allWorkFinished = false;
std::queue<HashOffload*> hashQueue;
boost::condition_variable hashCV;

size_t fakeCompression(const char* src, char* dst, size_t len)
{
    boost::unique_lock<boost::mutex> i(ioLock);
    printf("fakeCompression called on %lu bytes at %p\n", len, src);
}

boost::function<size_t(const char*, char*, size_t)> doCompression = boost::bind(&fakeCompression, _1, _2, _3);

void fakeHashing(const char* src, char* dst, int count)
{
    boost::unique_lock<boost::mutex> i(ioLock);
    printf("fakeHashing called on %d blocks at %p\n", count, src);
}

boost::function<void(const char*, char*, int)> doHashing = boost::bind(&fakeHashing, _1, _2, _3);

void doSkeinHashing(const char* src, char* dst, int count)
{
    Skein_256_Ctxt_t ctx;
    
    for (int i : boost::irange(0, count))
    {
        Skein_256_Init(&ctx, kHashSizeBitsSkein);
        Skein_256_Update(&ctx, (uint8_t*)src, blockSize);
        Skein_256_Final(&ctx, (uint8_t*)dst);

        src += blockSize;
    }
}

void doSHA256MBHashing(const char* src, char* dst, int count)
{
    // Ctx
    SHA256_HASH_CTX_MGR* mgr = NULL;
    posix_memalign((void**)&mgr, 16, sizeof(SHA256_HASH_CTX_MGR));
    sha256_ctx_mgr_init(mgr);
    SHA256_HASH_CTX ctxpool;
    hash_ctx_init(&ctxpool);

    for (int i : boost::irange(0, count))
    {
        sha256_ctx_mgr_submit(mgr, &ctxpool, (char *)src, kBlockSize, HASH_ENTIRE);
        src += kBlockSize;
    }

     SHA256_HASH_CTX* ctx;
    while ((ctx = sha256_ctx_mgr_flush(mgr)) != NULL)
    {
        --count;
        // No, really, how do I retrieve the hash value?
    }
    assert(count == 0);

    free(mgr);
}

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
    // Buffers to hold input data, compressed data, and hashes
    // Neither compression nor hashing is actually verified

    size_t readSize = blockSize * readBlockFactor;
    char* rawData = static_cast<char*>(malloc(readSize));
    char* compressedData = static_cast<char*>(malloc(2 * readSize));

    size_t hashSize = singleHashSize * hashBlockFactor;
    char* hashData = static_cast<char*>(malloc(hashSize));

    HashOffload h(hashBlockFactor);
    boost::mutex myOffloadLock;
    size_t bytesRead;

    ThroughputTracker tp;
    
    std::ifstream fs(f, std::ifstream::binary);
    if (!fs)
    {
        boost::unique_lock<boost::mutex> lock(ioLock);
        std::cerr << "warning: worker thread " << i << " unable to open " << f << std::endl;
        return;
    }

    int blocksProcessed = 0;
    
    while (!fs.eof())
    {
        fs.read(rawData, readSize);

        // truncate partial last reads; 
        if ((bytesRead = fs.gcount()) != readSize)
        {
            assert(fs.eof());
            break;
        }

        char* thisBlock = rawData;
        char* thisCompressed = compressedData;

        while (bytesRead > 0) 
        {
            auto startTime = chrono::high_resolution_clock::now();
            size_t thisBlockSize = std::min(bytesRead, blockSize);
            bool waitForOffload = false;

            assert(bytesRead <= readSize);
            
            if (blocksProcessed % hashBlockFactor == 0)
            {
                if (offload)
                {
                    h.Reset(thisBlock, hashData, boost::bind(&boost::mutex::unlock, &myOffloadLock));
                    boost::unique_lock<boost::mutex> hLock(hashLock);
                    hashQueue.push(&h);
                    myOffloadLock.lock();
                    h.Enqueue();
                    hLock.unlock();
                    hashCV.notify_one();
                    waitForOffload = true;
                }
                else
                {
                    doHashing(thisBlock, hashData, hashBlockFactor);
                }
            }
            
            // We're only interested in the time it takes to compress the data,
            // not in the actual compression ratios or results.
            doCompression(thisBlock, thisCompressed, thisBlockSize);

            if (waitForOffload)
            {
                boost::unique_lock<boost::mutex> oLock(myOffloadLock);
                assert(h.Completed());
            }

            blocksProcessed++;
            auto endTime = chrono::high_resolution_clock::now();
            tp.Track(readBlockFactor, chrono::duration_cast<chrono::microseconds>(endTime - startTime).count());

            if (bytesRead <= blockSize)
            {
                break;
            }
            
            thisBlock += blockSize;
            thisCompressed += 2 * blockSize;
            bytesRead -= blockSize;
        }
    }

    
    free(rawData);
    free(compressedData);
    free(hashData);

    if (blocksProcessed == 0)
    {
        boost::unique_lock<boost::mutex> reportLock(ioLock);
        std::cerr << f << " did not contain enough data to generate meaningful output." << std::endl;
        return;
    }

    totalTp.Track(tp);

    boost::unique_lock<boost::mutex> reportLock(ioLock);
    tp.Report(f);
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

    std::string command(argv[0]);
    
    try
    {
        options_description desc{"Options"};
        desc.add_options()
            ("help,h", "usage")
            ("c-threads,c", value<int>()->default_value(DEFAULT_THREADS), "compression threads")
            ("gpu-offload,g", value<bool>()->default_value(DEFAULT_OFFLOAD), "use GPU offload?")
            ("read-blocks,r", value<int>()->default_value(DEFAULT_BLOCKS_PER_READ), "read blocking factor")
            ("hash-blocks,G", value<int>()->default_value(DEFAULT_HASH_BLOCKS), "hash grouping factor")
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
        offloadHashing = vm["gpu-offload"].as<bool>();

        readBlockFactor = vm["read-blocks"].as<int>();
        hashBlockFactor = vm["hash-blocks"].as<int>();

        if (readBlockFactor % hashBlockFactor != 0)
        {
            usage(argv[0], desc, "read blocking factor must be an integer multiple of hash blocking factor");
        }
        
        if (vm["comp-alg"].as<std::string>() == "lzf")
        {
            doCompression = [](const char* s, char* d, size_t l) -> size_t
            {
                return lzf_compress(s, l, d, l - 1);
            };
        }
        else if (vm["comp-alg"].as<std::string>() == "lz4")
        {
            doCompression = [](const char* s, char* d, size_t l) -> size_t
            {
                return LZ4_compress_default(s, d, l, 2 * l);
            };
        }
        else
        {
            usage(argv[0], desc, "invalid compression algorithm specified; please use either \"lzf\" or \"lz4\"");
        }

        if (vm["hash-alg"].as<std::string>() == "skein")
        {
            singleHashSize = kHashSizeBytesSkein;
            doHashing = boost::bind(&doSkeinHashing, _1, _2, _3);
        }
        else if (vm["hash-alg"].as<std::string>() == "sha256mb")
        {
            singleHashSize = kHashSizeBytesSHA;

            if (hashBlockFactor == 1)
            {
                doHashing = [](const char* s, char *d, int l) -> void
                {
                    assert(l == 1);
                    SHA256((const unsigned char*)s, kBlockSize, (unsigned char*)d);
                };
            }
            else
            {
                doHashing = boost::bind(&doSHA256MBHashing, _1, _2, _3);
            }
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

    // We don't need to hold the ioLock here because none of the worker threads has been released when we
    // generate the summary line, and all of the worker threads have been joined before mTimer goes out of
    // scope.

    std::cout
        << argv[0]
        << " --c-threads=" << nCompressionThreads
        << " --gpu-offload=" << ((offloadHashing) ? "true" : "false")
        << " --read-blocks=" << readBlockFactor
        << " --hash-blocks=" << hashBlockFactor
        << " --comp-alg=" << vm["comp-alg"].as<std::string>()
        << " --hash-alg=" << vm["hash-alg"].as<std::string>();

    for (const std::string& s : inputFiles)
    {
        std::cout << " " << s;
    }
    
    std::cout << std::endl;
        
    startLock.unlock();
    workers.join_all();

    boost::unique_lock<boost::mutex> reportLock(ioLock);
    totalTp.Report("total");
    reportLock.unlock();
    
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
