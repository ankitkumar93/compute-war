#include "compress.h"

void LogCompressionResult(uint64_t blockIndex,
                          CompressionAlgorithmType alg,
                          double compRatio,
                          uint64_t timeElapsedNSCompression,
                          uint64_t timeElapsedNSDecompression)
{
    cout << blockIndex
         << ","
         << AlgToString(alg)
         << ","
         << compRatio
         << ","
         << timeElapsedNSCompression
         << ","
         << timeElapsedNSDecompression
         << endl;
}

// void CompressBlockLZF(uint8_t* dataPtr, uint64_t blockIndex)
// {
//     // Allocate memory for compression and decompression
//     char* compData = (char*)malloc(kBlockSize);
//     ASSERT(compData != NULL);

//     char* decompData = (char*)malloc(kBlockSize);
//     ASSERT(decompData != NULL);

//     // Compress
//     double compRatio = 0.0;
//     uint64_t compressedBlockSize = 0;
//     uint64_t timeElapsedNSComp = 0;
//     {
//         auto startTime = chrono::high_resolution_clock::now();
//         compressedBlockSize = lzf_compress((char*)dataPtr,
//                                            kBlockSize,
//                                            compData,
//                                            kBlockSize);
//         auto endTime = chrono::high_resolution_clock::now();
//         timeElapsedNSComp = chrono::duration_cast<chrono::nanoseconds>(endTime - startTime).count();

//         ASSERT_OP(compressedBlockSize, !=, 0);
//         compRatio = kBlockSize / compressedBlockSize;
//     }

//     ASSERT_OP(compRatio, !=, 0.0);

//     // Decompress
//     uint64_t timeElapsedNSDecomp = 0;
//     {
//         auto startTime = chrono::high_resolution_clock::now();
//         compressedBlockSize = lzf_decompress(compData,
//                                              compressedBlockSize,
//                                              decompData,
//                                              kBlockSize);
//         auto endTime = chrono::high_resolution_clock::now();
//         timeElapsedNSDecomp = chrono::duration_cast<chrono::nanoseconds>(endTime - startTime).count();
//     }

//     if (memcmp((void*)dataPtr, (void*)decompData, kBlockSize) != 0)
//     {
//         REPORT_ERR("Incorrect compression!");
//     }
//     else
//     {
//         LogCompressionResult(blockIndex, CompressionAlgorithmType::LZ4, compRatio, timeElapsedNSComp, timeElapsedNSDecomp);
//     }

//     free(compData);
//     free(decompData);
// }

void CompressBlockLZ4(uint8_t* dataPtr, uint64_t blockIndex)
{
    // Allocate memory for compression and decompression
    char* compData = (char*)malloc(kCompBufferSize);
    ASSERT(compData != NULL);

    char* decompData = (char*)malloc(kBlockSize);
    ASSERT(decompData != NULL);

    // Compress
    double compRatio = 0.0;
    uint64_t compressedBlockSize = 0;
    uint64_t timeElapsedNSComp = 0;
    {
        auto startTime = chrono::high_resolution_clock::now();
        compressedBlockSize = LZ4_compress_default((char*)dataPtr,
                                                   compData,
                                                   static_cast<int>(kBlockSize),
                                                   kCompBufferSize);
        auto endTime = chrono::high_resolution_clock::now();
        timeElapsedNSComp = chrono::duration_cast<chrono::nanoseconds>(endTime - startTime).count();

        ASSERT_OP(compressedBlockSize, !=, 0);
        compRatio = kBlockSize / compressedBlockSize;
    }

    ASSERT_OP(compRatio, !=, 0.0);

    // Decompress
    uint64_t timeElapsedNSDecomp = 0;
    {
        auto startTime = chrono::high_resolution_clock::now();
        compressedBlockSize = LZ4_decompress_safe(compData,
                                                  decompData,
                                                  compressedBlockSize,
                                                  kBlockSize);
        auto endTime = chrono::high_resolution_clock::now();
        timeElapsedNSDecomp = chrono::duration_cast<chrono::nanoseconds>(endTime - startTime).count();
    }

    LogCompressionResult(blockIndex, CompressionAlgorithmType::LZ4, compRatio, timeElapsedNSComp, timeElapsedNSDecomp);

    free(compData);
    free(decompData);
}

void CompressBlockLZ4Fast(uint8_t* dataPtr, uint64_t blockIndex)
{
    // Allocate memory for compression and decompression
    char* compData = (char*)malloc(kCompBufferSize);
    ASSERT(compData != NULL);

    char* decompData = (char*)malloc(kBlockSize);
    ASSERT(decompData != NULL);

    // Compress
    double compRatio = 0.0;
    uint64_t compressedBlockSize = 0;
    uint64_t timeElapsedNSComp = 0;
    {
        auto startTime = chrono::high_resolution_clock::now();
        compressedBlockSize = LZ4_compress_fast((char*)dataPtr,
                                                compData,
                                                static_cast<int>(kBlockSize),
                                                kCompBufferSize,
                                                2);
        auto endTime = chrono::high_resolution_clock::now();
        timeElapsedNSComp = chrono::duration_cast<chrono::nanoseconds>(endTime - startTime).count();

        ASSERT_OP(compressedBlockSize, !=, 0);
        compRatio = kBlockSize / compressedBlockSize;
    }

    ASSERT_OP(compRatio, !=, 0.0);

    // Decompress
    uint64_t timeElapsedNSDecomp = 0;
    {
        auto startTime = chrono::high_resolution_clock::now();
        compressedBlockSize = LZ4_decompress_fast(compData,
                                                  decompData,
                                                  kBlockSize);
        auto endTime = chrono::high_resolution_clock::now();
        timeElapsedNSDecomp = chrono::duration_cast<chrono::nanoseconds>(endTime - startTime).count();
    }
    
    LogCompressionResult(blockIndex, CompressionAlgorithmType::LZ4Fast, compRatio, timeElapsedNSComp, timeElapsedNSDecomp);

    free(compData);
    free(decompData);
}

void CompressBlockLZO(uint8_t* dataPtr, uint64_t blockIndex)
{
    // Allocate memory for compression and decompression
    char* compData = (char*)malloc(kBlockSize);
    ASSERT(compData != NULL);

    char* decompData = (char*)malloc(kBlockSize);
    ASSERT(decompData != NULL);

    // Compress
    double compRatio = 0.0;
    uint64_t compressedBlockSize = 0;
    uint64_t timeElapsedNSComp = 0;
    {
        auto startTime = chrono::high_resolution_clock::now();
        compressedBlockSize = lzf_compress((char*)dataPtr,
                                           kBlockSize,
                                           compData,
                                           kBlockSize);
        auto endTime = chrono::high_resolution_clock::now();
        timeElapsedNSComp = chrono::duration_cast<chrono::nanoseconds>(endTime - startTime).count();

        ASSERT_OP(compressedBlockSize, !=, 0);
        compRatio = kBlockSize / compressedBlockSize;
    }

    ASSERT_OP(compRatio, !=, 0.0);

    // Decompress
    uint64_t timeElapsedNSDecomp = 0;
    {
        auto startTime = chrono::high_resolution_clock::now();
        compressedBlockSize = lzf_decompress(compData,
                                             compressedBlockSize,
                                             decompData,
                                             kBlockSize);
        auto endTime = chrono::high_resolution_clock::now();
        timeElapsedNSDecomp = chrono::duration_cast<chrono::nanoseconds>(endTime - startTime).count();
    }

    if (memcmp((void*)dataPtr, (void*)decompData, kBlockSize) != 0)
    {
        REPORT_ERR("Incorrect compression!");
    }
    else
    {
        LogCompressionResult(blockIndex, CompressionAlgorithmType::LZ4, compRatio, timeElapsedNSComp, timeElapsedNSDecomp);
    }

    free(compData);
    free(decompData);
}