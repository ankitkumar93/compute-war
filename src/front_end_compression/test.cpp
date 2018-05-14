#include "shared.h"
#include "threadpool.h"
#include "file.h"

enum CompressionAlgorithmType
{
    LZO,
    Snappy,
    LZ4Fast,
    LZ4
};

atomic<uint64_t> globalBlockCounter;

void CompressBlock(uint8_t* dataPtr)
{
    globalBlockCounter++;
}

void RunCompression(string dataFile, uint32_t numThreads)
{
    globalBlockCounter = 0;

    File file(dataFile);
    ASSERT(file.Open());

    // Read all blocks into memory
    file.ReadAllBlocks();

    uint64_t numBlocks = file.GetNumBlocks();

    // Create thread pool
    ThreadPool threadPool(numThreads);

    while(file.HasMoreBlocks())
    {
        threadPool.Post(boost::bind(&CompressBlock, file.GetNextBlock()));
    }

    // Wait for all blocks to be compressed
    while (globalBlockCounter != numBlocks);

    // Shutdown the thread pool
    threadPool.Shutdown();

    // Free memory
    file.FreeAllBlocks();

    ASSERT(file.Close());
}

string ParseArgs(int argc, char* argv[])
{
    ASSERT(argc == 2);

    return string(argv[1]);
}

int main(int argc, char* argv[])
{
    string dataFile = ParseArgs(argc, argv);

    // Single threaded compression
    RunCompression(dataFile, 1);

    // Multi threaded compression
    RunCompression(dataFile, kNumThreads);
}