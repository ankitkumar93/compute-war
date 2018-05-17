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

#include <tbb/concurrent_queue.h>

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
        printf("src: %p, i: %d\n", src, i);
        Skein_256_Init(&ctx, kHashSizeBitsSkein);
        Skein_256_Update(&ctx, (uint8_t*)src, blockSize);
        Skein_256_Final(&ctx, (uint8_t*)dst);

        src += blockSize;
        dst += kHashSizeBytesSkein;
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
        // No, really, how do I retrieve the hash value?
    }

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

// Queue for data blocks
tbb::concurrent_queue<string> dataBlocksQueue;

void ReadFile(string file)
{
    std::ifstream fs(file, std::ifstream::binary);
    if (!fs)
    {
        std::cerr << "Unable to open file: " << file << std::endl;
        return;
    }

    size_t readSize = blockSize * readBlockFactor;
    char* rawData = static_cast<char*>(malloc(readSize));
    assert(rawData != NULL);

    size_t bytesRead = 0;
    while (!fs.eof())
    {
        fs.read(rawData, readSize);

        // truncate partial last reads;
        if ((bytesRead = fs.gcount()) != readSize)
        {
            break;
        }
        else
        {
            dataBlocksQueue.push(string(rawData));
        }
    }

    free(rawData);
}

void ReadAllFiles(const std::vector<string>& files)
{
    for (const string& file: files)
    {
        ReadFile(file);
    }
}

// TODO: Temporarily keep things compiling
typedef int Hash;

void ProcessBlock(const std::string& data)
{
    size_t hashSize = singleHashSize * hashBlockFactor;
    char* hashData = static_cast<char*>(malloc(hashSize));
    assert(hashData != NULL);

    size_t readSize = blockSize * readBlockFactor;
    char* compressedData = static_cast<char*>(malloc(2 * readSize));
    assert(compressedData != NULL);

    // Compress
    const char* dataPtr = data.c_str();
    for (size_t offset = 0; offset < readSize; offset += blockSize)
    {
        const char* thisBlock = dataPtr;
        char* thisCompressed = compressedData;

        // We're only interested in the time it takes to compress the data,
        // not in the actual compression ratios or results.
        doCompression(thisBlock, thisCompressed, blockSize);

        thisBlock += blockSize;
        thisCompressed += 2 * blockSize;
    }

    // Hash
    doHashing(dataPtr, hashData, hashBlockFactor);

    free(compressedData);
    free(hashData);
}

void PopAndProcessBlocks()
{
    string dataBlock;
    while (dataBlocksQueue.try_pop(dataBlock))
    {
        ProcessBlock(dataBlock);
    }

    assert(dataBlocksQueue.empty());
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
        hashBlockFactor = readBlockFactor;
        // hashBlockFactor = vm["hash-blocks"].as<int>();

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
            doHashing = boost::bind(&doSHA256MBHashing, _1, _2, _3);
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

    std::vector<std::string> inputFiles;
    if (vm.count("input-file"))
    {
        inputFiles = vm["input-file"].as<std::vector<std::string>>();
    }
    else
    {
        inputFiles.push_back(std::string("-"));
    }

    // Read all files into memory and chunk them into blocks
    ReadAllFiles(inputFiles);

    size_t numBlocks = dataBlocksQueue.unsafe_size();
    uint64_t totalDataSize = blockSize * numBlocks * readBlockFactor;

    auto startTime = chrono::high_resolution_clock::now();
    boost::thread_group workers;
    for (int i : boost::irange(0, nCompressionThreads))
    {
        workers.create_thread(boost::bind(&PopAndProcessBlocks));
    }
    workers.join_all();
    auto endTime = chrono::high_resolution_clock::now();

    uint64_t totalTimeMS = chrono::duration_cast<chrono::milliseconds>(endTime - startTime).count();
    uint64_t throughputMBPS = (totalDataSize * 1000) / (totalTimeMS * 1024 * 1024);

    cout << vm["hash-alg"].as<std::string>() << LOG_SEPARATOR
         << vm["comp-alg"].as<std::string>() << LOG_SEPARATOR
         << totalTimeMS << LOG_SEPARATOR
         << throughputMBPS << endl;

    return 0;
}
