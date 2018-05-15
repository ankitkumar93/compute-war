#ifndef __HASH_H__
#define __HASH_H__

#include "shared.h"

#include <isa-l_crypto.h>
#include <openssl/sha.h>
#include <openssl/md5.h>

extern "C" {
#include "skein/skein.h"
}

static constexpr int kHashSizeBytesSHA = 32;
static constexpr int kHashSizeBitsSHA = kHashSizeBytesSHA * 8;
static constexpr int kHashSizeBytesSkein = 16;
static constexpr int kHashSizeBitsSkein = kHashSizeBytesSkein * 8;

class Hasher
{
public:
    void HashBlock(uint8_t* dataPtr);
    void LogResults();
    void ReleaseMemory();

private:
    void HashBlockSHA256(uint8_t* dataPtr, uint8_t* hashBuffer)
    {
        SHA256(dataPtr, kBlockSize, hashBuffer);
    }

    void HashBlockSkein256(uint8_t* dataPtr, uint8_t* hashBuffer)
    {
        Skein_256_Ctxt_t ctx;
        Skein_256_Init(&ctx, kHashSizeBitsSkein);
        Skein_256_Update(&ctx, dataPtr, kBlockSize);
        Skein_256_Final(&ctx, hashBuffer);
    }

    void LogResultsInternal(queue<uint8_t*>& hashes, string alg);
    double LogByteResults(size_t byte, const std::map<uint8_t, size_t>& distributionMap, string alg);

private:
    queue<uint8_t*> mSHA256Hashes;
    queue<uint8_t*> mSkeinHashes;
};

#endif // __HASH_H__