#include "shared.h"
#include "threadpool.h"
#include "file.h"
#include "hash.h"

void RunHashingSHA256(string dataFile)
{
    File file(dataFile);
    ASSERT(file.Open());

    // Read all blocks into memory
    file.ReadAllBlocks();

    uint64_t numBlocks = file.GetNumBlocks();

    uint64_t blockIndex = 0;
    while(file.HasMoreBlocks())
    {
        HashBlockSHA256(file.GetNextBlock(), blockIndex++);
    }

    // Free memory
    file.FreeAllBlocks();

    ASSERT(file.Close());
}

void RunHashingMD5(string dataFile)
{
    File file(dataFile);
    ASSERT(file.Open());

    // Read all blocks into memory
    file.ReadAllBlocks();

    uint64_t numBlocks = file.GetNumBlocks();

    uint64_t blockIndex = 0;
    while(file.HasMoreBlocks())
    {
        HashBlockMD5(file.GetNextBlock(), blockIndex++);
    }

    // Free memory
    file.FreeAllBlocks();

    ASSERT(file.Close());
}

void RunHashingSHA256MB(string dataFile, uint64_t windowSize)
{
    File file(dataFile);
    ASSERT(file.Open());

    // Read all blocks into memory
    file.ReadAllBlocks(windowSize);

    uint64_t numBlocks = file.GetNumBlocks();

    uint64_t windowIndex = 0;
    while(file.HasMoreBlocks())
    {
        HashBlockSHA256MB(file.GetNextBlock(), windowIndex++, windowSize);
    }

    // Free memory
    file.FreeAllBlocks();

    ASSERT(file.Close());
}

void RunHashingMD5MB(string dataFile, uint64_t windowSize)
{
    File file(dataFile);
    ASSERT(file.Open());

    // Read all blocks into memory
    file.ReadAllBlocks(windowSize);

    uint64_t numBlocks = file.GetNumBlocks();

    uint64_t windowIndex = 0;
    while(file.HasMoreBlocks())
    {
        HashBlockMD5MB(file.GetNextBlock(), windowIndex++, windowSize);
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
    RunHashingSHA256(dataFile);
    RunHashingMD5(dataFile);

    // Run MB hashing with different window size
    for (uint64_t windowSize = 1; windowSize <= 4; windowSize++)
    {
        RunHashingSHA256MB(dataFile, windowSize);
        RunHashingMD5MB(dataFile, windowSize);
    }
}