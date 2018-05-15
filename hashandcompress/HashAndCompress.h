/*
 * Copyright 2018 NetApp, Inc. All rights reserved.
 */

#ifndef _HASHANDCOMPRESS_H_
#define _HASHANDCOMPRESS_H_

//
// Why is this even tweakable? Don't mess with it.
//
#define DEFAULT_BLOCK_SIZE 4096

//
// An offload value of 0 indicates that each worker thread should do one-block-at-a-time hashing inline with
// compression. A value greater than 0 indicates a count for how many hashes at a time should be handed off to
// the bulk hashing routine.
//
#define DEFAULT_OFFLOAD	0

//
// How many threads should be in the compression worker pool?
//
#define DEFAULT_THREADS	8

//
// What algorithms should we use?
//
#define DEFAULT_COMPRESSION_ALG "lz4"
#define DEFAULT_HASHING_ALG "skein"

#endif /* _HASHANDCOMPRESS_H_ */
