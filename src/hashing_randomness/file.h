#ifndef __FILE_H__
#define __FILE_H__

#include "shared.h"

class File
{
public:
    File(string path);
    bool Open();
    bool Close();

    void ReadAllBlocks(uint64_t windowSize = 1);
    void FreeAllBlocks();

    uint64_t GetNumBlocks();
    bool HasMoreBlocks();
    uint8_t* GetNextBlock();

private:
    uint64_t GetFileSize(string fileName)
    {
        struct stat statObj;

        int rc = stat(mPath.c_str(), &statObj);
        ASSERT(rc == 0);

        return statObj.st_size;
    }

    void ReadNextBlock(uint64_t blockSize);

private:
    int mFileDescriptor;
    string mPath;
    queue<uint8_t*> mBlocks;
    queue<uint8_t*> mBlocksRead;
};

#endif // __FILE_H__