#include "hash.h"

#define LOG_SEPARATOR "|"

void HashBlockSkein256(uint8_t* dataPtr, uint64_t blockIndex, string dataFile)
{
    // Allocate memory
    uint8_t* hashBuffer = (uint8_t*)malloc(kHashSizeBytes);
    ASSERT(hashBuffer != NULL);

    auto startTime = chrono::high_resolution_clock::now();
    Skein_256_Ctxt_t ctx;
    Skein_256_Init(&ctx, kHashSizeBits);
    Skein_256_Update(&ctx, dataPtr, kBlockSize);
    Skein_256_Final(&ctx, hashBuffer);
    auto endTime = chrono::high_resolution_clock::now();

    uint64_t timeElapsedUS = chrono::duration_cast<chrono::microseconds>(endTime - startTime).count();
    cout << dataFile << LOG_SEPARATOR
         << blockIndex << LOG_SEPARATOR
         << "Skein256" << LOG_SEPARATOR
         << timeElapsedUS << LOG_SEPARATOR
         << endl;

    free(hashBuffer);
}

void HashBlockSHA256(uint8_t* dataPtr, uint64_t blockIndex, string dataFile)
{
    // Allocate memory
    uint8_t* hashBuffer = (uint8_t*)malloc(kHashSizeBytes);
    ASSERT(hashBuffer != NULL);

    auto startTime = chrono::high_resolution_clock::now();
    SHA256(dataPtr, kBlockSize, hashBuffer);
    auto endTime = chrono::high_resolution_clock::now();

    uint64_t timeElapsedUS = chrono::duration_cast<chrono::microseconds>(endTime - startTime).count();
    cout << dataFile << LOG_SEPARATOR
         << blockIndex << LOG_SEPARATOR
         << "Sha256" << LOG_SEPARATOR
         << timeElapsedUS << LOG_SEPARATOR
         << endl;

    free(hashBuffer);
}

void HashBlockSHA256MB(uint8_t* dataPtr, uint64_t windowIndex, uint64_t windowSize, string dataFile)
{
    // Ctx
    SHA256_HASH_CTX_MGR* mgr = NULL;
    posix_memalign((void**)&mgr, 16, sizeof(SHA256_HASH_CTX_MGR));
    sha256_ctx_mgr_init(mgr);
    SHA256_HASH_CTX ctxpool;
    hash_ctx_init(&ctxpool);

    auto startTime = chrono::high_resolution_clock::now();
    for (uint64_t offset = 0; offset < kBlockSize * windowSize; offset += kBlockSize)
    {
        sha256_ctx_mgr_submit(mgr, &ctxpool, (char *)(dataPtr + offset), kBlockSize, HASH_ENTIRE);
    }

    // Compute hash digest
    while (sha256_ctx_mgr_flush(mgr) != NULL);

    auto endTime = chrono::high_resolution_clock::now();

    uint64_t timeElapsedUS = chrono::duration_cast<chrono::microseconds>(endTime - startTime).count();
    cout << dataFile << LOG_SEPARATOR
         << windowIndex << LOG_SEPARATOR
         << "Sha256MB" << LOG_SEPARATOR
         << timeElapsedUS << LOG_SEPARATOR
         << windowSize << LOG_SEPARATOR
         << endl;

    free(mgr);
}