#include "shared.h"
#include "directory.h"
#include "file.h"
#include "hash.h"

void RunHashing(string dataFile, Hasher& hasher)
{
    File file(dataFile);
    ASSERT(file.Open());

    // Read all blocks into memory
    file.ReadAllBlocks();

    uint64_t numBlocks = file.GetNumBlocks();

    while(file.HasMoreBlocks())
    {
        hasher.HashBlock(file.GetNextBlock());
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

    Hasher hasher;

    while (directory.HasMoreFiles())
    {
        RunHashing(directory.GetNextFile(), hasher);
    }

    // Log
    hasher.LogResults();
}