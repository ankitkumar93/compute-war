#include "shared.h"
#include "threadpool.h"
#include "directory.h"
#include "file.h"
#include "hash.h"

void RunHashingSB(string dataFile)
{
    File file(dataFile);
    ASSERT(file.Open());

    // Read all blocks into memory
    file.ReadAllBlocks();

    uint64_t numBlocks = file.GetNumBlocks();

    uint64_t blockIndex = 0;
    while(file.HasMoreBlocks())
    {
        uint8_t* block = file.GetNextBlock();
        HashBlockSkein256(block, blockIndex++, dataFile);
        HashBlockSHA256(block, blockIndex++, dataFile);
    }

    // Free memory
    file.FreeAllBlocks();

    ASSERT(file.Close());
}

void RunHashingMB(string dataFile, uint64_t windowSize)
{
    File file(dataFile);
    ASSERT(file.Open());

    // Read all blocks into memory
    file.ReadAllBlocks(windowSize);

    uint64_t numBlocks = file.GetNumBlocks();
    uint64_t numWindows = numBlocks / windowSize;

    for(uint64_t windowIndex = 0; windowIndex < numWindows; ++windowIndex)
    {
        // Form window
        uint64_t bufferSize = kBlockSize * windowSize;
        uint8_t* dataPtr = (uint8_t*)malloc(bufferSize);
        ASSERT(dataPtr != NULL);

        for(uint64_t offset = 0; offset < bufferSize; offset += kBlockSize)
        {
            uint8_t* block = file.GetNextBlock();
            memcpy(dataPtr + offset, block, kBlockSize);
        }

        // SHA256
        HashBlockSHA256MB(dataPtr, windowIndex, windowSize, dataFile);

        // Free
        free(dataPtr);
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
    string dataDir = ParseArgs(argc, argv);
    Directory directory(dataDir);
    directory.GetAllFiles();

    while (directory.HasMoreFiles())
    {
        string dataFile = directory.GetNextFile();
        RunHashingSB(dataFile);

        // Run MB hashing with different window size
        for (uint64_t windowSize = 1; windowSize <= 64; windowSize++)
        {
            RunHashingMB(dataFile, windowSize);
        }
    }
}