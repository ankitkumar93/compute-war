#include "shared.h"
#include "threadpool.h"
#include "file.h"
#include "compress.h"

void CompressBlock(uint8_t* dataPtr, uint64_t blockIndex)
{
    CompressBlockLZF(dataPtr, blockIndex);
    // CompressBlockLZ4(dataPtr, blockIndex);
    // CompressBlockLZ4Fast(dataPtr, blockIndex);
}

void RunCompression(string dataFile, uint32_t numThreads)
{
    File file(dataFile);
    ASSERT(file.Open());

    // Read all blocks into memory
    file.ReadAllBlocks();

    uint64_t numBlocks = file.GetNumBlocks();

    uint64_t blockIndex = 0;
    while(file.HasMoreBlocks())
    {
        CompressBlock(file.GetNextBlock(), blockIndex++);
    }

    // Free memory
    file.FreeAllBlocks();

    ASSERT(file.Close());
}

string ParseArgs(int argc, char* argv[])
{
    ASSERT_OP(argc, ==, 2);

    return string(argv[1]);
}

int main(int argc, char* argv[])
{
    string dataFile = ParseArgs(argc, argv);

    // Single threaded compression
    RunCompression(dataFile, 1);
}