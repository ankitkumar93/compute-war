#ifndef __COMPRESS_H__
#define __COMPRESS_H__

#include "shared.h"

// Compression libraries
#include <lz4.h>
#include <lzf.h>

static const int kCompBufferSize = LZ4_COMPRESSBOUND(kBlockSize);

void CompressBlockLZF(uint8_t* dataPtr, uint64_t blockIndex);
void CompressBlockLZ4(uint8_t* dataPtr, uint64_t blockIndex);
void CompressBlockLZ4Fast(uint8_t* dataPtr, uint64_t blockIndex);

#endif // __COMPRESS_H__