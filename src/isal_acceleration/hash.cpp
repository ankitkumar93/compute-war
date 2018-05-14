#include "hash.h"

#define LOG_SEPARATOR "|"

void HashBlockSHA256(uint8_t* dataPtr, uint64_t blockIndex)
{
    // Allocate memory
    uint8_t* hashBuffer = (uint8_t*)malloc(kHashSizeBytes);

    auto startTime = chrono::high_resolution_clock::now();
    SHA256(dataPtr, kBlockSize, hashBuffer);
    auto endTime = chrono::high_resolution_clock::now();

    uint64_t timeElapsedUS = chrono::duration_cast<chrono::microseconds>(endTime - startTime).count();
    cout << blockIndex << LOG_SEPARATOR
         << "Sha256" << LOG_SEPARATOR
         << timeElapsedUS << LOG_SEPARATOR
         << endl;

    free(hashBuffer);
}

void HashBlockMD5(uint8_t* dataPtr, uint64_t blockIndex)
{
    // Allocate memory
    uint8_t* hashBuffer = (uint8_t*)malloc(kHashSizeBytes);

    auto startTime = chrono::high_resolution_clock::now();
    MD5(dataPtr, kBlockSize, hashBuffer);
    auto endTime = chrono::high_resolution_clock::now();

    uint64_t timeElapsedUS = chrono::duration_cast<chrono::microseconds>(endTime - startTime).count();
    cout << blockIndex << LOG_SEPARATOR
         << "MD5" << LOG_SEPARATOR
         << timeElapsedUS << LOG_SEPARATOR
         << endl;

    free(hashBuffer);
}

void HashBlockSHA256MB(uint8_t* dataPtr, uint64_t windowIndex, uint64_t windowSize)
{
    // Allocate memory for hashes
    uint8_t* hashBuffer = (uint8_t*)malloc(kHashSizeBytes * windowSize);

    // Ctx
    SHA256_HASH_CTX_MGR* mgr = NULL;
    posix_memalign((void**)&mgr, 16, sizeof(SHA256_HASH_CTX_MGR));
    sha256_ctx_mgr_init(mgr);
    SHA256_HASH_CTX ctxpool;
    hash_ctx_init(&ctxpool);

    auto startTime = chrono::high_resolution_clock::now();
    for (uint64_t blockIndex = 0; blockIndex < windowSize; blockIndex++)
    {
        sha256_ctx_mgr_submit(mgr, &ctxpool, (char *)dataPtr + (blockIndex * kHashSizeBytes), kBlockSize, HASH_ENTIRE);
    }

    // Compute hash digest
    while (sha256_ctx_mgr_flush(mgr) != NULL);

    auto endTime = chrono::high_resolution_clock::now();

    uint64_t timeElapsedUS = chrono::duration_cast<chrono::microseconds>(endTime - startTime).count();
    cout << windowIndex << LOG_SEPARATOR
         << "Sha256MB" << LOG_SEPARATOR
         << timeElapsedUS << LOG_SEPARATOR
         << windowSize << LOG_SEPARATOR
         << endl;

    free(mgr);
    free(hashBuffer);
}

void HashBlockMD5MB(uint8_t* dataPtr, uint64_t windowIndex, uint64_t windowSize)
{
    // Allocate memory for hashes
    uint8_t* hashBuffer = (uint8_t*)malloc(kHashSizeBytes * windowSize);

    // Ctx
    MD5_HASH_CTX_MGR* mgr = NULL;
    posix_memalign((void**)&mgr, 16, sizeof(MD5_HASH_CTX_MGR));
    md5_ctx_mgr_init(mgr);
    MD5_HASH_CTX ctxpool;
    hash_ctx_init(&ctxpool);

    auto startTime = chrono::high_resolution_clock::now();
    for (uint64_t blockIndex = 0; blockIndex < windowSize; blockIndex++)
    {
        md5_ctx_mgr_submit(mgr, &ctxpool, (char *)dataPtr + (blockIndex * kHashSizeBytes), kBlockSize, HASH_ENTIRE);
    }

    // Compute hash digest
    while (md5_ctx_mgr_flush(mgr) != NULL);

    auto endTime = chrono::high_resolution_clock::now();

    uint64_t timeElapsedUS = chrono::duration_cast<chrono::microseconds>(endTime - startTime).count();
    cout << windowIndex << LOG_SEPARATOR
         << "MD5MB" << LOG_SEPARATOR
         << timeElapsedUS << LOG_SEPARATOR
         << windowSize << LOG_SEPARATOR
         << endl;

    free(mgr);
    free(hashBuffer);
}

void HashBlockMD5MBAVX(uint8_t* dataPtr, uint64_t windowIndex, uint64_t windowSize)
{
    // Allocate memory for hashes
    uint8_t* hashBuffer = (uint8_t*)malloc(kHashSizeBytes * windowSize);

    // Ctx
    MD5_HASH_CTX_MGR* mgr = NULL;
    posix_memalign((void**)&mgr, 16, sizeof(MD5_HASH_CTX_MGR));
    md5_ctx_mgr_init(mgr);
    MD5_HASH_CTX ctxpool;
    hash_ctx_init(&ctxpool);

    auto startTime = chrono::high_resolution_clock::now();
    for (uint64_t blockIndex = 0; blockIndex < windowSize; blockIndex++)
    {
        md5_ctx_mgr_submit(mgr, &ctxpool, (char *)dataPtr + (blockIndex * kHashSizeBytes), kBlockSize, HASH_ENTIRE);
    }

    // Compute hash digest
    while (md5_ctx_mgr_flush(mgr) != NULL);

    auto endTime = chrono::high_resolution_clock::now();

    uint64_t timeElapsedUS = chrono::duration_cast<chrono::microseconds>(endTime - startTime).count();
    cout << windowIndex << LOG_SEPARATOR
         << "MD5MB" << LOG_SEPARATOR
         << timeElapsedUS << LOG_SEPARATOR
         << windowSize << LOG_SEPARATOR
         << endl;

    free(mgr);
    free(hashBuffer);
}