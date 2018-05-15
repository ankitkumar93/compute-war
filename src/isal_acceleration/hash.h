#ifndef __HASH_H__
#define __HASH_H__

#include "shared.h"

#include <isa-l_crypto.h>
#include <openssl/sha.h>
#include <openssl/md5.h>
// #include "skein/skein.h"

static constexpr int kHashSizeBytes = 32;
static constexpr int kHashSizeBits = 256;

// void HashBlockSkein256(uint8_t* dataPtr, uint64_t blockIndex);
void HashBlockSHA256(uint8_t* dataPtr, uint64_t blockIndex, string dataFile);
void HashBlockSHA256MB(uint8_t* dataPtr, uint64_t windowIndex, uint64_t windowSize, string dataFile);

#endif // __HASH_H__