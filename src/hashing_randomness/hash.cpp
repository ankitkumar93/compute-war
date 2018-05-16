#include "hash.h"

#define LOG_SEPARATOR "|"

void Hasher::HashBlock(uint8_t* dataPtr)
{
    uint8_t* sha256HashBuffer = (uint8_t*)malloc(kHashSizeBytesSHA);
    ASSERT(sha256HashBuffer != NULL);
    HashBlockSHA256(dataPtr, sha256HashBuffer);
    mSHA256Hashes.push(sha256HashBuffer);

    uint8_t* skeinHashBuffer = (uint8_t*)malloc(kHashSizeBytesSkein);
    ASSERT(skeinHashBuffer != NULL);
    HashBlockSkein256(dataPtr, skeinHashBuffer);
    mSkeinHashes.push(skeinHashBuffer);

    uint8_t* md5HashBuffer = (uint8_t*)malloc(kHashSizeBytesMD5);
    ASSERT(md5HashBuffer != NULL);
    HashBlockMD5(dataPtr, md5HashBuffer);
    mMD5Hashes.push(md5HashBuffer);
}

void Hasher::LogResults()
{
    LogResultsInternal(mSHA256Hashes, "SHA256");
    LogResultsInternal(mSkeinHashes, "Skein256");
    LogResultsInternal(mMD5Hashes, "MD5");
}

void Hasher::LogResultsInternal(queue<uint8_t*>& hashes, string alg)
{
    ASSERT_OP(kHashSizeBytesSkein, <=, kHashSizeBytesSHA);

    std::map<size_t, std::map<uint8_t, size_t>> resultMap;
    for (size_t byte = 0; byte < kHashSizeBytesSkein; byte++)
    {
        std::map<uint8_t, size_t> distributionMap;
        for (uint8_t bucket = 0; bucket < UINT8_MAX; bucket++)
        {
            distributionMap.insert({bucket, 0});
        }

        resultMap.insert({byte, std::move(distributionMap)});
    }

    while (!hashes.empty())
    {
        uint8_t* hashBuffer = hashes.front();
        hashes.pop();

        for (size_t byte = 0; byte < kHashSizeBytesSkein; byte++)
        {
            uint8_t bucket = hashBuffer[byte];
            resultMap[byte][bucket]++;
        }

        free(hashBuffer);
    }

    ASSERT(hashes.empty());

    double totalStdDev = 0;
    for (const auto& p: resultMap)
    {
        totalStdDev += LogByteResults(p.first, p.second, alg);
    }

    double avgStdDev = totalStdDev / kHashSizeBytesSkein;

    cout << "Average std dev: " << avgStdDev << endl;
}

double Hasher::LogByteResults(size_t byte, const std::map<uint8_t, size_t>& distributionMap, string alg)
{
    size_t total = 0;

    for (const auto& p: distributionMap)
    {
        total += p.second;
    }

    double avg = (double)total / UINT8_MAX;
    double stdDev = 0;

    for (const auto& p: distributionMap)
    {
        stdDev += pow(p.second - avg, 2);
    }

    stdDev /= (UINT8_MAX - 1);

    stdDev = sqrt(stdDev);

    cout << byte << LOG_SEPARATOR
         << alg << LOG_SEPARATOR
         << avg << LOG_SEPARATOR
         << stdDev << LOG_SEPARATOR
         << endl;

    return stdDev;
}