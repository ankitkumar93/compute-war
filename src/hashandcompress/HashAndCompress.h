/*
 * Copyright 2018 NetApp, Inc. All rights reserved.
 */

#ifndef _HASHANDCOMPRESS_H_
#define _HASHANDCOMPRESS_H_

//
// An offload value of false indicates that each worker thread should do its own hashing inline with
// compression. A value of true indicates that hashing should be handed off to a separate thread.
//
#define DEFAULT_OFFLOAD	false

//
// How many blocks should we hash at once?
//
#define DEFAULT_HASH_BLOCKS 1

//
// How many blocks should we read at a time?
//
#define DEFAULT_BLOCKS_PER_READ 8

//
// How many threads should be in the compression worker pool?
//
#define DEFAULT_THREADS	8

//
// What algorithms should we use?
//
#define DEFAULT_COMPRESSION_ALG "lz4"
#define DEFAULT_HASHING_ALG "skein"

#define LOG_SEPARATOR "|"

#endif /* _HASHANDCOMPRESS_H_ */
