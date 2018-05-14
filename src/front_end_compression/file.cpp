#include "file.h"

File::File(string path) :
    mPath(path)
{}

bool File::Open()
{
    mFileDescriptor = open(mPath.c_str(), O_RDONLY, S_IRWXG);
    return mFileDescriptor > 0;
}

bool File::Close()
{
    return close(mFileDescriptor) == 0;
}

void File::ReadAllBlocks()
{
    uint64_t fileSize = GetFileSize(mPath);
    uint64_t numBlocks = fileSize / kBlockSize;
    ASSERT_OP(numBlocks, >, 0);

    for (uint64_t blockIndex = 0; blockIndex < numBlocks; ++blockIndex)
    {
        ReadNextBlock(kBlockSize);
    }
}

void File::FreeAllBlocks()
{
    ASSERT(mBlocks.empty());

    while (!mBlocksRead.empty())
    {
        uint8_t* block = mBlocksRead.front();
        mBlocksRead.pop();

        free(block);
    }
}

void File::ReadNextBlock(uint64_t blockSize)
{
    uint8_t* dataBuffer = (uint8_t*)malloc(blockSize);
    ASSERT(dataBuffer != NULL);

    ssize_t bytesRead = read(mFileDescriptor, (void*)dataBuffer, static_cast<size_t>(blockSize));
    if (bytesRead != blockSize)
    {
        REPORT_ERR("Read failed");
    }

    mBlocks.push(dataBuffer);
}

uint64_t File::GetNumBlocks()
{
    return mBlocks.size();
}

bool File::HasMoreBlocks()
{
    return !mBlocks.empty();
}

uint8_t* File::GetNextBlock()
{
    uint8_t* block = mBlocks.front();
    mBlocks.pop();

    // Add it to blocks read to be free(d) later
    mBlocksRead.push(block);

    return block;
}