/***********************************************************************
**
** Generate Skein rotation constant candidate sets and test them.
**
** Source code author: Doug Whiting, 2008.
**
** This algorithm and source code is released to the public domain.
**
************************************************************************/

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
#include <ctype.h>
#include <math.h>
#include <assert.h>
#include "brg_types.h"                  /* get Brian Gladman's platform-specific definitions */

#define uint    unsigned int
#define u08b    uint_8t
#define u32b    uint_32t
#define u64b    uint_64t
#define conStr  const char *

/* Threefish algorithm parameters */
#ifndef BITS_PER_WORD
#define BITS_PER_WORD           (64)    /* number of bits in each word of a Threefish block */
#endif

#define ROUNDS_PER_CYCLE         (8)    /* when do we inject keys and start reusing rotation constants? */
#define MAX_BITS_PER_BLK      (1024)

#define MAX_WORDS_PER_BLK       (MAX_BITS_PER_BLK/BITS_PER_WORD) 
#define MAX_ROTS_PER_CYCLE      (MAX_WORDS_PER_BLK*(ROUNDS_PER_CYCLE/2))  

/* default search parameters for different block sizes */
#define DEFAULT_ROT_CNT_4    (10000)
#define DEFAULT_ROUND_CNT_4     ( 8)
#define MIN_HW_OR_4             (57)
#define MAX_SAT_ROUNDS_4        ( 9)

#define DEFAULT_ROT_CNT_8    (15000)
#define DEFAULT_ROUND_CNT_8     ( 8)
#define MIN_HW_OR_8             (43)
#define MAX_SAT_ROUNDS_8        (10)

#define DEFAULT_ROT_CNT_16    (3000)    /* the 1024-bit search is slower, so search for fewer iterations :-( */
#define DEFAULT_ROUND_CNT_16    ( 9)
#define MIN_HW_OR_16            (47)
#define MAX_SAT_ROUNDS_16       (11)

#define MAX_ROT_VER_CNT         ( 4)
#define MAX_ROT_VER_MASK        ((1 << MAX_ROT_VER_CNT ) - 1)

#define MAX_BEST_CNT            (16)

#if     BITS_PER_WORD == 64
typedef u64b    Word;
#elif   BITS_PER_WORD == 32
typedef u32b    Word;
#else
#error  "Invalid BITS_PER_WORD"
#endif

#define  left_rot(a,b) (((a) << (b)) | ((a) >> (BITS_PER_WORD - (b))))
#define right_rot(a,b) (((a) >> (b)) | ((a) << (BITS_PER_WORD - (b))))

/* flag bits for CheckDifferentials() */
#define CHK_FLG_DO_RAND     (1u << 0)
#define CHK_FLG_SHOW_HIST   (1u << 1)
#define CHK_FLG_VERBOSE     (1u << 2)
#define CHK_FLG_STDERR      (1u << 3)
#define CHK_FLG_QUICK_EXIT  (1u << 4)
#define CHK_FLG_NO_HDR      (1u << 5)

/* parameters for ShowSearchRec */
#define SHOW_ROTS_FINAL     (10000)     /* add "offset" to this one */
#define SHOW_ROTS_H         (3)
#define SHOW_ROTS_PRELIM    (2)
#define SHOW_ROTS           (1)
#define NO_ROTS             (0)

typedef struct { Word x[MAX_WORDS_PER_BLK]; } Block;

typedef void cycle_func(Word *b, const u08b *rotates, int rounds);

typedef struct                          /* record for dealing with rotation searches */
    {
    u08b rotList[MAX_ROTS_PER_CYCLE];   /* rotation constants */
    uint bitsPerBlock;                  /* Skein variant (256/512/1024) */
    uint rounds;                        /* how many rounds */
    uint diffBits;                      /* number of bits in differential pattern */
    uint CRC;                           /* CRC of rotates[] -- use as a quick "ID" */
    uint hw_OR[MAX_ROT_VER_CNT];        /* hamming weights, using OR */
    uint rWorst;                        /* "worst" min bit-to-bit differential */
    uint rotNum,rotScale;               /* where this came from */
    uint sampleCnt;                     /* how many differentials were taken */
    uint gotHdr;                        /* cosmetics */
    } rSearchRec;

typedef struct                          /* pass a bunch of parameters to RunSearch */
    {
    uint    chkFlags;
    uint    rounds;
    uint    minHW_or;
    uint    minOffs;
    uint    diffBits;
    uint    rScaleMax;
    uint    rotCntMax;
    uint    sampleCnt;
    uint    maxSatRnds;
    uint    seed0;
    } testParms;

/* globals */
cycle_func *fwd_cycle       =   NULL;
cycle_func *rev_cycle       =   NULL;
cycle_func *fwd_cycle_or    =   NULL;   /* slow but steady */
cycle_func *rev_cycle_or    =   NULL;
cycle_func *fwd_cycle_or_rN =   NULL;   /* optimized for the current # rounds (for speed) */
cycle_func *rev_cycle_or_rN =   NULL;
const char *rotFileName     =   NULL;   /* read from file instead of generate random? */
uint        rotVerMask      =    0xF;   /* mask of which versions to run */
uint        bitsPerBlock    =      0;   /* default is to do all */
uint        rotsPerCycle;
uint        wordsPerBlock;
uint        dupRotMask      =      0;   /* zero --> allow dup rots within the same round */
u64b        goodRotCntMask  =      0;   /* which rotation values are ok? (must be set first!) */

#define RotCntGood(rotCnt) (((goodRotCntMask >> (rotCnt)) & 1) != 0)
#define RotCnt_Bad(rotCnt) (((goodRotCntMask >> (rotCnt)) & 1) == 0)

/********************** use RC4 to generate test data ******************/
/* Note: this works identically on all platforms (big/little-endian)   */
static struct
    {
    uint I,J;                           /* RC4 vars */
    u08b state[256];
    } prng;

void RandBytes(void *dst,uint byteCnt)
    {
    u08b a,b;
    u08b *d = (u08b *) dst;

    for (;byteCnt;byteCnt--,d++)        /* run RC4  */
        {
        prng.I  = (prng.I+1) & 0xFF;
        a       =  prng.state[prng.I];
        prng.J  = (prng.J+a) & 0xFF;
        b       =  prng.state[prng.J];
        prng.state[prng.I] = b;
        prng.state[prng.J] = a;
        *d      =  prng.state[(a+b) & 0xFF];
        }
    }

/* get a pseudo-random 32-bit integer in a portable way */
uint Rand32(void)
    {
    uint i,n;
    u08b tmp[sizeof(uint)];

    RandBytes(tmp,sizeof(tmp));

    for (i=n=0;i<sizeof(tmp);i++)
        n = n*256 + tmp[i];
    
    return n;
    }

/* get a pseudo-random 64-bit integer in a portable way */
u64b Rand64(void)
    {
    uint i;
    u64b n;
    u08b tmp[sizeof(u64b)];

    RandBytes(tmp,sizeof(tmp));

    n=0;
    for (i=0;i<sizeof(tmp);i++)
        n = n*256 + tmp[i];
    
    return n;
    }

/* init the (RC4-based) prng */
void Rand_Init(u64b seed)
    {
    uint i,j;
    u08b tmp[4*256];

    /* init the "key" in an endian-independent fashion */
    for (i=0;i<8;i++)
        tmp[i] = (u08b) (seed >> (8*i));

    /* initialize the permutation */
    for (i=0;i<256;i++)
        prng.state[i]=(u08b) i;

    /* now run the RC4 key schedule */
    for (i=j=0;i<256;i++)
        {                   
        j = (j + prng.state[i] + tmp[i%8]) & 0xFF;
        tmp[256]      = prng.state[i];
        prng.state[i] = prng.state[j];
        prng.state[j] = tmp[256];
        }
    prng.I = prng.J = 0;  /* init I,J variables for RC4 */
    
    /* discard some initial RC4 keystream before returning */
    RandBytes(tmp,sizeof(tmp));
    }

/* implementations of Skein round functions for various block sizes */
void fwd_cycle_16(Word *b, const u08b *rotates, int rounds)
    {
    for (;rounds > 0;rounds -=8)
        {
        b[ 0] += b[ 1]; b[ 1] = left_rot(b[ 1], rotates[ 0]); b[ 1] ^= b[ 0];
        b[ 2] += b[ 3]; b[ 3] = left_rot(b[ 3], rotates[ 1]); b[ 3] ^= b[ 2];
        b[ 4] += b[ 5]; b[ 5] = left_rot(b[ 5], rotates[ 2]); b[ 5] ^= b[ 4];
        b[ 6] += b[ 7]; b[ 7] = left_rot(b[ 7], rotates[ 3]); b[ 7] ^= b[ 6];
        b[ 8] += b[ 9]; b[ 9] = left_rot(b[ 9], rotates[ 4]); b[ 9] ^= b[ 8];
        b[10] += b[11]; b[11] = left_rot(b[11], rotates[ 5]); b[11] ^= b[10];
        b[12] += b[13]; b[13] = left_rot(b[13], rotates[ 6]); b[13] ^= b[12];
        b[14] += b[15]; b[15] = left_rot(b[15], rotates[ 7]); b[15] ^= b[14];
        if (rounds == 1) break;                           
                                                          
        b[ 0] += b[ 9]; b[ 9] = left_rot(b[ 9], rotates[ 8]); b[ 9] ^= b[ 0];
        b[ 2] += b[13]; b[13] = left_rot(b[13], rotates[ 9]); b[13] ^= b[ 2];
        b[ 6] += b[11]; b[11] = left_rot(b[11], rotates[10]); b[11] ^= b[ 6];
        b[ 4] += b[15]; b[15] = left_rot(b[15], rotates[11]); b[15] ^= b[ 4];
        b[10] += b[ 7]; b[ 7] = left_rot(b[ 7], rotates[12]); b[ 7] ^= b[10];
        b[12] += b[ 3]; b[ 3] = left_rot(b[ 3], rotates[13]); b[ 3] ^= b[12];
        b[14] += b[ 5]; b[ 5] = left_rot(b[ 5], rotates[14]); b[ 5] ^= b[14];
        b[ 8] += b[ 1]; b[ 1] = left_rot(b[ 1], rotates[15]); b[ 1] ^= b[ 8];
        if (rounds == 2) break;                           
                                                          
        b[ 0] += b[ 7]; b[ 7] = left_rot(b[ 7], rotates[16]); b[ 7] ^= b[ 0];
        b[ 2] += b[ 5]; b[ 5] = left_rot(b[ 5], rotates[17]); b[ 5] ^= b[ 2];
        b[ 4] += b[ 3]; b[ 3] = left_rot(b[ 3], rotates[18]); b[ 3] ^= b[ 4];
        b[ 6] += b[ 1]; b[ 1] = left_rot(b[ 1], rotates[19]); b[ 1] ^= b[ 6];
        b[12] += b[15]; b[15] = left_rot(b[15], rotates[20]); b[15] ^= b[12];
        b[14] += b[13]; b[13] = left_rot(b[13], rotates[21]); b[13] ^= b[14];
        b[ 8] += b[11]; b[11] = left_rot(b[11], rotates[22]); b[11] ^= b[ 8];
        b[10] += b[ 9]; b[ 9] = left_rot(b[ 9], rotates[23]); b[ 9] ^= b[10];
        if (rounds == 3) break;                           
                                                          
        b[ 0] += b[15]; b[15] = left_rot(b[15], rotates[24]); b[15] ^= b[ 0];
        b[ 2] += b[11]; b[11] = left_rot(b[11], rotates[25]); b[11] ^= b[ 2];
        b[ 6] += b[13]; b[13] = left_rot(b[13], rotates[26]); b[13] ^= b[ 6];
        b[ 4] += b[ 9]; b[ 9] = left_rot(b[ 9], rotates[27]); b[ 9] ^= b[ 4];
        b[14] += b[ 1]; b[ 1] = left_rot(b[ 1], rotates[28]); b[ 1] ^= b[14];
        b[ 8] += b[ 5]; b[ 5] = left_rot(b[ 5], rotates[29]); b[ 5] ^= b[ 8];
        b[10] += b[ 3]; b[ 3] = left_rot(b[ 3], rotates[30]); b[ 3] ^= b[10];
        b[12] += b[ 7]; b[ 7] = left_rot(b[ 7], rotates[31]); b[ 7] ^= b[12];
        if (rounds == 4) break;                           
                                                          
        b[ 0] += b[ 1]; b[ 1] = left_rot(b[ 1], rotates[32]); b[ 1] ^= b[ 0];
        b[ 2] += b[ 3]; b[ 3] = left_rot(b[ 3], rotates[33]); b[ 3] ^= b[ 2];
        b[ 4] += b[ 5]; b[ 5] = left_rot(b[ 5], rotates[34]); b[ 5] ^= b[ 4];
        b[ 6] += b[ 7]; b[ 7] = left_rot(b[ 7], rotates[35]); b[ 7] ^= b[ 6];
        b[ 8] += b[ 9]; b[ 9] = left_rot(b[ 9], rotates[36]); b[ 9] ^= b[ 8];
        b[10] += b[11]; b[11] = left_rot(b[11], rotates[37]); b[11] ^= b[10];
        b[12] += b[13]; b[13] = left_rot(b[13], rotates[38]); b[13] ^= b[12];
        b[14] += b[15]; b[15] = left_rot(b[15], rotates[39]); b[15] ^= b[14];
        if (rounds == 5) break;                           
                                                          
        b[ 0] += b[ 9]; b[ 9] = left_rot(b[ 9], rotates[40]); b[ 9] ^= b[ 0];
        b[ 2] += b[13]; b[13] = left_rot(b[13], rotates[41]); b[13] ^= b[ 2];
        b[ 6] += b[11]; b[11] = left_rot(b[11], rotates[42]); b[11] ^= b[ 6];
        b[ 4] += b[15]; b[15] = left_rot(b[15], rotates[43]); b[15] ^= b[ 4];
        b[10] += b[ 7]; b[ 7] = left_rot(b[ 7], rotates[44]); b[ 7] ^= b[10];
        b[12] += b[ 3]; b[ 3] = left_rot(b[ 3], rotates[45]); b[ 3] ^= b[12];
        b[14] += b[ 5]; b[ 5] = left_rot(b[ 5], rotates[46]); b[ 5] ^= b[14];
        b[ 8] += b[ 1]; b[ 1] = left_rot(b[ 1], rotates[47]); b[ 1] ^= b[ 8];
        if (rounds == 6) break;                           
                                                          
        b[ 0] += b[ 7]; b[ 7] = left_rot(b[ 7], rotates[48]); b[ 7] ^= b[ 0];
        b[ 2] += b[ 5]; b[ 5] = left_rot(b[ 5], rotates[49]); b[ 5] ^= b[ 2];
        b[ 4] += b[ 3]; b[ 3] = left_rot(b[ 3], rotates[50]); b[ 3] ^= b[ 4];
        b[ 6] += b[ 1]; b[ 1] = left_rot(b[ 1], rotates[51]); b[ 1] ^= b[ 6];
        b[12] += b[15]; b[15] = left_rot(b[15], rotates[52]); b[15] ^= b[12];
        b[14] += b[13]; b[13] = left_rot(b[13], rotates[53]); b[13] ^= b[14];
        b[ 8] += b[11]; b[11] = left_rot(b[11], rotates[54]); b[11] ^= b[ 8];
        b[10] += b[ 9]; b[ 9] = left_rot(b[ 9], rotates[55]); b[ 9] ^= b[10];
        if (rounds == 7) break;                           
                                                          
        b[ 0] += b[15]; b[15] = left_rot(b[15], rotates[56]); b[15] ^= b[ 0];
        b[ 2] += b[11]; b[11] = left_rot(b[11], rotates[57]); b[11] ^= b[ 2];
        b[ 6] += b[13]; b[13] = left_rot(b[13], rotates[58]); b[13] ^= b[ 6];
        b[ 4] += b[ 9]; b[ 9] = left_rot(b[ 9], rotates[59]); b[ 9] ^= b[ 4];
        b[14] += b[ 1]; b[ 1] = left_rot(b[ 1], rotates[60]); b[ 1] ^= b[14];
        b[ 8] += b[ 5]; b[ 5] = left_rot(b[ 5], rotates[61]); b[ 5] ^= b[ 8];
        b[10] += b[ 3]; b[ 3] = left_rot(b[ 3], rotates[62]); b[ 3] ^= b[10];
        b[12] += b[ 7]; b[ 7] = left_rot(b[ 7], rotates[63]); b[ 7] ^= b[12];
        }
    }

void fwd_cycle_8(Word *b, const u08b *rotates, int rounds)
    {
    for (;rounds > 0;rounds -=8)
        {
        b[ 0] += b[ 1]; b[ 1] = left_rot(b[ 1], rotates[ 0]); b[ 1] ^= b[ 0];
        b[ 2] += b[ 3]; b[ 3] = left_rot(b[ 3], rotates[ 1]); b[ 3] ^= b[ 2];
        b[ 4] += b[ 5]; b[ 5] = left_rot(b[ 5], rotates[ 2]); b[ 5] ^= b[ 4];
        b[ 6] += b[ 7]; b[ 7] = left_rot(b[ 7], rotates[ 3]); b[ 7] ^= b[ 6];
        if (rounds == 1) break;

        b[ 2] += b[ 1]; b[ 1] = left_rot(b[ 1], rotates[ 4]); b[ 1] ^= b[ 2];
        b[ 4] += b[ 7]; b[ 7] = left_rot(b[ 7], rotates[ 5]); b[ 7] ^= b[ 4];
        b[ 6] += b[ 5]; b[ 5] = left_rot(b[ 5], rotates[ 6]); b[ 5] ^= b[ 6];
        b[ 0] += b[ 3]; b[ 3] = left_rot(b[ 3], rotates[ 7]); b[ 3] ^= b[ 0];
        if (rounds == 2) break;

        b[ 4] += b[ 1]; b[ 1] = left_rot(b[ 1], rotates[ 8]); b[ 1] ^= b[ 4];
        b[ 6] += b[ 3]; b[ 3] = left_rot(b[ 3], rotates[ 9]); b[ 3] ^= b[ 6];
        b[ 0] += b[ 5]; b[ 5] = left_rot(b[ 5], rotates[10]); b[ 5] ^= b[ 0];
        b[ 2] += b[ 7]; b[ 7] = left_rot(b[ 7], rotates[11]); b[ 7] ^= b[ 2];
        if (rounds == 3) break;

        b[ 6] += b[ 1]; b[ 1] = left_rot(b[ 1], rotates[12]); b[ 1] ^= b[ 6];
        b[ 0] += b[ 7]; b[ 7] = left_rot(b[ 7], rotates[13]); b[ 7] ^= b[ 0];
        b[ 2] += b[ 5]; b[ 5] = left_rot(b[ 5], rotates[14]); b[ 5] ^= b[ 2];
        b[ 4] += b[ 3]; b[ 3] = left_rot(b[ 3], rotates[15]); b[ 3] ^= b[ 4];
        if (rounds == 4) break;

        b[ 0] += b[ 1]; b[ 1] = left_rot(b[ 1], rotates[16]); b[ 1] ^= b[ 0];
        b[ 2] += b[ 3]; b[ 3] = left_rot(b[ 3], rotates[17]); b[ 3] ^= b[ 2];
        b[ 4] += b[ 5]; b[ 5] = left_rot(b[ 5], rotates[18]); b[ 5] ^= b[ 4];
        b[ 6] += b[ 7]; b[ 7] = left_rot(b[ 7], rotates[19]); b[ 7] ^= b[ 6];
        if (rounds == 5) break;

        b[ 2] += b[ 1]; b[ 1] = left_rot(b[ 1], rotates[20]); b[ 1] ^= b[ 2];
        b[ 4] += b[ 7]; b[ 7] = left_rot(b[ 7], rotates[21]); b[ 7] ^= b[ 4];
        b[ 6] += b[ 5]; b[ 5] = left_rot(b[ 5], rotates[22]); b[ 5] ^= b[ 6];
        b[ 0] += b[ 3]; b[ 3] = left_rot(b[ 3], rotates[23]); b[ 3] ^= b[ 0];
        if (rounds == 6) break;

        b[ 4] += b[ 1]; b[ 1] = left_rot(b[ 1], rotates[24]); b[ 1] ^= b[ 4];
        b[ 6] += b[ 3]; b[ 3] = left_rot(b[ 3], rotates[25]); b[ 3] ^= b[ 6];
        b[ 0] += b[ 5]; b[ 5] = left_rot(b[ 5], rotates[26]); b[ 5] ^= b[ 0];
        b[ 2] += b[ 7]; b[ 7] = left_rot(b[ 7], rotates[27]); b[ 7] ^= b[ 2];
        if (rounds == 7) break;

        b[ 6] += b[ 1]; b[ 1] = left_rot(b[ 1], rotates[28]); b[ 1] ^= b[ 6];
        b[ 0] += b[ 7]; b[ 7] = left_rot(b[ 7], rotates[29]); b[ 7] ^= b[ 0];
        b[ 2] += b[ 5]; b[ 5] = left_rot(b[ 5], rotates[30]); b[ 5] ^= b[ 2];
        b[ 4] += b[ 3]; b[ 3] = left_rot(b[ 3], rotates[31]); b[ 3] ^= b[ 4];
        }
    }

void fwd_cycle_4(Word *b, const u08b *rotates, int rounds)
    {
    for (;rounds > 0;rounds -=8)
        {
        b[ 0] += b[ 1]; b[ 1] = left_rot(b[ 1], rotates[ 0]); b[ 1] ^= b[ 0];
        b[ 2] += b[ 3]; b[ 3] = left_rot(b[ 3], rotates[ 1]); b[ 3] ^= b[ 2];
        if (rounds == 1) break;

        b[ 0] += b[ 3]; b[ 3] = left_rot(b[ 3], rotates[ 2]); b[ 3] ^= b[ 0];
        b[ 2] += b[ 1]; b[ 1] = left_rot(b[ 1], rotates[ 3]); b[ 1] ^= b[ 2];
        if (rounds == 2) break;

        b[ 0] += b[ 1]; b[ 1] = left_rot(b[ 1], rotates[ 4]); b[ 1] ^= b[ 0];
        b[ 2] += b[ 3]; b[ 3] = left_rot(b[ 3], rotates[ 5]); b[ 3] ^= b[ 2];
        if (rounds == 3) break;

        b[ 0] += b[ 3]; b[ 3] = left_rot(b[ 3], rotates[ 6]); b[ 3] ^= b[ 0];
        b[ 2] += b[ 1]; b[ 1] = left_rot(b[ 1], rotates[ 7]); b[ 1] ^= b[ 2];
        if (rounds == 4) break;

        b[ 0] += b[ 1]; b[ 1] = left_rot(b[ 1], rotates[ 8]); b[ 1] ^= b[ 0];
        b[ 2] += b[ 3]; b[ 3] = left_rot(b[ 3], rotates[ 9]); b[ 3] ^= b[ 2];
        if (rounds == 5) break;

        b[ 0] += b[ 3]; b[ 3] = left_rot(b[ 3], rotates[10]); b[ 3] ^= b[ 0];
        b[ 2] += b[ 1]; b[ 1] = left_rot(b[ 1], rotates[11]); b[ 1] ^= b[ 2];
        if (rounds == 6) break;

        b[ 0] += b[ 1]; b[ 1] = left_rot(b[ 1], rotates[12]); b[ 1] ^= b[ 0];
        b[ 2] += b[ 3]; b[ 3] = left_rot(b[ 3], rotates[13]); b[ 3] ^= b[ 2];
        if (rounds == 7) break;

        b[ 0] += b[ 3]; b[ 3] = left_rot(b[ 3], rotates[14]); b[ 3] ^= b[ 0];
        b[ 2] += b[ 1]; b[ 1] = left_rot(b[ 1], rotates[15]); b[ 1] ^= b[ 2];
        }
    }

/* reverse versions of the cipher */
void rev_cycle_16(Word *b, const u08b *rotates, int rounds)
    {
    for (;rounds > 0;rounds = (rounds-1) & ~7)
        {
        switch (rounds & 7)
            {
            case 0:
                    b[ 7] ^= b[12]; b[ 7] = right_rot(b[ 7], rotates[63]); b[12] -= b[ 7]; 
                    b[ 3] ^= b[10]; b[ 3] = right_rot(b[ 3], rotates[62]); b[10] -= b[ 3]; 
                    b[ 5] ^= b[ 8]; b[ 5] = right_rot(b[ 5], rotates[61]); b[ 8] -= b[ 5]; 
                    b[ 1] ^= b[14]; b[ 1] = right_rot(b[ 1], rotates[60]); b[14] -= b[ 1]; 
                    b[ 9] ^= b[ 4]; b[ 9] = right_rot(b[ 9], rotates[59]); b[ 4] -= b[ 9]; 
                    b[13] ^= b[ 6]; b[13] = right_rot(b[13], rotates[58]); b[ 6] -= b[13]; 
                    b[11] ^= b[ 2]; b[11] = right_rot(b[11], rotates[57]); b[ 2] -= b[11]; 
                    b[15] ^= b[ 0]; b[15] = right_rot(b[15], rotates[56]); b[ 0] -= b[15];
            case 7:                                                                       
                    b[ 9] ^= b[10]; b[ 9] = right_rot(b[ 9], rotates[55]); b[10] -= b[ 9];
                    b[11] ^= b[ 8]; b[11] = right_rot(b[11], rotates[54]); b[ 8] -= b[11];
                    b[13] ^= b[14]; b[13] = right_rot(b[13], rotates[53]); b[14] -= b[13];
                    b[15] ^= b[12]; b[15] = right_rot(b[15], rotates[52]); b[12] -= b[15];
                    b[ 1] ^= b[ 6]; b[ 1] = right_rot(b[ 1], rotates[51]); b[ 6] -= b[ 1];
                    b[ 3] ^= b[ 4]; b[ 3] = right_rot(b[ 3], rotates[50]); b[ 4] -= b[ 3];
                    b[ 5] ^= b[ 2]; b[ 5] = right_rot(b[ 5], rotates[49]); b[ 2] -= b[ 5];
                    b[ 7] ^= b[ 0]; b[ 7] = right_rot(b[ 7], rotates[48]); b[ 0] -= b[ 7];
            case 6:                                                                       
                    b[ 1] ^= b[ 8]; b[ 1] = right_rot(b[ 1], rotates[47]); b[ 8] -= b[ 1];
                    b[ 5] ^= b[14]; b[ 5] = right_rot(b[ 5], rotates[46]); b[14] -= b[ 5];
                    b[ 3] ^= b[12]; b[ 3] = right_rot(b[ 3], rotates[45]); b[12] -= b[ 3];
                    b[ 7] ^= b[10]; b[ 7] = right_rot(b[ 7], rotates[44]); b[10] -= b[ 7];
                    b[15] ^= b[ 4]; b[15] = right_rot(b[15], rotates[43]); b[ 4] -= b[15];
                    b[11] ^= b[ 6]; b[11] = right_rot(b[11], rotates[42]); b[ 6] -= b[11];
                    b[13] ^= b[ 2]; b[13] = right_rot(b[13], rotates[41]); b[ 2] -= b[13];
                    b[ 9] ^= b[ 0]; b[ 9] = right_rot(b[ 9], rotates[40]); b[ 0] -= b[ 9];
            case 5:                                                                       
                    b[15] ^= b[14]; b[15] = right_rot(b[15], rotates[39]); b[14] -= b[15];
                    b[13] ^= b[12]; b[13] = right_rot(b[13], rotates[38]); b[12] -= b[13];
                    b[11] ^= b[10]; b[11] = right_rot(b[11], rotates[37]); b[10] -= b[11];
                    b[ 9] ^= b[ 8]; b[ 9] = right_rot(b[ 9], rotates[36]); b[ 8] -= b[ 9];
                    b[ 7] ^= b[ 6]; b[ 7] = right_rot(b[ 7], rotates[35]); b[ 6] -= b[ 7];
                    b[ 5] ^= b[ 4]; b[ 5] = right_rot(b[ 5], rotates[34]); b[ 4] -= b[ 5];
                    b[ 3] ^= b[ 2]; b[ 3] = right_rot(b[ 3], rotates[33]); b[ 2] -= b[ 3];
                    b[ 1] ^= b[ 0]; b[ 1] = right_rot(b[ 1], rotates[32]); b[ 0] -= b[ 1];
            case 4:                                                                       
                    b[ 7] ^= b[12]; b[ 7] = right_rot(b[ 7], rotates[31]); b[12] -= b[ 7];
                    b[ 3] ^= b[10]; b[ 3] = right_rot(b[ 3], rotates[30]); b[10] -= b[ 3];
                    b[ 5] ^= b[ 8]; b[ 5] = right_rot(b[ 5], rotates[29]); b[ 8] -= b[ 5];
                    b[ 1] ^= b[14]; b[ 1] = right_rot(b[ 1], rotates[28]); b[14] -= b[ 1];
                    b[ 9] ^= b[ 4]; b[ 9] = right_rot(b[ 9], rotates[27]); b[ 4] -= b[ 9];
                    b[13] ^= b[ 6]; b[13] = right_rot(b[13], rotates[26]); b[ 6] -= b[13];
                    b[11] ^= b[ 2]; b[11] = right_rot(b[11], rotates[25]); b[ 2] -= b[11];
                    b[15] ^= b[ 0]; b[15] = right_rot(b[15], rotates[24]); b[ 0] -= b[15];
            case 3:                                                                       
                    b[ 9] ^= b[10]; b[ 9] = right_rot(b[ 9], rotates[23]); b[10] -= b[ 9];
                    b[11] ^= b[ 8]; b[11] = right_rot(b[11], rotates[22]); b[ 8] -= b[11];
                    b[13] ^= b[14]; b[13] = right_rot(b[13], rotates[21]); b[14] -= b[13];
                    b[15] ^= b[12]; b[15] = right_rot(b[15], rotates[20]); b[12] -= b[15];
                    b[ 1] ^= b[ 6]; b[ 1] = right_rot(b[ 1], rotates[19]); b[ 6] -= b[ 1];
                    b[ 3] ^= b[ 4]; b[ 3] = right_rot(b[ 3], rotates[18]); b[ 4] -= b[ 3];
                    b[ 5] ^= b[ 2]; b[ 5] = right_rot(b[ 5], rotates[17]); b[ 2] -= b[ 5];
                    b[ 7] ^= b[ 0]; b[ 7] = right_rot(b[ 7], rotates[16]); b[ 0] -= b[ 7];
            case 2:                                                                       
                    b[ 1] ^= b[ 8]; b[ 1] = right_rot(b[ 1], rotates[15]); b[ 8] -= b[ 1];
                    b[ 5] ^= b[14]; b[ 5] = right_rot(b[ 5], rotates[14]); b[14] -= b[ 5];
                    b[ 3] ^= b[12]; b[ 3] = right_rot(b[ 3], rotates[13]); b[12] -= b[ 3];
                    b[ 7] ^= b[10]; b[ 7] = right_rot(b[ 7], rotates[12]); b[10] -= b[ 7];
                    b[15] ^= b[ 4]; b[15] = right_rot(b[15], rotates[11]); b[ 4] -= b[15];
                    b[11] ^= b[ 6]; b[11] = right_rot(b[11], rotates[10]); b[ 6] -= b[11];
                    b[13] ^= b[ 2]; b[13] = right_rot(b[13], rotates[ 9]); b[ 2] -= b[13];
                    b[ 9] ^= b[ 0]; b[ 9] = right_rot(b[ 9], rotates[ 8]); b[ 0] -= b[ 9];
            case 1:                                                                       
                    b[15] ^= b[14]; b[15] = right_rot(b[15], rotates[ 7]); b[14] -= b[15];
                    b[13] ^= b[12]; b[13] = right_rot(b[13], rotates[ 6]); b[12] -= b[13];
                    b[11] ^= b[10]; b[11] = right_rot(b[11], rotates[ 5]); b[10] -= b[11];
                    b[ 9] ^= b[ 8]; b[ 9] = right_rot(b[ 9], rotates[ 4]); b[ 8] -= b[ 9];
                    b[ 7] ^= b[ 6]; b[ 7] = right_rot(b[ 7], rotates[ 3]); b[ 6] -= b[ 7];
                    b[ 5] ^= b[ 4]; b[ 5] = right_rot(b[ 5], rotates[ 2]); b[ 4] -= b[ 5];
                    b[ 3] ^= b[ 2]; b[ 3] = right_rot(b[ 3], rotates[ 1]); b[ 2] -= b[ 3];
                    b[ 1] ^= b[ 0]; b[ 1] = right_rot(b[ 1], rotates[ 0]); b[ 0] -= b[ 1];
            }                                                                             
                                                                                          
        }                                                                                 
    }                                                                                     
                                                                                          
void rev_cycle_8(Word *b, const u08b *rotates, int rounds)                                
    {                                                                                     
    for (;rounds > 0;rounds = (rounds-1) & ~7)                                            
        {                                                                                 
        switch (rounds & 7)                                                               
            {                                                                             
            case 0:                                                                       
                    b[ 3] ^= b[ 4]; b[ 3] = right_rot(b[ 3], rotates[31]); b[ 4] -= b[ 3];
                    b[ 5] ^= b[ 2]; b[ 5] = right_rot(b[ 5], rotates[30]); b[ 2] -= b[ 5];
                    b[ 7] ^= b[ 0]; b[ 7] = right_rot(b[ 7], rotates[29]); b[ 0] -= b[ 7];
                    b[ 1] ^= b[ 6]; b[ 1] = right_rot(b[ 1], rotates[28]); b[ 6] -= b[ 1];
            case 7:                                                                       
                    b[ 7] ^= b[ 2]; b[ 7] = right_rot(b[ 7], rotates[27]); b[ 2] -= b[ 7];
                    b[ 5] ^= b[ 0]; b[ 5] = right_rot(b[ 5], rotates[26]); b[ 0] -= b[ 5];
                    b[ 3] ^= b[ 6]; b[ 3] = right_rot(b[ 3], rotates[25]); b[ 6] -= b[ 3];
                    b[ 1] ^= b[ 4]; b[ 1] = right_rot(b[ 1], rotates[24]); b[ 4] -= b[ 1];
            case 6:                                                                       
                    b[ 3] ^= b[ 0]; b[ 3] = right_rot(b[ 3], rotates[23]); b[ 0] -= b[ 3];
                    b[ 5] ^= b[ 6]; b[ 5] = right_rot(b[ 5], rotates[22]); b[ 6] -= b[ 5];
                    b[ 7] ^= b[ 4]; b[ 7] = right_rot(b[ 7], rotates[21]); b[ 4] -= b[ 7];
                    b[ 1] ^= b[ 2]; b[ 1] = right_rot(b[ 1], rotates[20]); b[ 2] -= b[ 1];
            case 5:                                                                       
                    b[ 7] ^= b[ 6]; b[ 7] = right_rot(b[ 7], rotates[19]); b[ 6] -= b[ 7];
                    b[ 5] ^= b[ 4]; b[ 5] = right_rot(b[ 5], rotates[18]); b[ 4] -= b[ 5];
                    b[ 3] ^= b[ 2]; b[ 3] = right_rot(b[ 3], rotates[17]); b[ 2] -= b[ 3];
                    b[ 1] ^= b[ 0]; b[ 1] = right_rot(b[ 1], rotates[16]); b[ 0] -= b[ 1];
            case 4:                                                                       
                    b[ 3] ^= b[ 4]; b[ 3] = right_rot(b[ 3], rotates[15]); b[ 4] -= b[ 3];
                    b[ 5] ^= b[ 2]; b[ 5] = right_rot(b[ 5], rotates[14]); b[ 2] -= b[ 5];
                    b[ 7] ^= b[ 0]; b[ 7] = right_rot(b[ 7], rotates[13]); b[ 0] -= b[ 7];
                    b[ 1] ^= b[ 6]; b[ 1] = right_rot(b[ 1], rotates[12]); b[ 6] -= b[ 1];
            case 3:                                                                       
                    b[ 7] ^= b[ 2]; b[ 7] = right_rot(b[ 7], rotates[11]); b[ 2] -= b[ 7];
                    b[ 5] ^= b[ 0]; b[ 5] = right_rot(b[ 5], rotates[10]); b[ 0] -= b[ 5];
                    b[ 3] ^= b[ 6]; b[ 3] = right_rot(b[ 3], rotates[ 9]); b[ 6] -= b[ 3];
                    b[ 1] ^= b[ 4]; b[ 1] = right_rot(b[ 1], rotates[ 8]); b[ 4] -= b[ 1];
            case 2:                                                                       
                    b[ 3] ^= b[ 0]; b[ 3] = right_rot(b[ 3], rotates[ 7]); b[ 0] -= b[ 3];
                    b[ 5] ^= b[ 6]; b[ 5] = right_rot(b[ 5], rotates[ 6]); b[ 6] -= b[ 5];
                    b[ 7] ^= b[ 4]; b[ 7] = right_rot(b[ 7], rotates[ 5]); b[ 4] -= b[ 7];
                    b[ 1] ^= b[ 2]; b[ 1] = right_rot(b[ 1], rotates[ 4]); b[ 2] -= b[ 1];
            case 1:                                                                       
                    b[ 7] ^= b[ 6]; b[ 7] = right_rot(b[ 7], rotates[ 3]); b[ 6] -= b[ 7];
                    b[ 5] ^= b[ 4]; b[ 5] = right_rot(b[ 5], rotates[ 2]); b[ 4] -= b[ 5];
                    b[ 3] ^= b[ 2]; b[ 3] = right_rot(b[ 3], rotates[ 1]); b[ 2] -= b[ 3];
                    b[ 1] ^= b[ 0]; b[ 1] = right_rot(b[ 1], rotates[ 0]); b[ 0] -= b[ 1];
            }                                                                             
        }                                                                                 
    }                                                                                     
                                                                                          
void rev_cycle_4(Word *b, const u08b *rotates, int rounds)                                
    {                                                                                     
    for (;rounds > 0;rounds = (rounds-1) & ~7)                                            
        {                                                                                 
        switch (rounds & 7)                                                               
            {                                                                             
            case 0:                                                                       
                    b[ 1] ^= b[ 2]; b[ 1] = right_rot(b[ 1], rotates[15]); b[ 2] -= b[ 1];
                    b[ 3] ^= b[ 0]; b[ 3] = right_rot(b[ 3], rotates[14]); b[ 0] -= b[ 3];
            case 7:                                                                       
                    b[ 3] ^= b[ 2]; b[ 3] = right_rot(b[ 3], rotates[13]); b[ 2] -= b[ 3];
                    b[ 1] ^= b[ 0]; b[ 1] = right_rot(b[ 1], rotates[12]); b[ 0] -= b[ 1];
            case 6:                                                                       
                    b[ 1] ^= b[ 2]; b[ 1] = right_rot(b[ 1], rotates[11]); b[ 2] -= b[ 1];
                    b[ 3] ^= b[ 0]; b[ 3] = right_rot(b[ 3], rotates[10]); b[ 0] -= b[ 3];
            case 5:                                                                       
                    b[ 3] ^= b[ 2]; b[ 3] = right_rot(b[ 3], rotates[ 9]); b[ 2] -= b[ 3];
                    b[ 1] ^= b[ 0]; b[ 1] = right_rot(b[ 1], rotates[ 8]); b[ 0] -= b[ 1];
            case 4:                                                                       
                    b[ 1] ^= b[ 2]; b[ 1] = right_rot(b[ 1], rotates[ 7]); b[ 2] -= b[ 1];
                    b[ 3] ^= b[ 0]; b[ 3] = right_rot(b[ 3], rotates[ 6]); b[ 0] -= b[ 3];
            case 3:                                                                       
                    b[ 3] ^= b[ 2]; b[ 3] = right_rot(b[ 3], rotates[ 5]); b[ 2] -= b[ 3];
                    b[ 1] ^= b[ 0]; b[ 1] = right_rot(b[ 1], rotates[ 4]); b[ 0] -= b[ 1];
            case 2:                                                                       
                    b[ 1] ^= b[ 2]; b[ 1] = right_rot(b[ 1], rotates[ 3]); b[ 2] -= b[ 1];
                    b[ 3] ^= b[ 0]; b[ 3] = right_rot(b[ 3], rotates[ 2]); b[ 0] -= b[ 3];
            case 1:                                                                       
                    b[ 3] ^= b[ 2]; b[ 3] = right_rot(b[ 3], rotates[ 1]); b[ 2] -= b[ 3];
                    b[ 1] ^= b[ 0]; b[ 1] = right_rot(b[ 1], rotates[ 0]); b[ 0] -= b[ 1];
            }
        }
    }

#ifdef TEST_OR  /* enable this to simplify testing, since OR is not invertible */
#define AddOp(I,J) b[I] += b[J]
#define SubOp(I,J) b[I] -= b[J]
#define XorOp(I,J) b[I] ^= b[J]
#else           /* this is the "real" OR version */
#define AddOp(I,J) b[I] |= b[J]
#define SubOp(I,J) b[I] |= b[J]
#define XorOp(I,J) b[I] |= b[J]
#endif

/* "OR" versions of the cipher: replace ADD, XOR with OR */
void fwd_cycle_16_or(Word *b, const u08b *rotates, int rounds)
    {
    for (;rounds > 0;rounds -=8)
        {
        AddOp( 0, 1); b[ 1] = left_rot(b[ 1], rotates[ 0]); XorOp( 1, 0);
        AddOp( 2, 3); b[ 3] = left_rot(b[ 3], rotates[ 1]); XorOp( 3, 2);
        AddOp( 4, 5); b[ 5] = left_rot(b[ 5], rotates[ 2]); XorOp( 5, 4);
        AddOp( 6, 7); b[ 7] = left_rot(b[ 7], rotates[ 3]); XorOp( 7, 6);
        AddOp( 8, 9); b[ 9] = left_rot(b[ 9], rotates[ 4]); XorOp( 9, 8);
        AddOp(10,11); b[11] = left_rot(b[11], rotates[ 5]); XorOp(11,10);
        AddOp(12,13); b[13] = left_rot(b[13], rotates[ 6]); XorOp(13,12);
        AddOp(14,15); b[15] = left_rot(b[15], rotates[ 7]); XorOp(15,14);
        if (rounds == 1) break;                         
                                                        
        AddOp( 0, 9); b[ 9] = left_rot(b[ 9], rotates[ 8]); XorOp( 9, 0);
        AddOp( 2,13); b[13] = left_rot(b[13], rotates[ 9]); XorOp(13, 2);
        AddOp( 6,11); b[11] = left_rot(b[11], rotates[10]); XorOp(11, 6);
        AddOp( 4,15); b[15] = left_rot(b[15], rotates[11]); XorOp(15, 4);
        AddOp(10, 7); b[ 7] = left_rot(b[ 7], rotates[12]); XorOp( 7,10);
        AddOp(12, 3); b[ 3] = left_rot(b[ 3], rotates[13]); XorOp( 3,12);
        AddOp(14, 5); b[ 5] = left_rot(b[ 5], rotates[14]); XorOp( 5,14);
        AddOp( 8, 1); b[ 1] = left_rot(b[ 1], rotates[15]); XorOp( 1, 8);
        if (rounds == 2) break;                         
                                                        
        AddOp( 0, 7); b[ 7] = left_rot(b[ 7], rotates[16]); XorOp( 7, 0);
        AddOp( 2, 5); b[ 5] = left_rot(b[ 5], rotates[17]); XorOp( 5, 2);
        AddOp( 4, 3); b[ 3] = left_rot(b[ 3], rotates[18]); XorOp( 3, 4);
        AddOp( 6, 1); b[ 1] = left_rot(b[ 1], rotates[19]); XorOp( 1, 6);
        AddOp(12,15); b[15] = left_rot(b[15], rotates[20]); XorOp(15,12);
        AddOp(14,13); b[13] = left_rot(b[13], rotates[21]); XorOp(13,14);
        AddOp( 8,11); b[11] = left_rot(b[11], rotates[22]); XorOp(11, 8);
        AddOp(10, 9); b[ 9] = left_rot(b[ 9], rotates[23]); XorOp( 9,10);
        if (rounds == 3) break;                         
                                                        
        AddOp( 0,15); b[15] = left_rot(b[15], rotates[24]); XorOp(15, 0);
        AddOp( 2,11); b[11] = left_rot(b[11], rotates[25]); XorOp(11, 2);
        AddOp( 6,13); b[13] = left_rot(b[13], rotates[26]); XorOp(13, 6);
        AddOp( 4, 9); b[ 9] = left_rot(b[ 9], rotates[27]); XorOp( 9, 4);
        AddOp(14, 1); b[ 1] = left_rot(b[ 1], rotates[28]); XorOp( 1,14);
        AddOp( 8, 5); b[ 5] = left_rot(b[ 5], rotates[29]); XorOp( 5, 8);
        AddOp(10, 3); b[ 3] = left_rot(b[ 3], rotates[30]); XorOp( 3,10);
        AddOp(12, 7); b[ 7] = left_rot(b[ 7], rotates[31]); XorOp( 7,12);
        if (rounds == 4) break;                         
                                                        
        AddOp( 0, 1); b[ 1] = left_rot(b[ 1], rotates[32]); XorOp( 1, 0);
        AddOp( 2, 3); b[ 3] = left_rot(b[ 3], rotates[33]); XorOp( 3, 2);
        AddOp( 4, 5); b[ 5] = left_rot(b[ 5], rotates[34]); XorOp( 5, 4);
        AddOp( 6, 7); b[ 7] = left_rot(b[ 7], rotates[35]); XorOp( 7, 6);
        AddOp( 8, 9); b[ 9] = left_rot(b[ 9], rotates[36]); XorOp( 9, 8);
        AddOp(10,11); b[11] = left_rot(b[11], rotates[37]); XorOp(11,10);
        AddOp(12,13); b[13] = left_rot(b[13], rotates[38]); XorOp(13,12);
        AddOp(14,15); b[15] = left_rot(b[15], rotates[39]); XorOp(15,14);
        if (rounds == 5) break;                         
                                                        
        AddOp( 0, 9); b[ 9] = left_rot(b[ 9], rotates[40]); XorOp( 9, 0);
        AddOp( 2,13); b[13] = left_rot(b[13], rotates[41]); XorOp(13, 2);
        AddOp( 6,11); b[11] = left_rot(b[11], rotates[42]); XorOp(11, 6);
        AddOp( 4,15); b[15] = left_rot(b[15], rotates[43]); XorOp(15, 4);
        AddOp(10, 7); b[ 7] = left_rot(b[ 7], rotates[44]); XorOp( 7,10);
        AddOp(12, 3); b[ 3] = left_rot(b[ 3], rotates[45]); XorOp( 3,12);
        AddOp(14, 5); b[ 5] = left_rot(b[ 5], rotates[46]); XorOp( 5,14);
        AddOp( 8, 1); b[ 1] = left_rot(b[ 1], rotates[47]); XorOp( 1, 8);
        if (rounds == 6) break;                         
                                                        
        AddOp( 0, 7); b[ 7] = left_rot(b[ 7], rotates[48]); XorOp( 7, 0);
        AddOp( 2, 5); b[ 5] = left_rot(b[ 5], rotates[49]); XorOp( 5, 2);
        AddOp( 4, 3); b[ 3] = left_rot(b[ 3], rotates[50]); XorOp( 3, 4);
        AddOp( 6, 1); b[ 1] = left_rot(b[ 1], rotates[51]); XorOp( 1, 6);
        AddOp(12,15); b[15] = left_rot(b[15], rotates[52]); XorOp(15,12);
        AddOp(14,13); b[13] = left_rot(b[13], rotates[53]); XorOp(13,14);
        AddOp( 8,11); b[11] = left_rot(b[11], rotates[54]); XorOp(11, 8);
        AddOp(10, 9); b[ 9] = left_rot(b[ 9], rotates[55]); XorOp( 9,10);
        if (rounds == 7) break;                         
                                                        
        AddOp( 0,15); b[15] = left_rot(b[15], rotates[56]); XorOp(15, 0);
        AddOp( 2,11); b[11] = left_rot(b[11], rotates[57]); XorOp(11, 2);
        AddOp( 6,13); b[13] = left_rot(b[13], rotates[58]); XorOp(13, 6);
        AddOp( 4, 9); b[ 9] = left_rot(b[ 9], rotates[59]); XorOp( 9, 4);
        AddOp(14, 1); b[ 1] = left_rot(b[ 1], rotates[60]); XorOp( 1,14);
        AddOp( 8, 5); b[ 5] = left_rot(b[ 5], rotates[61]); XorOp( 5, 8);
        AddOp(10, 3); b[ 3] = left_rot(b[ 3], rotates[62]); XorOp( 3,10);
        AddOp(12, 7); b[ 7] = left_rot(b[ 7], rotates[63]); XorOp( 7,12);
        }
    }

void fwd_cycle_8_or(Word *b, const u08b *rotates, int rounds)
    {
    for (;rounds > 0;rounds -=8)
        {
        AddOp( 0, 1); b[ 1] = left_rot(b[ 1], rotates[ 0]); XorOp( 1, 0);
        AddOp( 2, 3); b[ 3] = left_rot(b[ 3], rotates[ 1]); XorOp( 3, 2);
        AddOp( 4, 5); b[ 5] = left_rot(b[ 5], rotates[ 2]); XorOp( 5, 4);
        AddOp( 6, 7); b[ 7] = left_rot(b[ 7], rotates[ 3]); XorOp( 7, 6);
        if (rounds == 1) break;

        AddOp( 2, 1); b[ 1] = left_rot(b[ 1], rotates[ 4]); XorOp( 1, 2);
        AddOp( 4, 7); b[ 7] = left_rot(b[ 7], rotates[ 5]); XorOp( 7, 4);
        AddOp( 6, 5); b[ 5] = left_rot(b[ 5], rotates[ 6]); XorOp( 5, 6);
        AddOp( 0, 3); b[ 3] = left_rot(b[ 3], rotates[ 7]); XorOp( 3, 0);
        if (rounds == 2) break;

        AddOp( 4, 1); b[ 1] = left_rot(b[ 1], rotates[ 8]); XorOp( 1, 4);
        AddOp( 6, 3); b[ 3] = left_rot(b[ 3], rotates[ 9]); XorOp( 3, 6);
        AddOp( 0, 5); b[ 5] = left_rot(b[ 5], rotates[10]); XorOp( 5, 0);
        AddOp( 2, 7); b[ 7] = left_rot(b[ 7], rotates[11]); XorOp( 7, 2);
        if (rounds == 3) break;

        AddOp( 6, 1); b[ 1] = left_rot(b[ 1], rotates[12]); XorOp( 1, 6);
        AddOp( 0, 7); b[ 7] = left_rot(b[ 7], rotates[13]); XorOp( 7, 0);
        AddOp( 2, 5); b[ 5] = left_rot(b[ 5], rotates[14]); XorOp( 5, 2);
        AddOp( 4, 3); b[ 3] = left_rot(b[ 3], rotates[15]); XorOp( 3, 4);
        if (rounds == 4) break;

        AddOp( 0, 1); b[ 1] = left_rot(b[ 1], rotates[16]); XorOp( 1, 0);
        AddOp( 2, 3); b[ 3] = left_rot(b[ 3], rotates[17]); XorOp( 3, 2);
        AddOp( 4, 5); b[ 5] = left_rot(b[ 5], rotates[18]); XorOp( 5, 4);
        AddOp( 6, 7); b[ 7] = left_rot(b[ 7], rotates[19]); XorOp( 7, 6);
        if (rounds == 5) break;

        AddOp( 2, 1); b[ 1] = left_rot(b[ 1], rotates[20]); XorOp( 1, 2);
        AddOp( 4, 7); b[ 7] = left_rot(b[ 7], rotates[21]); XorOp( 7, 4);
        AddOp( 6, 5); b[ 5] = left_rot(b[ 5], rotates[22]); XorOp( 5, 6);
        AddOp( 0, 3); b[ 3] = left_rot(b[ 3], rotates[23]); XorOp( 3, 0);
        if (rounds == 6) break;

        AddOp( 4, 1); b[ 1] = left_rot(b[ 1], rotates[24]); XorOp( 1, 4);
        AddOp( 6, 3); b[ 3] = left_rot(b[ 3], rotates[25]); XorOp( 3, 6);
        AddOp( 0, 5); b[ 5] = left_rot(b[ 5], rotates[26]); XorOp( 5, 0);
        AddOp( 2, 7); b[ 7] = left_rot(b[ 7], rotates[27]); XorOp( 7, 2);
        if (rounds == 7) break;

        AddOp( 6, 1); b[ 1] = left_rot(b[ 1], rotates[28]); XorOp( 1, 6);
        AddOp( 0, 7); b[ 7] = left_rot(b[ 7], rotates[29]); XorOp( 7, 0);
        AddOp( 2, 5); b[ 5] = left_rot(b[ 5], rotates[30]); XorOp( 5, 2);
        AddOp( 4, 3); b[ 3] = left_rot(b[ 3], rotates[31]); XorOp( 3, 4);
        }
    }

void fwd_cycle_4_or(Word *b, const u08b *rotates, int rounds)
    {
    for (;rounds > 0;rounds -=8)
        {
        AddOp( 0, 1); b[ 1] = left_rot(b[ 1], rotates[ 0]); XorOp( 1, 0);
        AddOp( 2, 3); b[ 3] = left_rot(b[ 3], rotates[ 1]); XorOp( 3, 2);
        if (rounds == 1) break;

        AddOp( 0, 3); b[ 3] = left_rot(b[ 3], rotates[ 2]); XorOp( 3, 0);
        AddOp( 2, 1); b[ 1] = left_rot(b[ 1], rotates[ 3]); XorOp( 1, 2);
        if (rounds == 2) break;

        AddOp( 0, 1); b[ 1] = left_rot(b[ 1], rotates[ 4]); XorOp( 1, 0);
        AddOp( 2, 3); b[ 3] = left_rot(b[ 3], rotates[ 5]); XorOp( 3, 2);
        if (rounds == 3) break;

        AddOp( 0, 3); b[ 3] = left_rot(b[ 3], rotates[ 6]); XorOp( 3, 0);
        AddOp( 2, 1); b[ 1] = left_rot(b[ 1], rotates[ 7]); XorOp( 1, 2);
        if (rounds == 4) break;

        AddOp( 0, 1); b[ 1] = left_rot(b[ 1], rotates[ 8]); XorOp( 1, 0);
        AddOp( 2, 3); b[ 3] = left_rot(b[ 3], rotates[ 9]); XorOp( 3, 2);
        if (rounds == 5) break;

        AddOp( 0, 3); b[ 3] = left_rot(b[ 3], rotates[10]); XorOp( 3, 0);
        AddOp( 2, 1); b[ 1] = left_rot(b[ 1], rotates[11]); XorOp( 1, 2);
        if (rounds == 6) break;

        AddOp( 0, 1); b[ 1] = left_rot(b[ 1], rotates[12]); XorOp( 1, 0);
        AddOp( 2, 3); b[ 3] = left_rot(b[ 3], rotates[13]); XorOp( 3, 2);
        if (rounds == 7) break;

        AddOp( 0, 3); b[ 3] = left_rot(b[ 3], rotates[14]); XorOp( 3, 0);
        AddOp( 2, 1); b[ 1] = left_rot(b[ 1], rotates[15]); XorOp( 1, 2);
        }
    }

/* reverse versions of the cipher, using OR */
void rev_cycle_16_or(Word *b, const u08b *rotates, int rounds)
    {
    for (;rounds > 0;rounds = (rounds-1) & ~7)
        {
        switch (rounds & 7)
            {
            case 0:
                    XorOp( 7,12); b[ 7] = right_rot(b[ 7], rotates[63]); SubOp(12, 7); 
                    XorOp( 3,10); b[ 3] = right_rot(b[ 3], rotates[62]); SubOp(10, 3); 
                    XorOp( 5, 8); b[ 5] = right_rot(b[ 5], rotates[61]); SubOp( 8, 5); 
                    XorOp( 1,14); b[ 1] = right_rot(b[ 1], rotates[60]); SubOp(14, 1); 
                    XorOp( 9, 4); b[ 9] = right_rot(b[ 9], rotates[59]); SubOp( 4, 9); 
                    XorOp(13, 6); b[13] = right_rot(b[13], rotates[58]); SubOp( 6,13); 
                    XorOp(11, 2); b[11] = right_rot(b[11], rotates[57]); SubOp( 2,11); 
                    XorOp(15, 0); b[15] = right_rot(b[15], rotates[56]); SubOp( 0,15);
            case 7:
                    XorOp( 9,10); b[ 9] = right_rot(b[ 9], rotates[55]); SubOp(10, 9); 
                    XorOp(11, 8); b[11] = right_rot(b[11], rotates[54]); SubOp( 8,11); 
                    XorOp(13,14); b[13] = right_rot(b[13], rotates[53]); SubOp(14,13); 
                    XorOp(15,12); b[15] = right_rot(b[15], rotates[52]); SubOp(12,15); 
                    XorOp( 1, 6); b[ 1] = right_rot(b[ 1], rotates[51]); SubOp( 6, 1); 
                    XorOp( 3, 4); b[ 3] = right_rot(b[ 3], rotates[50]); SubOp( 4, 3); 
                    XorOp( 5, 2); b[ 5] = right_rot(b[ 5], rotates[49]); SubOp( 2, 5); 
                    XorOp( 7, 0); b[ 7] = right_rot(b[ 7], rotates[48]); SubOp( 0, 7);
            case 6:
                    XorOp( 1, 8); b[ 1] = right_rot(b[ 1], rotates[47]); SubOp( 8, 1); 
                    XorOp( 5,14); b[ 5] = right_rot(b[ 5], rotates[46]); SubOp(14, 5); 
                    XorOp( 3,12); b[ 3] = right_rot(b[ 3], rotates[45]); SubOp(12, 3); 
                    XorOp( 7,10); b[ 7] = right_rot(b[ 7], rotates[44]); SubOp(10, 7); 
                    XorOp(15, 4); b[15] = right_rot(b[15], rotates[43]); SubOp( 4,15); 
                    XorOp(11, 6); b[11] = right_rot(b[11], rotates[42]); SubOp( 6,11); 
                    XorOp(13, 2); b[13] = right_rot(b[13], rotates[41]); SubOp( 2,13); 
                    XorOp( 9, 0); b[ 9] = right_rot(b[ 9], rotates[40]); SubOp( 0, 9);
            case 5:
                    XorOp(15,14); b[15] = right_rot(b[15], rotates[39]); SubOp(14,15); 
                    XorOp(13,12); b[13] = right_rot(b[13], rotates[38]); SubOp(12,13); 
                    XorOp(11,10); b[11] = right_rot(b[11], rotates[37]); SubOp(10,11); 
                    XorOp( 9, 8); b[ 9] = right_rot(b[ 9], rotates[36]); SubOp( 8, 9); 
                    XorOp( 7, 6); b[ 7] = right_rot(b[ 7], rotates[35]); SubOp( 6, 7); 
                    XorOp( 5, 4); b[ 5] = right_rot(b[ 5], rotates[34]); SubOp( 4, 5); 
                    XorOp( 3, 2); b[ 3] = right_rot(b[ 3], rotates[33]); SubOp( 2, 3); 
                    XorOp( 1, 0); b[ 1] = right_rot(b[ 1], rotates[32]); SubOp( 0, 1);
            case 4:
                    XorOp( 7,12); b[ 7] = right_rot(b[ 7], rotates[31]); SubOp(12, 7); 
                    XorOp( 3,10); b[ 3] = right_rot(b[ 3], rotates[30]); SubOp(10, 3); 
                    XorOp( 5, 8); b[ 5] = right_rot(b[ 5], rotates[29]); SubOp( 8, 5); 
                    XorOp( 1,14); b[ 1] = right_rot(b[ 1], rotates[28]); SubOp(14, 1); 
                    XorOp( 9, 4); b[ 9] = right_rot(b[ 9], rotates[27]); SubOp( 4, 9); 
                    XorOp(13, 6); b[13] = right_rot(b[13], rotates[26]); SubOp( 6,13); 
                    XorOp(11, 2); b[11] = right_rot(b[11], rotates[25]); SubOp( 2,11); 
                    XorOp(15, 0); b[15] = right_rot(b[15], rotates[24]); SubOp( 0,15);
            case 3:                                                                       
                    XorOp( 9,10); b[ 9] = right_rot(b[ 9], rotates[23]); SubOp(10, 9);
                    XorOp(11, 8); b[11] = right_rot(b[11], rotates[22]); SubOp( 8,11);
                    XorOp(13,14); b[13] = right_rot(b[13], rotates[21]); SubOp(14,13);
                    XorOp(15,12); b[15] = right_rot(b[15], rotates[20]); SubOp(12,15);
                    XorOp( 1, 6); b[ 1] = right_rot(b[ 1], rotates[19]); SubOp( 6, 1);
                    XorOp( 3, 4); b[ 3] = right_rot(b[ 3], rotates[18]); SubOp( 4, 3);
                    XorOp( 5, 2); b[ 5] = right_rot(b[ 5], rotates[17]); SubOp( 2, 5);
                    XorOp( 7, 0); b[ 7] = right_rot(b[ 7], rotates[16]); SubOp( 0, 7);
            case 2:                                                                       
                    XorOp( 1, 8); b[ 1] = right_rot(b[ 1], rotates[15]); SubOp( 8, 1);
                    XorOp( 5,14); b[ 5] = right_rot(b[ 5], rotates[14]); SubOp(14, 5);
                    XorOp( 3,12); b[ 3] = right_rot(b[ 3], rotates[13]); SubOp(12, 3);
                    XorOp( 7,10); b[ 7] = right_rot(b[ 7], rotates[12]); SubOp(10, 7);
                    XorOp(15, 4); b[15] = right_rot(b[15], rotates[11]); SubOp( 4,15);
                    XorOp(11, 6); b[11] = right_rot(b[11], rotates[10]); SubOp( 6,11);
                    XorOp(13, 2); b[13] = right_rot(b[13], rotates[ 9]); SubOp( 2,13);
                    XorOp( 9, 0); b[ 9] = right_rot(b[ 9], rotates[ 8]); SubOp( 0, 9);
            case 1:                                                                       
                    XorOp(15,14); b[15] = right_rot(b[15], rotates[ 7]); SubOp(14,15);
                    XorOp(13,12); b[13] = right_rot(b[13], rotates[ 6]); SubOp(12,13);
                    XorOp(11,10); b[11] = right_rot(b[11], rotates[ 5]); SubOp(10,11);
                    XorOp( 9, 8); b[ 9] = right_rot(b[ 9], rotates[ 4]); SubOp( 8, 9);
                    XorOp( 7, 6); b[ 7] = right_rot(b[ 7], rotates[ 3]); SubOp( 6, 7);
                    XorOp( 5, 4); b[ 5] = right_rot(b[ 5], rotates[ 2]); SubOp( 4, 5);
                    XorOp( 3, 2); b[ 3] = right_rot(b[ 3], rotates[ 1]); SubOp( 2, 3);
                    XorOp( 1, 0); b[ 1] = right_rot(b[ 1], rotates[ 0]); SubOp( 0, 1);
            }                                                                             
                                                                                          
        }                                                                                 
    }                                                                                     
                                                                                          
void rev_cycle_8_or(Word *b, const u08b *rotates, int rounds)                             
    {                                                                                     
    for (;rounds > 0;rounds = (rounds-1) & ~7)                                            
        {                                                                                 
        switch (rounds & 7)                                                               
            {                                                                             
            case 0:                                                                       
                    XorOp( 3, 4); b[ 3] = right_rot(b[ 3], rotates[31]); SubOp( 4, 3);
                    XorOp( 5, 2); b[ 5] = right_rot(b[ 5], rotates[30]); SubOp( 2, 5);
                    XorOp( 7, 0); b[ 7] = right_rot(b[ 7], rotates[29]); SubOp( 0, 7);
                    XorOp( 1, 6); b[ 1] = right_rot(b[ 1], rotates[28]); SubOp( 6, 1);
            case 7:                                                                       
                    XorOp( 7, 2); b[ 7] = right_rot(b[ 7], rotates[27]); SubOp( 2, 7);
                    XorOp( 5, 0); b[ 5] = right_rot(b[ 5], rotates[26]); SubOp( 0, 5);
                    XorOp( 3, 6); b[ 3] = right_rot(b[ 3], rotates[25]); SubOp( 6, 3);
                    XorOp( 1, 4); b[ 1] = right_rot(b[ 1], rotates[24]); SubOp( 4, 1);
            case 6:                                                                       
                    XorOp( 3, 0); b[ 3] = right_rot(b[ 3], rotates[23]); SubOp( 0, 3);
                    XorOp( 5, 6); b[ 5] = right_rot(b[ 5], rotates[22]); SubOp( 6, 5);
                    XorOp( 7, 4); b[ 7] = right_rot(b[ 7], rotates[21]); SubOp( 4, 7);
                    XorOp( 1, 2); b[ 1] = right_rot(b[ 1], rotates[20]); SubOp( 2, 1);
            case 5:                                                                       
                    XorOp( 7, 6); b[ 7] = right_rot(b[ 7], rotates[19]); SubOp( 6, 7);
                    XorOp( 5, 4); b[ 5] = right_rot(b[ 5], rotates[18]); SubOp( 4, 5);
                    XorOp( 3, 2); b[ 3] = right_rot(b[ 3], rotates[17]); SubOp( 2, 3);
                    XorOp( 1, 0); b[ 1] = right_rot(b[ 1], rotates[16]); SubOp( 0, 1);
            case 4:                                                                       
                    XorOp( 3, 4); b[ 3] = right_rot(b[ 3], rotates[15]); SubOp( 4, 3);
                    XorOp( 5, 2); b[ 5] = right_rot(b[ 5], rotates[14]); SubOp( 2, 5);
                    XorOp( 7, 0); b[ 7] = right_rot(b[ 7], rotates[13]); SubOp( 0, 7);
                    XorOp( 1, 6); b[ 1] = right_rot(b[ 1], rotates[12]); SubOp( 6, 1);
            case 3:                                                                       
                    XorOp( 7, 2); b[ 7] = right_rot(b[ 7], rotates[11]); SubOp( 2, 7);
                    XorOp( 5, 0); b[ 5] = right_rot(b[ 5], rotates[10]); SubOp( 0, 5);
                    XorOp( 3, 6); b[ 3] = right_rot(b[ 3], rotates[ 9]); SubOp( 6, 3);
                    XorOp( 1, 4); b[ 1] = right_rot(b[ 1], rotates[ 8]); SubOp( 4, 1);
            case 2:                                                                       
                    XorOp( 3, 0); b[ 3] = right_rot(b[ 3], rotates[ 7]); SubOp( 0, 3);
                    XorOp( 5, 6); b[ 5] = right_rot(b[ 5], rotates[ 6]); SubOp( 6, 5);
                    XorOp( 7, 4); b[ 7] = right_rot(b[ 7], rotates[ 5]); SubOp( 4, 7);
                    XorOp( 1, 2); b[ 1] = right_rot(b[ 1], rotates[ 4]); SubOp( 2, 1);
            case 1:                                                                       
                    XorOp( 7, 6); b[ 7] = right_rot(b[ 7], rotates[ 3]); SubOp( 6, 7);
                    XorOp( 5, 4); b[ 5] = right_rot(b[ 5], rotates[ 2]); SubOp( 4, 5);
                    XorOp( 3, 2); b[ 3] = right_rot(b[ 3], rotates[ 1]); SubOp( 2, 3);
                    XorOp( 1, 0); b[ 1] = right_rot(b[ 1], rotates[ 0]); SubOp( 0, 1);
            }                                                                             
        }                                                                                 
    }                                                                                     
                                                                                          
void rev_cycle_4_or(Word *b, const u08b *rotates, int rounds)                             
    {                                                                                     
    for (;rounds > 0;rounds = (rounds-1) & ~7)                                            
        {                                                                                 
        switch (rounds & 7)                                                               
            {                                                                             
            case 0:                                                                       
                    XorOp( 1, 2); b[ 1] = right_rot(b[ 1], rotates[15]); SubOp( 2, 1);
                    XorOp( 3, 0); b[ 3] = right_rot(b[ 3], rotates[14]); SubOp( 0, 3);
            case 7:                                                                       
                    XorOp( 3, 2); b[ 3] = right_rot(b[ 3], rotates[13]); SubOp( 2, 3);
                    XorOp( 1, 0); b[ 1] = right_rot(b[ 1], rotates[12]); SubOp( 0, 1);
            case 6:                                                                       
                    XorOp( 1, 2); b[ 1] = right_rot(b[ 1], rotates[11]); SubOp( 2, 1);
                    XorOp( 3, 0); b[ 3] = right_rot(b[ 3], rotates[10]); SubOp( 0, 3);
            case 5:                                                                       
                    XorOp( 3, 2); b[ 3] = right_rot(b[ 3], rotates[ 9]); SubOp( 2, 3);
                    XorOp( 1, 0); b[ 1] = right_rot(b[ 1], rotates[ 8]); SubOp( 0, 1);
            case 4:                                                                       
                    XorOp( 1, 2); b[ 1] = right_rot(b[ 1], rotates[ 7]); SubOp( 2, 1);
                    XorOp( 3, 0); b[ 3] = right_rot(b[ 3], rotates[ 6]); SubOp( 0, 3);
            case 3:                                                                       
                    XorOp( 3, 2); b[ 3] = right_rot(b[ 3], rotates[ 5]); SubOp( 2, 3);
                    XorOp( 1, 0); b[ 1] = right_rot(b[ 1], rotates[ 4]); SubOp( 0, 1);
            case 2:                                                                       
                    XorOp( 1, 2); b[ 1] = right_rot(b[ 1], rotates[ 3]); SubOp( 2, 1);
                    XorOp( 3, 0); b[ 3] = right_rot(b[ 3], rotates[ 2]); SubOp( 0, 3);
            case 1:                                                                       
                    XorOp( 3, 2); b[ 3] = right_rot(b[ 3], rotates[ 1]); SubOp( 2, 3);
                    XorOp( 1, 0); b[ 1] = right_rot(b[ 1], rotates[ 0]); SubOp( 0, 1);
            }
        }
    }

/* optimized versions for default round counts */
#if   defined(__BORLANDC__)
#pragma argsused
#elif defined(_MSC_VER)
#pragma warning(disable:4100)
#endif
void fwd_cycle_16_or_r9(Word *b, const u08b *rotates, int rounds)
    {
        AddOp( 0, 1); b[ 1] = left_rot(b[ 1], rotates[ 0]); XorOp( 1, 0);
        AddOp( 2, 3); b[ 3] = left_rot(b[ 3], rotates[ 1]); XorOp( 3, 2);
        AddOp( 4, 5); b[ 5] = left_rot(b[ 5], rotates[ 2]); XorOp( 5, 4);
        AddOp( 6, 7); b[ 7] = left_rot(b[ 7], rotates[ 3]); XorOp( 7, 6);
        AddOp( 8, 9); b[ 9] = left_rot(b[ 9], rotates[ 4]); XorOp( 9, 8);
        AddOp(10,11); b[11] = left_rot(b[11], rotates[ 5]); XorOp(11,10);
        AddOp(12,13); b[13] = left_rot(b[13], rotates[ 6]); XorOp(13,12);
        AddOp(14,15); b[15] = left_rot(b[15], rotates[ 7]); XorOp(15,14);

        AddOp( 0, 9); b[ 9] = left_rot(b[ 9], rotates[ 8]); XorOp( 9, 0);
        AddOp( 2,13); b[13] = left_rot(b[13], rotates[ 9]); XorOp(13, 2);
        AddOp( 6,11); b[11] = left_rot(b[11], rotates[10]); XorOp(11, 6);
        AddOp( 4,15); b[15] = left_rot(b[15], rotates[11]); XorOp(15, 4);
        AddOp(10, 7); b[ 7] = left_rot(b[ 7], rotates[12]); XorOp( 7,10);
        AddOp(12, 3); b[ 3] = left_rot(b[ 3], rotates[13]); XorOp( 3,12);
        AddOp(14, 5); b[ 5] = left_rot(b[ 5], rotates[14]); XorOp( 5,14);
        AddOp( 8, 1); b[ 1] = left_rot(b[ 1], rotates[15]); XorOp( 1, 8);

        AddOp( 0, 7); b[ 7] = left_rot(b[ 7], rotates[16]); XorOp( 7, 0);
        AddOp( 2, 5); b[ 5] = left_rot(b[ 5], rotates[17]); XorOp( 5, 2);
        AddOp( 4, 3); b[ 3] = left_rot(b[ 3], rotates[18]); XorOp( 3, 4);
        AddOp( 6, 1); b[ 1] = left_rot(b[ 1], rotates[19]); XorOp( 1, 6);
        AddOp(12,15); b[15] = left_rot(b[15], rotates[20]); XorOp(15,12);
        AddOp(14,13); b[13] = left_rot(b[13], rotates[21]); XorOp(13,14);
        AddOp( 8,11); b[11] = left_rot(b[11], rotates[22]); XorOp(11, 8);
        AddOp(10, 9); b[ 9] = left_rot(b[ 9], rotates[23]); XorOp( 9,10);

        AddOp( 0,15); b[15] = left_rot(b[15], rotates[24]); XorOp(15, 0);
        AddOp( 2,11); b[11] = left_rot(b[11], rotates[25]); XorOp(11, 2);
        AddOp( 6,13); b[13] = left_rot(b[13], rotates[26]); XorOp(13, 6);
        AddOp( 4, 9); b[ 9] = left_rot(b[ 9], rotates[27]); XorOp( 9, 4);
        AddOp(14, 1); b[ 1] = left_rot(b[ 1], rotates[28]); XorOp( 1,14);
        AddOp( 8, 5); b[ 5] = left_rot(b[ 5], rotates[29]); XorOp( 5, 8);
        AddOp(10, 3); b[ 3] = left_rot(b[ 3], rotates[30]); XorOp( 3,10);
        AddOp(12, 7); b[ 7] = left_rot(b[ 7], rotates[31]); XorOp( 7,12);

        AddOp( 0, 1); b[ 1] = left_rot(b[ 1], rotates[32]); XorOp( 1, 0);
        AddOp( 2, 3); b[ 3] = left_rot(b[ 3], rotates[33]); XorOp( 3, 2);
        AddOp( 4, 5); b[ 5] = left_rot(b[ 5], rotates[34]); XorOp( 5, 4);
        AddOp( 6, 7); b[ 7] = left_rot(b[ 7], rotates[35]); XorOp( 7, 6);
        AddOp( 8, 9); b[ 9] = left_rot(b[ 9], rotates[36]); XorOp( 9, 8);
        AddOp(10,11); b[11] = left_rot(b[11], rotates[37]); XorOp(11,10);
        AddOp(12,13); b[13] = left_rot(b[13], rotates[38]); XorOp(13,12);
        AddOp(14,15); b[15] = left_rot(b[15], rotates[39]); XorOp(15,14);

        AddOp( 0, 9); b[ 9] = left_rot(b[ 9], rotates[40]); XorOp( 9, 0);
        AddOp( 2,13); b[13] = left_rot(b[13], rotates[41]); XorOp(13, 2);
        AddOp( 6,11); b[11] = left_rot(b[11], rotates[42]); XorOp(11, 6);
        AddOp( 4,15); b[15] = left_rot(b[15], rotates[43]); XorOp(15, 4);
        AddOp(10, 7); b[ 7] = left_rot(b[ 7], rotates[44]); XorOp( 7,10);
        AddOp(12, 3); b[ 3] = left_rot(b[ 3], rotates[45]); XorOp( 3,12);
        AddOp(14, 5); b[ 5] = left_rot(b[ 5], rotates[46]); XorOp( 5,14);
        AddOp( 8, 1); b[ 1] = left_rot(b[ 1], rotates[47]); XorOp( 1, 8);

        AddOp( 0, 7); b[ 7] = left_rot(b[ 7], rotates[48]); XorOp( 7, 0);
        AddOp( 2, 5); b[ 5] = left_rot(b[ 5], rotates[49]); XorOp( 5, 2);
        AddOp( 4, 3); b[ 3] = left_rot(b[ 3], rotates[50]); XorOp( 3, 4);
        AddOp( 6, 1); b[ 1] = left_rot(b[ 1], rotates[51]); XorOp( 1, 6);
        AddOp(12,15); b[15] = left_rot(b[15], rotates[52]); XorOp(15,12);
        AddOp(14,13); b[13] = left_rot(b[13], rotates[53]); XorOp(13,14);
        AddOp( 8,11); b[11] = left_rot(b[11], rotates[54]); XorOp(11, 8);
        AddOp(10, 9); b[ 9] = left_rot(b[ 9], rotates[55]); XorOp( 9,10);

        AddOp( 0,15); b[15] = left_rot(b[15], rotates[56]); XorOp(15, 0);
        AddOp( 2,11); b[11] = left_rot(b[11], rotates[57]); XorOp(11, 2);
        AddOp( 6,13); b[13] = left_rot(b[13], rotates[58]); XorOp(13, 6);
        AddOp( 4, 9); b[ 9] = left_rot(b[ 9], rotates[59]); XorOp( 9, 4);
        AddOp(14, 1); b[ 1] = left_rot(b[ 1], rotates[60]); XorOp( 1,14);
        AddOp( 8, 5); b[ 5] = left_rot(b[ 5], rotates[61]); XorOp( 5, 8);
        AddOp(10, 3); b[ 3] = left_rot(b[ 3], rotates[62]); XorOp( 3,10);
        AddOp(12, 7); b[ 7] = left_rot(b[ 7], rotates[63]); XorOp( 7,12);

        AddOp( 0, 1); b[ 1] = left_rot(b[ 1], rotates[ 0]); XorOp( 1, 0);
        AddOp( 2, 3); b[ 3] = left_rot(b[ 3], rotates[ 1]); XorOp( 3, 2);
        AddOp( 4, 5); b[ 5] = left_rot(b[ 5], rotates[ 2]); XorOp( 5, 4);
        AddOp( 6, 7); b[ 7] = left_rot(b[ 7], rotates[ 3]); XorOp( 7, 6);
        AddOp( 8, 9); b[ 9] = left_rot(b[ 9], rotates[ 4]); XorOp( 9, 8);
        AddOp(10,11); b[11] = left_rot(b[11], rotates[ 5]); XorOp(11,10);
        AddOp(12,13); b[13] = left_rot(b[13], rotates[ 6]); XorOp(13,12);
        AddOp(14,15); b[15] = left_rot(b[15], rotates[ 7]); XorOp(15,14);
    }

#if   defined(__BORLANDC__)
#pragma argsused
#endif
void fwd_cycle_8_or_r8(Word *b, const u08b *rotates, int rounds)
    {
        AddOp( 0, 1); b[ 1] = left_rot(b[ 1], rotates[ 0]); XorOp( 1, 0);
        AddOp( 2, 3); b[ 3] = left_rot(b[ 3], rotates[ 1]); XorOp( 3, 2);
        AddOp( 4, 5); b[ 5] = left_rot(b[ 5], rotates[ 2]); XorOp( 5, 4);
        AddOp( 6, 7); b[ 7] = left_rot(b[ 7], rotates[ 3]); XorOp( 7, 6);

        AddOp( 2, 1); b[ 1] = left_rot(b[ 1], rotates[ 4]); XorOp( 1, 2);
        AddOp( 4, 7); b[ 7] = left_rot(b[ 7], rotates[ 5]); XorOp( 7, 4);
        AddOp( 6, 5); b[ 5] = left_rot(b[ 5], rotates[ 6]); XorOp( 5, 6);
        AddOp( 0, 3); b[ 3] = left_rot(b[ 3], rotates[ 7]); XorOp( 3, 0);

        AddOp( 4, 1); b[ 1] = left_rot(b[ 1], rotates[ 8]); XorOp( 1, 4);
        AddOp( 6, 3); b[ 3] = left_rot(b[ 3], rotates[ 9]); XorOp( 3, 6);
        AddOp( 0, 5); b[ 5] = left_rot(b[ 5], rotates[10]); XorOp( 5, 0);
        AddOp( 2, 7); b[ 7] = left_rot(b[ 7], rotates[11]); XorOp( 7, 2);

        AddOp( 6, 1); b[ 1] = left_rot(b[ 1], rotates[12]); XorOp( 1, 6);
        AddOp( 0, 7); b[ 7] = left_rot(b[ 7], rotates[13]); XorOp( 7, 0);
        AddOp( 2, 5); b[ 5] = left_rot(b[ 5], rotates[14]); XorOp( 5, 2);
        AddOp( 4, 3); b[ 3] = left_rot(b[ 3], rotates[15]); XorOp( 3, 4);

        AddOp( 0, 1); b[ 1] = left_rot(b[ 1], rotates[16]); XorOp( 1, 0);
        AddOp( 2, 3); b[ 3] = left_rot(b[ 3], rotates[17]); XorOp( 3, 2);
        AddOp( 4, 5); b[ 5] = left_rot(b[ 5], rotates[18]); XorOp( 5, 4);
        AddOp( 6, 7); b[ 7] = left_rot(b[ 7], rotates[19]); XorOp( 7, 6);

        AddOp( 2, 1); b[ 1] = left_rot(b[ 1], rotates[20]); XorOp( 1, 2);
        AddOp( 4, 7); b[ 7] = left_rot(b[ 7], rotates[21]); XorOp( 7, 4);
        AddOp( 6, 5); b[ 5] = left_rot(b[ 5], rotates[22]); XorOp( 5, 6);
        AddOp( 0, 3); b[ 3] = left_rot(b[ 3], rotates[23]); XorOp( 3, 0);

        AddOp( 4, 1); b[ 1] = left_rot(b[ 1], rotates[24]); XorOp( 1, 4);
        AddOp( 6, 3); b[ 3] = left_rot(b[ 3], rotates[25]); XorOp( 3, 6);
        AddOp( 0, 5); b[ 5] = left_rot(b[ 5], rotates[26]); XorOp( 5, 0);
        AddOp( 2, 7); b[ 7] = left_rot(b[ 7], rotates[27]); XorOp( 7, 2);

        AddOp( 6, 1); b[ 1] = left_rot(b[ 1], rotates[28]); XorOp( 1, 6);
        AddOp( 0, 7); b[ 7] = left_rot(b[ 7], rotates[29]); XorOp( 7, 0);
        AddOp( 2, 5); b[ 5] = left_rot(b[ 5], rotates[30]); XorOp( 5, 2);
        AddOp( 4, 3); b[ 3] = left_rot(b[ 3], rotates[31]); XorOp( 3, 4);
    }

#ifdef __BORLANDC__
#pragma argsused
#endif
void fwd_cycle_4_or_r8(Word *b, const u08b *rotates, int rounds)
    {
        AddOp( 0, 1); b[ 1] = left_rot(b[ 1], rotates[ 0]); XorOp( 1, 0);
        AddOp( 2, 3); b[ 3] = left_rot(b[ 3], rotates[ 1]); XorOp( 3, 2);

        AddOp( 0, 3); b[ 3] = left_rot(b[ 3], rotates[ 2]); XorOp( 3, 0);
        AddOp( 2, 1); b[ 1] = left_rot(b[ 1], rotates[ 3]); XorOp( 1, 2);

        AddOp( 0, 1); b[ 1] = left_rot(b[ 1], rotates[ 4]); XorOp( 1, 0);
        AddOp( 2, 3); b[ 3] = left_rot(b[ 3], rotates[ 5]); XorOp( 3, 2);

        AddOp( 0, 3); b[ 3] = left_rot(b[ 3], rotates[ 6]); XorOp( 3, 0);
        AddOp( 2, 1); b[ 1] = left_rot(b[ 1], rotates[ 7]); XorOp( 1, 2);

        AddOp( 0, 1); b[ 1] = left_rot(b[ 1], rotates[ 8]); XorOp( 1, 0);
        AddOp( 2, 3); b[ 3] = left_rot(b[ 3], rotates[ 9]); XorOp( 3, 2);

        AddOp( 0, 3); b[ 3] = left_rot(b[ 3], rotates[10]); XorOp( 3, 0);
        AddOp( 2, 1); b[ 1] = left_rot(b[ 1], rotates[11]); XorOp( 1, 2);

        AddOp( 0, 1); b[ 1] = left_rot(b[ 1], rotates[12]); XorOp( 1, 0);
        AddOp( 2, 3); b[ 3] = left_rot(b[ 3], rotates[13]); XorOp( 3, 2);

        AddOp( 0, 3); b[ 3] = left_rot(b[ 3], rotates[14]); XorOp( 3, 0);
        AddOp( 2, 1); b[ 1] = left_rot(b[ 1], rotates[15]); XorOp( 1, 2);
    }

/* reverse versions of the cipher, using OR, for fixed round numbers */
#ifdef __BORLANDC__
#pragma argsused
#endif
void rev_cycle_16_or_r9(Word *b, const u08b *rotates, int rounds)
    {
    XorOp(15,14); b[15] = right_rot(b[15], rotates[ 7]); SubOp(14,15);
    XorOp(13,12); b[13] = right_rot(b[13], rotates[ 6]); SubOp(12,13);
    XorOp(11,10); b[11] = right_rot(b[11], rotates[ 5]); SubOp(10,11);
    XorOp( 9, 8); b[ 9] = right_rot(b[ 9], rotates[ 4]); SubOp( 8, 9);
    XorOp( 7, 6); b[ 7] = right_rot(b[ 7], rotates[ 3]); SubOp( 6, 7);
    XorOp( 5, 4); b[ 5] = right_rot(b[ 5], rotates[ 2]); SubOp( 4, 5);
    XorOp( 3, 2); b[ 3] = right_rot(b[ 3], rotates[ 1]); SubOp( 2, 3);
    XorOp( 1, 0); b[ 1] = right_rot(b[ 1], rotates[ 0]); SubOp( 0, 1);
                                                     
    XorOp( 7,12); b[ 7] = right_rot(b[ 7], rotates[63]); SubOp(12, 7); 
    XorOp( 3,10); b[ 3] = right_rot(b[ 3], rotates[62]); SubOp(10, 3); 
    XorOp( 5, 8); b[ 5] = right_rot(b[ 5], rotates[61]); SubOp( 8, 5); 
    XorOp( 1,14); b[ 1] = right_rot(b[ 1], rotates[60]); SubOp(14, 1); 
    XorOp( 9, 4); b[ 9] = right_rot(b[ 9], rotates[59]); SubOp( 4, 9); 
    XorOp(13, 6); b[13] = right_rot(b[13], rotates[58]); SubOp( 6,13); 
    XorOp(11, 2); b[11] = right_rot(b[11], rotates[57]); SubOp( 2,11); 
    XorOp(15, 0); b[15] = right_rot(b[15], rotates[56]); SubOp( 0,15);
                                                     
    XorOp( 9,10); b[ 9] = right_rot(b[ 9], rotates[55]); SubOp(10, 9); 
    XorOp(11, 8); b[11] = right_rot(b[11], rotates[54]); SubOp( 8,11); 
    XorOp(13,14); b[13] = right_rot(b[13], rotates[53]); SubOp(14,13); 
    XorOp(15,12); b[15] = right_rot(b[15], rotates[52]); SubOp(12,15); 
    XorOp( 1, 6); b[ 1] = right_rot(b[ 1], rotates[51]); SubOp( 6, 1); 
    XorOp( 3, 4); b[ 3] = right_rot(b[ 3], rotates[50]); SubOp( 4, 3); 
    XorOp( 5, 2); b[ 5] = right_rot(b[ 5], rotates[49]); SubOp( 2, 5); 
    XorOp( 7, 0); b[ 7] = right_rot(b[ 7], rotates[48]); SubOp( 0, 7);
                                                     
    XorOp( 1, 8); b[ 1] = right_rot(b[ 1], rotates[47]); SubOp( 8, 1); 
    XorOp( 5,14); b[ 5] = right_rot(b[ 5], rotates[46]); SubOp(14, 5); 
    XorOp( 3,12); b[ 3] = right_rot(b[ 3], rotates[45]); SubOp(12, 3); 
    XorOp( 7,10); b[ 7] = right_rot(b[ 7], rotates[44]); SubOp(10, 7); 
    XorOp(15, 4); b[15] = right_rot(b[15], rotates[43]); SubOp( 4,15); 
    XorOp(11, 6); b[11] = right_rot(b[11], rotates[42]); SubOp( 6,11); 
    XorOp(13, 2); b[13] = right_rot(b[13], rotates[41]); SubOp( 2,13); 
    XorOp( 9, 0); b[ 9] = right_rot(b[ 9], rotates[40]); SubOp( 0, 9);
                                                     
    XorOp(15,14); b[15] = right_rot(b[15], rotates[39]); SubOp(14,15); 
    XorOp(13,12); b[13] = right_rot(b[13], rotates[38]); SubOp(12,13); 
    XorOp(11,10); b[11] = right_rot(b[11], rotates[37]); SubOp(10,11); 
    XorOp( 9, 8); b[ 9] = right_rot(b[ 9], rotates[36]); SubOp( 8, 9); 
    XorOp( 7, 6); b[ 7] = right_rot(b[ 7], rotates[35]); SubOp( 6, 7); 
    XorOp( 5, 4); b[ 5] = right_rot(b[ 5], rotates[34]); SubOp( 4, 5); 
    XorOp( 3, 2); b[ 3] = right_rot(b[ 3], rotates[33]); SubOp( 2, 3); 
    XorOp( 1, 0); b[ 1] = right_rot(b[ 1], rotates[32]); SubOp( 0, 1);
                                                     
    XorOp( 7,12); b[ 7] = right_rot(b[ 7], rotates[31]); SubOp(12, 7); 
    XorOp( 3,10); b[ 3] = right_rot(b[ 3], rotates[30]); SubOp(10, 3); 
    XorOp( 5, 8); b[ 5] = right_rot(b[ 5], rotates[29]); SubOp( 8, 5); 
    XorOp( 1,14); b[ 1] = right_rot(b[ 1], rotates[28]); SubOp(14, 1); 
    XorOp( 9, 4); b[ 9] = right_rot(b[ 9], rotates[27]); SubOp( 4, 9); 
    XorOp(13, 6); b[13] = right_rot(b[13], rotates[26]); SubOp( 6,13); 
    XorOp(11, 2); b[11] = right_rot(b[11], rotates[25]); SubOp( 2,11); 
    XorOp(15, 0); b[15] = right_rot(b[15], rotates[24]); SubOp( 0,15);
                                                     
    XorOp( 9,10); b[ 9] = right_rot(b[ 9], rotates[23]); SubOp(10, 9);
    XorOp(11, 8); b[11] = right_rot(b[11], rotates[22]); SubOp( 8,11);
    XorOp(13,14); b[13] = right_rot(b[13], rotates[21]); SubOp(14,13);
    XorOp(15,12); b[15] = right_rot(b[15], rotates[20]); SubOp(12,15);
    XorOp( 1, 6); b[ 1] = right_rot(b[ 1], rotates[19]); SubOp( 6, 1);
    XorOp( 3, 4); b[ 3] = right_rot(b[ 3], rotates[18]); SubOp( 4, 3);
    XorOp( 5, 2); b[ 5] = right_rot(b[ 5], rotates[17]); SubOp( 2, 5);
    XorOp( 7, 0); b[ 7] = right_rot(b[ 7], rotates[16]); SubOp( 0, 7);
                                                     
    XorOp( 1, 8); b[ 1] = right_rot(b[ 1], rotates[15]); SubOp( 8, 1);
    XorOp( 5,14); b[ 5] = right_rot(b[ 5], rotates[14]); SubOp(14, 5);
    XorOp( 3,12); b[ 3] = right_rot(b[ 3], rotates[13]); SubOp(12, 3);
    XorOp( 7,10); b[ 7] = right_rot(b[ 7], rotates[12]); SubOp(10, 7);
    XorOp(15, 4); b[15] = right_rot(b[15], rotates[11]); SubOp( 4,15);
    XorOp(11, 6); b[11] = right_rot(b[11], rotates[10]); SubOp( 6,11);
    XorOp(13, 2); b[13] = right_rot(b[13], rotates[ 9]); SubOp( 2,13);
    XorOp( 9, 0); b[ 9] = right_rot(b[ 9], rotates[ 8]); SubOp( 0, 9);
                                                     
    XorOp(15,14); b[15] = right_rot(b[15], rotates[ 7]); SubOp(14,15);
    XorOp(13,12); b[13] = right_rot(b[13], rotates[ 6]); SubOp(12,13);
    XorOp(11,10); b[11] = right_rot(b[11], rotates[ 5]); SubOp(10,11);
    XorOp( 9, 8); b[ 9] = right_rot(b[ 9], rotates[ 4]); SubOp( 8, 9);
    XorOp( 7, 6); b[ 7] = right_rot(b[ 7], rotates[ 3]); SubOp( 6, 7);
    XorOp( 5, 4); b[ 5] = right_rot(b[ 5], rotates[ 2]); SubOp( 4, 5);
    XorOp( 3, 2); b[ 3] = right_rot(b[ 3], rotates[ 1]); SubOp( 2, 3);
    XorOp( 1, 0); b[ 1] = right_rot(b[ 1], rotates[ 0]); SubOp( 0, 1);
    }
                                                                                          
#ifdef __BORLANDC__
#pragma argsused
#endif
void rev_cycle_8_or_r8(Word *b, const u08b *rotates, int rounds)                             
    {                                                                                     
    XorOp( 3, 4); b[ 3] = right_rot(b[ 3], rotates[31]); SubOp( 4, 3);
    XorOp( 5, 2); b[ 5] = right_rot(b[ 5], rotates[30]); SubOp( 2, 5);
    XorOp( 7, 0); b[ 7] = right_rot(b[ 7], rotates[29]); SubOp( 0, 7);
    XorOp( 1, 6); b[ 1] = right_rot(b[ 1], rotates[28]); SubOp( 6, 1);

    XorOp( 7, 2); b[ 7] = right_rot(b[ 7], rotates[27]); SubOp( 2, 7);
    XorOp( 5, 0); b[ 5] = right_rot(b[ 5], rotates[26]); SubOp( 0, 5);
    XorOp( 3, 6); b[ 3] = right_rot(b[ 3], rotates[25]); SubOp( 6, 3);
    XorOp( 1, 4); b[ 1] = right_rot(b[ 1], rotates[24]); SubOp( 4, 1);

    XorOp( 3, 0); b[ 3] = right_rot(b[ 3], rotates[23]); SubOp( 0, 3);
    XorOp( 5, 6); b[ 5] = right_rot(b[ 5], rotates[22]); SubOp( 6, 5);
    XorOp( 7, 4); b[ 7] = right_rot(b[ 7], rotates[21]); SubOp( 4, 7);
    XorOp( 1, 2); b[ 1] = right_rot(b[ 1], rotates[20]); SubOp( 2, 1);

    XorOp( 7, 6); b[ 7] = right_rot(b[ 7], rotates[19]); SubOp( 6, 7);
    XorOp( 5, 4); b[ 5] = right_rot(b[ 5], rotates[18]); SubOp( 4, 5);
    XorOp( 3, 2); b[ 3] = right_rot(b[ 3], rotates[17]); SubOp( 2, 3);
    XorOp( 1, 0); b[ 1] = right_rot(b[ 1], rotates[16]); SubOp( 0, 1);

    XorOp( 3, 4); b[ 3] = right_rot(b[ 3], rotates[15]); SubOp( 4, 3);
    XorOp( 5, 2); b[ 5] = right_rot(b[ 5], rotates[14]); SubOp( 2, 5);
    XorOp( 7, 0); b[ 7] = right_rot(b[ 7], rotates[13]); SubOp( 0, 7);
    XorOp( 1, 6); b[ 1] = right_rot(b[ 1], rotates[12]); SubOp( 6, 1);

    XorOp( 7, 2); b[ 7] = right_rot(b[ 7], rotates[11]); SubOp( 2, 7);
    XorOp( 5, 0); b[ 5] = right_rot(b[ 5], rotates[10]); SubOp( 0, 5);
    XorOp( 3, 6); b[ 3] = right_rot(b[ 3], rotates[ 9]); SubOp( 6, 3);
    XorOp( 1, 4); b[ 1] = right_rot(b[ 1], rotates[ 8]); SubOp( 4, 1);

    XorOp( 3, 0); b[ 3] = right_rot(b[ 3], rotates[ 7]); SubOp( 0, 3);
    XorOp( 5, 6); b[ 5] = right_rot(b[ 5], rotates[ 6]); SubOp( 6, 5);
    XorOp( 7, 4); b[ 7] = right_rot(b[ 7], rotates[ 5]); SubOp( 4, 7);
    XorOp( 1, 2); b[ 1] = right_rot(b[ 1], rotates[ 4]); SubOp( 2, 1);

    XorOp( 7, 6); b[ 7] = right_rot(b[ 7], rotates[ 3]); SubOp( 6, 7);
    XorOp( 5, 4); b[ 5] = right_rot(b[ 5], rotates[ 2]); SubOp( 4, 5);
    XorOp( 3, 2); b[ 3] = right_rot(b[ 3], rotates[ 1]); SubOp( 2, 3);
    XorOp( 1, 0); b[ 1] = right_rot(b[ 1], rotates[ 0]); SubOp( 0, 1);
    }                                                                                     
                                                                                          
#ifdef __BORLANDC__
#pragma argsused
#endif
void rev_cycle_4_or_r8(Word *b, const u08b *rotates, int rounds)                             
    {                                                                                     
    XorOp( 1, 2); b[ 1] = right_rot(b[ 1], rotates[15]); SubOp( 2, 1);
    XorOp( 3, 0); b[ 3] = right_rot(b[ 3], rotates[14]); SubOp( 0, 3);

    XorOp( 3, 2); b[ 3] = right_rot(b[ 3], rotates[13]); SubOp( 2, 3);
    XorOp( 1, 0); b[ 1] = right_rot(b[ 1], rotates[12]); SubOp( 0, 1);

    XorOp( 1, 2); b[ 1] = right_rot(b[ 1], rotates[11]); SubOp( 2, 1);
    XorOp( 3, 0); b[ 3] = right_rot(b[ 3], rotates[10]); SubOp( 0, 3);

    XorOp( 3, 2); b[ 3] = right_rot(b[ 3], rotates[ 9]); SubOp( 2, 3);
    XorOp( 1, 0); b[ 1] = right_rot(b[ 1], rotates[ 8]); SubOp( 0, 1);

    XorOp( 1, 2); b[ 1] = right_rot(b[ 1], rotates[ 7]); SubOp( 2, 1);
    XorOp( 3, 0); b[ 3] = right_rot(b[ 3], rotates[ 6]); SubOp( 0, 3);

    XorOp( 3, 2); b[ 3] = right_rot(b[ 3], rotates[ 5]); SubOp( 2, 3);
    XorOp( 1, 0); b[ 1] = right_rot(b[ 1], rotates[ 4]); SubOp( 0, 1);

    XorOp( 1, 2); b[ 1] = right_rot(b[ 1], rotates[ 3]); SubOp( 2, 1);
    XorOp( 3, 0); b[ 3] = right_rot(b[ 3], rotates[ 2]); SubOp( 0, 3);

    XorOp( 3, 2); b[ 3] = right_rot(b[ 3], rotates[ 1]); SubOp( 2, 3);
    XorOp( 1, 0); b[ 1] = right_rot(b[ 1], rotates[ 0]); SubOp( 0, 1);
    }


/* test that fwd and rev ciphers are truly inverses */
void InverseChecks(void)
    {
    uint  i,j,k,wCnt,tstCnt;
    int   r,rN;
    Block pt,ct,xt;
    u08b  rots[MAX_ROTS_PER_CYCLE];
    uint  TEST_CNT = (sizeof(size_t) == 8) ? 64 : 8;

    cycle_func *fwd;
    cycle_func *rev;
    cycle_func *fwd_or;
    cycle_func *fwd_or_rN;
#ifdef TEST_OR
    cycle_func *rev_or;
    cycle_func *rev_or_rN;
#endif
    
    Rand_Init(0);
    for (wCnt=4;wCnt<=MAX_WORDS_PER_BLK;wCnt *= 2)
        {
        switch (wCnt)
            {
            case  4: fwd       = fwd_cycle_4        ; rev       = rev_cycle_4        ;
                     fwd_or    = fwd_cycle_4_or     ; fwd_or_rN = fwd_cycle_4_or_r8  ; break;
            case  8: fwd       = fwd_cycle_8        ; rev       = rev_cycle_8        ;
                     fwd_or    = fwd_cycle_8_or     ; fwd_or_rN = fwd_cycle_8_or_r8  ; break;
            default: fwd       = fwd_cycle_16       ; rev       = rev_cycle_16       ; 
                     fwd_or    = fwd_cycle_16_or    ; fwd_or_rN = fwd_cycle_16_or_r9 ; break;
            }
#ifdef TEST_OR
        switch (wCnt)
            {
            case  4: rev_or_rN = rev_cycle_4_or_r8  ; rev_or    = rev_cycle_4_or     ; break;
            case  8: rev_or_rN = rev_cycle_8_or_r8  ; rev_or    = rev_cycle_8_or     ; break;
            default: rev_or_rN = rev_cycle_16_or_r9 ; rev_or    = rev_cycle_16_or    ; break;
            }
#endif
        for (tstCnt=0;tstCnt<TEST_CNT;tstCnt++)
            {
            if (tstCnt == 0)
                {
                memset(pt.x,0,sizeof(pt));      /* make the first test simple, for debug */
                pt.x[0]++;
                }
            else
                RandBytes(pt.x,wCnt*sizeof(pt.x[0]));

            RandBytes(rots,sizeof(rots));       /* use random rotation constants */
            for (i=0;i<MAX_ROTS_PER_CYCLE;i++)
                rots[i] &= (BITS_PER_WORD-1);
            for (r=1;r<32;r++)
                {
                ct=pt;
                rev(ct.x,rots,r);
                fwd(ct.x,rots,r);
                if (memcmp(pt.x,ct.x,wCnt*sizeof(pt.x[0])))
                    {
                    printf("Inverse failure: #%03d: wCnt=%d. r=%2d",tstCnt,wCnt,r);
                    exit(8);
                    }
                fwd(ct.x,rots,r);
                rev(ct.x,rots,r);
                if (memcmp(pt.x,ct.x,wCnt*sizeof(pt.x[0])))
                    {
                    printf("Inverse failure: #%03d: wCnt=%d. r=%2d",tstCnt,wCnt,r);
                    exit(8);
                    }
#ifdef TEST_OR
                fwd_or(ct.x,rots,r);
                rev   (ct.x,rots,r);
                if (memcmp(pt.x,ct.x,wCnt*sizeof(pt.x[0])))
                    {
                    printf("Inverse failure (fwd_or): #%03d: wCnt=%d. r=%2d",tstCnt,wCnt,r);
                    exit(8);
                    }
                fwd   (ct.x,rots,r);
                rev_or(ct.x,rots,r);
                if (memcmp(pt.x,ct.x,wCnt*sizeof(pt.x[0])))
                    {
                    printf("Inverse failure (rev_or): #%03d: wCnt=%d. r=%2d",tstCnt,wCnt,r);
                    exit(8);
                    }                
                if (r != ((wCnt == 16) ? 9 : 8))
                    continue;
                fwd_or_rN(ct.x,rots,r);
                rev      (ct.x,rots,r);
                if (memcmp(pt.x,ct.x,wCnt*sizeof(pt.x[0])))
                    {
                    printf("Inverse failure (fwd_or_rN): #%03d: wCnt=%d. r=%2d",tstCnt,wCnt,r);
                    exit(8);
                    }                
                fwd      (ct.x,rots,r);
                rev_or_rN(ct.x,rots,r);
                if (memcmp(pt.x,ct.x,wCnt*sizeof(pt.x[0])))
                    {
                    printf("Inverse failure (rev_or_rN): #%03d: wCnt=%d. r=%2d",tstCnt,wCnt,r);
                    exit(8);
                    }                
#else
                /* validate that "quick" Hamming weight checks are ok, using OR */
                for (i=0;i<wCnt;i++)
                    {
                    memset(ct.x,0,sizeof(ct.x));
                    ct.x[i]=1;
                    fwd_or(ct.x,rots,r);
                    for (j=1;j<64;j++)
                        {
                        memset(xt.x,0,sizeof(xt.x));
                        xt.x[i]=((u64b) 1) << j;
                        fwd_or(xt.x,rots,r);
                        for (k=0;k<wCnt;k++)
                            if (left_rot(ct.x[k],j) != xt.x[k])
                                {
                                printf("Quick HW check failure: blk=%4d bits. r=%d. j=%d",wCnt*64,r,j);
                                exit(2);
                                }
                        }
                    }
#endif
                }
            }
        /* test the "hard coded" versions against variable versions of OR routines */
        for (tstCnt=0;tstCnt<TEST_CNT;tstCnt++)
            {
            RandBytes(rots,sizeof(rots));
            for (i=0;i<MAX_ROTS_PER_CYCLE;i++)
                rots[i] &= (BITS_PER_WORD-1);
            rN = (wCnt == 16) ? 9 : 8;
            for (i=0;i<wCnt*64;i++)
                {
                memset(pt.x,0,sizeof(pt));
                pt.x[i / 64] = ((u64b) 1) << (i % 64);
                ct=pt;
                xt=pt;
                fwd_or   (ct.x,rots,rN);
                fwd_or_rN(xt.x,rots,rN);
                if (memcmp(xt.x,ct.x,wCnt*sizeof(xt.x[0])))
                    {
                    printf("OR failure: #%03d: wCnt=%d. i=%2d",tstCnt,wCnt,i);
                    exit(8);
                    }
                }
            }
        }
    }

/* count the bits set in the word */
uint HammingWeight(Word x)
    {
#if BITS_PER_WORD == 64
#define MK_64(w32) ((w32) | (((u64b) w32) << 32))
    x = (x & MK_64(0x55555555)) + ((x >> 1) & MK_64(0x55555555));
    x = (x & MK_64(0x33333333)) + ((x >> 2) & MK_64(0x33333333));
    x = (x & MK_64(0x0F0F0F0F)) + ((x >> 4) & MK_64(0x0F0F0F0F));
    x = (x & MK_64(0x00FF00FF)) + ((x >> 8) & MK_64(0x00FF00FF));
    x = (x & MK_64(0x0000FFFF)) + ((x >>16) & MK_64(0x0000FFFF));
    x = (x & MK_64(0x000000FF)) + ((x >>32) & MK_64(0x000000FF));
#else
    x = (x & 0x55555555) + ((x >> 1) & 0x55555555);
    x = (x & 0x33333333) + ((x >> 2) & 0x33333333);
    x = (x & 0x0F0F0F0F) + ((x >> 4) & 0x0F0F0F0F);
    x = (x & 0x00FF00FF) + ((x >> 8) & 0x00FF00FF);
    x = (x & 0x0000FFFF) + ((x >>16) & 0x000000FF);
#endif
    return (uint) x;
    }

/* test the rotation set for minimum hamming weight >= minHW */
/*   [try to do it fast: rely on rotational symmetry using OR, */
/*    and do an early exit if hamming weight is too low] */
int Cycle_Min_HW(uint rounds, const u08b *rotList,uint minHW,uint verMask)
    {
    uint    i,j,v,hw,hMin;
    u08b    rots[MAX_ROTS_PER_CYCLE];
    Block   b;

    hMin = BITS_PER_WORD;
    for (v=0;v<MAX_ROT_VER_CNT;v++)
        {
        if ((verMask & (1 << v)) == 0)
            continue;
        if (v & 1)
            { /* do it on the "half-cycle" */
            for (i=0;i<rotsPerCycle;i++)
                {
                rots[i] = rotList[(i >= rotsPerCycle/2) ? i - rotsPerCycle/2 : i + rotsPerCycle/2];
                }
            }
        else
            memcpy(rots,rotList,rotsPerCycle*sizeof(rots[0]));
        for (i=0;i<wordsPerBlock;i++)
            {
            memset(b.x,0,wordsPerBlock*sizeof(b.x[0]));
            b.x[i] = 1;                     /* test propagation into one word */
            if (minHW)
                {       /* use the "_rN" versions for speed */
                if (v & 2)
                    rev_cycle_or_rN(b.x,rots,(int)rounds);
                else
                    fwd_cycle_or_rN(b.x,rots,(int)rounds);
                }
            else
                {       /* saturation check */
                if (v & 2)
                    rev_cycle_or   (b.x,rots,(int)rounds);
                else
                    fwd_cycle_or   (b.x,rots,(int)rounds);
                }
            for (j=0;j<wordsPerBlock;j++)
                {
                hw = HammingWeight(b.x[j]);
                if (minHW > hw)
                    return 0;               /* stop if this isn't good enough */
                if (hMin  > hw)             /* else keep track of min */
                    hMin  = hw;
                }
            }
        }
    return hMin;
    }

/* compute/set the minimum hamming weight of the rotation set */
/*   [more thorough check than Cycle_Min_HW] */
uint Set_Min_hw_OR(rSearchRec *r,uint verMask)
    { /* int rounds,const u08b *rotList,uint *vList) */
    uint  i,j,v,hw,hwMin;
    u08b  rots[MAX_ROTS_PER_CYCLE];
    Block b;

    hwMin = BITS_PER_WORD;
    for (v=0;v<MAX_ROT_VER_CNT;v++)
        {
        r->hw_OR[v] = BITS_PER_WORD;
        if ((verMask & (1 << v)) == 0)
            continue;
        if (v & 1)
            { /* do it on the "half-cycle" */
            for (i=0;i<rotsPerCycle;i++)
                {
                rots[i] = r->rotList[(i >= rotsPerCycle/2) ? i - rotsPerCycle/2 : i + rotsPerCycle/2];
                }
            }
        else
            memcpy(rots,r->rotList,rotsPerCycle*sizeof(rots[0]));
        for (i=0;i<bitsPerBlock;i+=BITS_PER_WORD)
            {
            memset(b.x,0,sizeof(b.x));
            b.x[i/BITS_PER_WORD] |= (((u64b) 1) << (i%BITS_PER_WORD));
            if (v & 2)
                rev_cycle_or(b.x,rots,(int)r->rounds);
            else
                fwd_cycle_or(b.x,rots,(int)r->rounds);
            for (j=0;j<wordsPerBlock;j++)
                {
                hw = HammingWeight(b.x[j]);
                if (hwMin > hw)
                    hwMin = hw;
                if (r->hw_OR[v] > hw)
                    r->hw_OR[v] = hw;
                }
            }
        }
    return hwMin;
    }

/* show how the Hamming weight varies as a function of # rounds */
void Show_HW_rounds(const u08b *rotates)
    {
    uint i,r,minHW,hw[4];

    for (r=4;r<12;r++)
        {  
        minHW = bitsPerBlock;
        for (i=0;i<4;i++)
            {
            hw[i]=Cycle_Min_HW(r,rotates,0,1 << i);
            if (minHW > hw[i])
                minHW = hw[i];
            }
        printf("%2d rounds: minHW = %2d  [",r,minHW);
        for (i=0;i<4;i++)   /* show the different "versions" */
            printf(" %2d",hw[i]);
        printf(" ]\n");
        }
    }

/* read rotations value from file */
const u08b *get_rotation_file(const char *rfName)
    {
    enum   { MAX_LINE = 512 };
    char   line[MAX_LINE+4];
    uint   i,rotVal;
    static FILE *rf=NULL;
    static u08b rotates[MAX_ROTS_PER_CYCLE];
    static uint rotShow=0;
    static uint rotCnt =0;
/**** sample format: 
+++++++++++++ Preliminary results: sampleCnt =  1024, block =  256 bits
rMin = 0.425. #079C[*21] [CRC=D89E7C72. hw_OR=62. cnt= 1024. blkSize= 256]          
   46   52
   21   38
   13   13
   20   27
   14   40
   43   26
   35   29
   19   63
rMin = 0.425. #0646[*17] [CRC=527174F3. hw_OR=61. cnt= 1024. blkSize= 256]          
   26   24
   50   48
   40   25
   36   55
   10   20
   10   16
   60   55
   18    7
...
****/
    if (rf == NULL)
        {
        if (rfName[0] == '+')
            {
            rotShow = 1;
            rfName++;
            }
        rf = fopen(rfName,"rt");
        if (rf == NULL)
            {
            printf("Unable to open rotation file '%s'",rfName);
            exit(2);
            }
        rotCnt=0;
        for (;;)        /* skip to "preliminary results" section */
            {
            line[0]=0;
            if (fgets(line,sizeof(line)-4,rf) == NULL || line[0] == 0)
                {
                fclose(rf);                 /* eof --> stop */
                rf = NULL;
                return NULL;
                }
            /* check for the header */
            if (line[0] != '+' || line[1] != '+' || line[2] != '+' ||
                strstr(line,"reliminary results:") == NULL)
                continue;
            /* now check for the correct block size */
            for (i=strlen(line);i;i--)      /* start at eol and look backwards */
                if (line[i-1] == '=')       /* check for '=' sign for block size */
                    break;
            if (i > 0 && sscanf(line+i,"%u bits",&i) == 1 && i == bitsPerBlock)
                break;
            }
        }
    /* now at the rMin line */
    line[0]=0;
    if (fgets(line,sizeof(line)-4,rf) == NULL || line[0] == 0 || strncmp(line,"rMin =",6))  
        {
        fclose(rf);
        rf = NULL;
        return NULL;
        }

    /* now read in all the rotation values */
    for (i=0;i<rotsPerCycle;i++)
        {
        if (fscanf(rf,"%u",&rotVal) != 1 || rotVal >= bitsPerBlock)
            {   /* Invalid rotation value */
            fclose(rf);
            rf = NULL;
            return NULL;
            }
        rotates[i] = (u08b) rotVal;
        }
    fgets(line,sizeof(line)-4,rf);          /* skip eol */
    
    if (rotShow)
        {   /* show the hamming weight profile */
        printf("\n:::::::::::\n");
        printf("Rot #%02d [%4d-bit blocks] read from file '%s':\n",rotCnt,bitsPerBlock,rotFileName);
        for (i=0;i<rotsPerCycle;i++)
            printf("%4d%s",rotates[i],((i+1)%(wordsPerBlock/2))?"":"\n");
        Show_HW_rounds(rotates);     /* show HW results for different numbers of rounds */
        printf(":::::::::::\n");
        }
    rotCnt++;
    return rotates;
    }

/* generate a randomly chosen set of rotation constants of given minimum hamming weight (using OR) */
/* (this may take a while, depending on minHW,rounds) */
const u08b *get_rotation(uint minHW_or,uint rounds,uint minOffs,uint flags,uint rotNum,uint maxSatRnds)
    {
    static  u64b rCnt    = 1;
    static  u64b rCntOK  = 0;
    static  uint hwBase  = 0;
    uint    i,j,k,m,n,b,hw,showCnt,needShow,q,qMask;
    static  u08b rotates[MAX_ROTS_PER_CYCLE];   /*  last generated rotation set */
    
    if (rotFileName)                            /* get from search results file? */
        return get_rotation_file(rotFileName);
    
    qMask   = ((wordsPerBlock/2)-1) & dupRotMask;  /* filter for dup rotate counts in the same round? */
    showCnt = ((sizeof(size_t) == 4) ? 943211 : 9432111);   /* 64-bit CPUs display less frequently */
    showCnt = showCnt / (wordsPerBlock/4);
    for (needShow=1;;rCnt++)
        {
        if (needShow || (rCnt % showCnt) == 0)  /* show progress the first time, then periodically */
            {
            if (flags & CHK_FLG_STDERR)
                fprintf(stderr,"\r%16uK [%4u = %9.7f @ %2d.%02d].#%04X \r",
                            (u32b)(rCnt/1000),(u32b)rCntOK,rCntOK/(double)rCnt,
                             minHW_or-minOffs,wordsPerBlock,rotNum);
            needShow=0;
            }

        if (hwBase == 0)
            {   /* pick a rotation set at random */
            RandBytes(rotates,rotsPerCycle*sizeof(rotates[0]));
            for (i=0;i<rotsPerCycle;i++)
                {
                rotates[i] &= (BITS_PER_WORD-1);
                for (;;)
                    {   /* filter out unapproved rotation sets here */
                    if (RotCntGood(rotates[i]))
                        {
                        for (q=i & ~qMask;q < i;q++)    /* check for dups in the same round */
                            if (rotates[i] == rotates[q])
                                break;
                        if (q >= i)                     /* no dup, value ok, so this value is ok */
                            break;  
                        }
                    RandBytes(rotates+i,1);             /* try a new value */
                    rotates[i] &= (BITS_PER_WORD-1);
                    }
                }
            hw = Cycle_Min_HW(rounds,rotates,minHW_or-minOffs,rotVerMask);
            if (hw == 0)                /* did we get close? */
                continue;
            rCntOK++;
            hwBase = hw;
            if (hw >= minHW_or)
                if (Cycle_Min_HW(maxSatRnds, rotates,0,rotVerMask) == BITS_PER_WORD)
                    {
                    return rotates;
                    }
            }

        /* Try nearby values (hill-climbing) to see if hw gets better */
        /*      -- exhaustively try all possible values of pairs of changes */
        for (m=0;m<rotsPerCycle;m++)
        for (b=0;b<BITS_PER_WORD ;b++)
            {
            k = rotsPerCycle-1-m;           /* work backwards, since we're already close */
            rotates[k]++;
            rotates[k] &= (BITS_PER_WORD-1);
            if (RotCnt_Bad(rotates[k]))
                continue;
            for (q=k | qMask;q > k;q--)    /* check for dups in the same round */
                if (rotates[k] == rotates[q])
                    break;
            if (q > k)      
                continue;
            if (b == 0 && flags & CHK_FLG_STDERR)
                fprintf(stderr,"\r%2d  \r",k);
            for (i=m+1;i<rotsPerCycle;i++)
                {
                n = rotsPerCycle-1-i;   /* work backwards */
                for (j=0;j<BITS_PER_WORD;j++)
                    {
                    rotates[n]++;       /* try another rotation value */
                    rotates[n] &= (BITS_PER_WORD-1);
                    if (RotCnt_Bad(rotates[n]))
                        continue;
                    for (q=n | qMask;q > n;q--)    /* check for dups in the same round */
                        if (rotates[n] == rotates[q])
                            break;
                    if (q > n)      
                        continue;  
                    k  = (minHW_or > hwBase) ? minHW_or : hwBase;
                    hw = Cycle_Min_HW(rounds,rotates,k,rotVerMask);
                    if (hw > hwBase)
                        if (Cycle_Min_HW(maxSatRnds, rotates,0,rotVerMask) == BITS_PER_WORD)
                            {   /* must improve hw to accept this new rotation set */
                            assert(hw >= minHW_or);
                            hwBase = hw;
                            return rotates;
                            }
                    }
                }
            }
        hwBase = 0;                     /* back to random  */
        }
    }

/* display a search record result */
void ShowSearchRec(FILE *f,const rSearchRec *r,uint showMode)
    {
    uint  i,j,n,hwMin;
    const char *s;
    char  fStr[100];
    char  c = ' ';

    hwMin=BITS_PER_WORD;
    for (i=0;i<MAX_ROT_VER_CNT;i++)
        if (hwMin > r->hw_OR[i])
            hwMin = r->hw_OR[i];

    switch (showMode)
        {
        case SHOW_ROTS_PRELIM: s = ".prelim"; break;
        case SHOW_ROTS_H:      s = ".format"; break;
        default:          
            if (showMode < SHOW_ROTS_FINAL)
                s = "";
            else
                {
                n = showMode - SHOW_ROTS_FINAL;
                if (n == MAX_BEST_CNT-1)
                    c = '-';  /* mark the best one */
                sprintf(fStr,".final:%02d",n);
                s=fStr;
                }
            break;
        }

    fprintf(f,"rMin = %5.3f.%c#%04X[*%02d] [CRC=%08X. hw_OR=%2d. cnt=%5d. blkSize=%4u]%-10s\n",
            r->rWorst/(double)r->sampleCnt,c,r->rotNum,r->rotScale,
            r->CRC,hwMin,r->sampleCnt,bitsPerBlock,s);

    switch (showMode)
        {
        case NO_ROTS:
            break;
        case SHOW_ROTS_H: /* format for "skein.h" */
            for (j=n=0;j<rotsPerCycle/(wordsPerBlock/2);j++)
                {
                fprintf(f,"   ");
                for (i=0;i<wordsPerBlock/2;i++)
                    {
                    fprintf(f,(wordsPerBlock == 16)?" R%04d":" R_%03d",wordsPerBlock*64);
                    fprintf(f,"_%d_%d=%2d,",j,i,r->rotList[n++]);
                    }
                fprintf(f,"\n");
                }
            break;
        default:
            for (i=0;i<rotsPerCycle;i++)
                fprintf(f,"   %2d%s",r->rotList[i],((i+1)%(wordsPerBlock/2))?"":"\n");
            break;
        }
    }

/* compute Skein differentials for a given rotation set */
uint CheckDifferentials(rSearchRec *r,rSearchRec *rBest,uint flags,uint verMask)
    {
    enum  { HIST_BINS =  20, QUICK_CHECK_CNT = 32 };

    uint    i,j,k,v,n,d,dMax,minCnt,maxCnt,vCnt;
    uint    rMin,rMax,hwMin,hwMax,hw,rMinCnt,rMaxCnt;
    uint    hist[HIST_BINS+1];
    u08b    rots[MAX_ROTS_PER_CYCLE];
    u64b    sum,totSum,w;
    double  fSum,fSqr,x,var,denom;
    static  uint onesCnt[MAX_BITS_PER_BLK][MAX_BITS_PER_BLK];  /* too big for the stack --> static :-( */
    uint    *onesCntPtr;
    struct
        {
        Block pt,ct;
        } a,b;

    r->rWorst = r->sampleCnt;
    dMax = 1u << r->diffBits;

    for (v=vCnt=0;v < MAX_ROT_VER_CNT; v++)  
        { /* different versions of rotation schedule, including "inverse" cipher */
        if ((verMask & (1 << v)) == 0)
            continue;
        vCnt++;     /* number of versions processed */
        if (v & 1)
            { /* do it on the "half-cycle" */
            for (i=0;i<rotsPerCycle;i++)
                {
                rots[i] = r->rotList[(i >= rotsPerCycle/2) ? i - rotsPerCycle/2 : i + rotsPerCycle/2];
                }
            }
        else
            memcpy(rots,r->rotList,rotsPerCycle*sizeof(rots[0]));
        for (d=1; d < dMax; d+=2)    /* multi-bit difference patterns (must start with a '1' bit)  */
            {
            hwMax=0;
            hwMin=bitsPerBlock+1;
            memset(onesCnt,0,sizeof(onesCnt));      /* clear stats before starting */
            memset(a.pt.x,0,wordsPerBlock*sizeof(a.pt.x[0]));   /* zero plaintext (first time only) */
            for (n=0;n<r->sampleCnt;n++)
                {
                if (n)
                    {
                    for (i=0;i<wordsPerBlock;i++)   /* generate input blocks in a portable way */
                        a.pt.x[i] = Rand64();
                    }
                a.ct = a.pt;
                if (v & 2)
                    rev_cycle(a.ct.x,rots,r->rounds);
                else
                    fwd_cycle(a.ct.x,rots,r->rounds);
                for (i=0;i<bitsPerBlock;i++)
                    {
                    b.pt = a.pt;
                    b.pt.x[i/BITS_PER_WORD] ^= left_rot((u64b)d,(i%BITS_PER_WORD));  /* inject input difference  */
                    b.ct = b.pt;
                    if (flags & CHK_FLG_DO_RAND)
                        RandBytes(b.ct.x,sizeof(b.ct.x));       /* random results as a comparison point */
                    else if (v & 2)
                        rev_cycle(b.ct.x,rots,r->rounds);       /* let Skein do the mixing */
                    else
                        fwd_cycle(b.ct.x,rots,r->rounds);       /* let Skein do the mixing */
                    for (j=hw=0;j<wordsPerBlock;j++)
                        {
                        w = b.ct.x[j] ^ a.ct.x[j];              /* xor difference in ciphertext word */
                        onesCntPtr = &(onesCnt[i][j*BITS_PER_WORD]);
                        for (k=0;k<BITS_PER_WORD;k+=4)
                            {   /* unroll the inner loop a bit */
                            onesCntPtr[k+0] += ((u32b) w&1); hw += ((u32b) w&1); w >>= 1;
                            onesCntPtr[k+1] += ((u32b) w&1); hw += ((u32b) w&1); w >>= 1;
                            onesCntPtr[k+2] += ((u32b) w&1); hw += ((u32b) w&1); w >>= 1;
                            onesCntPtr[k+3] += ((u32b) w&1); hw += ((u32b) w&1); w >>= 1;
                            }
                        }
                    if (hwMin > hw) hwMin = hw;
                    if (hwMax < hw) hwMax = hw;
                    }
                if (n == QUICK_CHECK_CNT && d == 1 && (flags & CHK_FLG_QUICK_EXIT))
                    {   /* quick exit if not even close to random looking after a few samples */
                    for (i=0;i<bitsPerBlock;i++)
                    for (j=0;j<bitsPerBlock;j++)
                        {
                        if (onesCnt[i][j] < 2)
                            {
                            /** Since an ideal random function has prob=0.5 each for input/output bit 
                             ** pair, the expected distribution of onesCnt[i][j] is binomial. 
                             ** Thus, at this point, the probability of onesCnt[i][j] < 2 is:
                             **     (1+QUICK_CHECK_CNT/2)/(2**QUICK_CHECK_CNT)
                             ** For QUICK_CHECK_CNT == 32, this is about 2**(-27), so when we see
                             ** such an occurrence, we exit immediately to save taking a lot of stats 
                             ** just to fail later. This filter significantly speeds up the search, at
                             ** a very low probability of improperly dismissing a "good" rotation set.
                             **/
                            if (vCnt > 1)   /* show why we stopped, if we already showed something */
                                printf("%23s/* quick exit: %5.3f */\n","",onesCnt[i][j]/(double) QUICK_CHECK_CNT);
                            return r->rWorst = 0;   /* not a good result */
                            }
                        }
                    }
                }
            /* show progress when redirecting output */
            if (flags & CHK_FLG_STDERR)
                fprintf(stderr,"#%04X[*%02d].%d.%02d \r",r->rotNum,r->rotScale,v,d);

            /* now process the stats from the samples we just generated */
            memset(hist,0,sizeof(hist));
            fSum  = fSqr = 0.0;
            rMin  = ~0u;
            denom = 1 / (double) r->sampleCnt;
            totSum= rMax = rMinCnt = rMaxCnt = 0;
            for (i=0;i<bitsPerBlock;i++)
                {
                sum=maxCnt=0;
                minCnt = ~0u;
                for (j=0;j<bitsPerBlock;j++)
                    {
                    k = onesCnt[i][j];
                    if (maxCnt < k) maxCnt = k;
                    if (minCnt > k) minCnt = k;
                    sum  += k;
                    x     = k*denom;                    /* update stats for stdDev  */
                    fSum += x;
                    fSqr += x*x;
                    hist[(uint)floor(x*HIST_BINS)]++;   /* track histogram  */
                    }
                totSum += sum;
                if (rMin >  minCnt) { rMin = minCnt; rMinCnt = 0; }
                if (rMax <  maxCnt) { rMax = maxCnt; rMaxCnt = 0; }
                if (rMin == minCnt) rMinCnt++;
                if (rMax == maxCnt) rMaxCnt++;
                }
            if (r->rWorst > rMin && !(flags & CHK_FLG_DO_RAND))
                {
                r->rWorst = rMin;
                if (rMin == 0 ||
                    (rBest  && r->rWorst < (rBest->rWorst - rBest->rWorst/4)  && d == 1 && d+2 < dMax))
                    {  /* if far worse than current best, stop now (to speed up the search) */
                    if (d > 1 || vCnt > 1) /* show why we stopped, if we already showed something */
                        printf("%23s/* early exit: %5.3f vs. %5.3f */\n","",rMin*denom,rBest->rWorst*denom);
                    return r->rWorst = 0;   /* don't allow this to be added to list */
                    }
                }
            /* put out a header separation, if still needed */
            if (!r->gotHdr && !(flags & CHK_FLG_NO_HDR))
                {
                r->gotHdr=1;            /* only one header, please */
                printf("====================================================\n");
                }
            /* put out the rotation info the first time thru */
            if (d == 1 && !(flags & CHK_FLG_NO_HDR))
                {
                if (flags & CHK_FLG_DO_RAND)
                    {
                    printf("%20s\nRANDOM OUTPUT: /* useful stats for comparison to 'ideal' */\n","");
                    }
                else
                    {
                    printf("Rotation set #%04X*%02d [CRC=%08X. hw_OR=%2d. sampleCnt=%5d. block=%4d bits. v=%d]:\n",
                           r->rotNum,r->rotScale,r->CRC,r->hw_OR[v],r->sampleCnt,bitsPerBlock,v);
                    if (vCnt == 0)
                        for (i=0;i<rotsPerCycle;i++)
                            printf("   %2d%s",r->rotList[i],((i+1)%(wordsPerBlock/2))?"":"\n");
                    }
                }
            /* show some detailed results of the random test */
            printf("rnds=%2d,cnt=%5d: ",r->rounds,r->sampleCnt);
            if (flags & CHK_FLG_DO_RAND)
                printf("   RANDOM     ");
            else
                printf("Rot=#%04X[*%02d]",r->rotNum,r->rotScale);

            x  =  fSum/(bitsPerBlock*bitsPerBlock);
            var= (fSqr/(bitsPerBlock*bitsPerBlock)) - x*x;
            printf(" min=%5.3f.[%c] max=%5.3f.[%c]  hw=%3d..%3d.  avg=%7.5f. std=%6.4f.",
                   rMin*denom,(rMinCnt > 9) ? '+' : '0'+rMinCnt,
                   rMax*denom,(rMaxCnt > 9) ? '+' : '0'+rMaxCnt,
                   hwMin,hwMax,
                   (totSum*denom)/(bitsPerBlock*bitsPerBlock),sqrt(var));
            if (flags & CHK_FLG_DO_RAND)
                printf("     R ");
            else
                printf(" d=%X",(uint)d);
            if (flags & CHK_FLG_SHOW_HIST)
                { /* very wide histogram display */
                for (i=0;i<=HIST_BINS;i++)
                    if (hist[i])
                        printf(" %7.5f",hist[i]/(double)(bitsPerBlock*bitsPerBlock));
                    else
                        printf("  _     ");
                }
            printf("\n");
            fflush(stdout);
            if (flags & CHK_FLG_DO_RAND)
                break;                  /* no need to do more than one random setting per rotation set */
            }   /* for (d=1;d<dMax;d+=2) */
        }
    if (rBest && rBest->rWorst < r->rWorst && !(flags & CHK_FLG_DO_RAND))
        {
        *rBest        = *r;         /* save it for filtering future results  */
        if (flags & CHK_FLG_STDERR)
            {                       /* show progress if redirecting stdout */
            fprintf(stderr,"\r-- New max: ");
            ShowSearchRec(stderr,rBest,NO_ROTS);  
            }
        printf("-- New max: ");
        ShowSearchRec(stdout,rBest,NO_ROTS);  
        }
    return r->rWorst;
    }

/* use the CRC value as quick ID to help identify/verify rotation sets */
uint CRC32(uint h,u08b x)
    {
#define CRC_FDBK ((0x04C11DB7u >> 1) ^ 0x80000000u) /* CRC-32-IEEE-802.3 (from Wikipedia) */
    uint i;
    h ^= x; /* inject the new byte */

    for (i=0;i<8;i++)
        h = (h & 1) ? (h >> 1) ^ CRC_FDBK : (h >> 1);

    return h;
    }

/* qsort routine for search records: keep in descending order */
int Compare_SearchRec_Ascending(const void *aPtr,const void *bPtr)
    {
    uint wA = ((const rSearchRec *) aPtr)->rWorst;
    uint wB = ((const rSearchRec *) bPtr)->rWorst;

    if (wA < wB)
        return +1;
    if (wA > wB)
        return -1;
    else
        return  0;
    }

/* qsort routine for search records: keep in descending order */
int Compare_SearchRec_Descending(const void *aPtr,const void *bPtr)
    {
    uint wA = ((const rSearchRec *) aPtr)->rWorst;
    uint wB = ((const rSearchRec *) bPtr)->rWorst;

    if (wA < wB)
        return -1;
    if (wA > wB)
        return +1;
    else
        return  0;
    }

conStr ASCII_TimeDate(void)
    {
    time_t t;
    time(&t);   
    return ctime(&t);
    }

/* run a full search */
void RunSearch(testParms t)
    {
    rSearchRec  r,rBest,bestList[MAX_BEST_CNT+2];
    uint        i,n,rotCnt,rScale,bestCnt;
    const       u08b *rotPtr;
    const       char *timeStr;
    time_t      t0,t1;

    /* now set up the globals according to selected Skein blocksize */
    switch (bitsPerBlock)
        {
        case  256:
            t.rotCntMax      = (t.rotCntMax) ? t.rotCntMax    : DEFAULT_ROT_CNT_4  ;
            t.rounds         = (t.rounds)    ? t.rounds       : DEFAULT_ROUND_CNT_4;
            t.minHW_or       = (t.minHW_or)  ? t.minHW_or     :         MIN_HW_OR_4;
            t.maxSatRnds     = (t.maxSatRnds)? t.maxSatRnds   :    MAX_SAT_ROUNDS_4;
            fwd_cycle_or_rN  = (t.rounds!=8) ? fwd_cycle_4_or :  fwd_cycle_4_or_r8 ;
            rev_cycle_or_rN  = (t.rounds!=8) ? rev_cycle_4_or :  rev_cycle_4_or_r8 ;
            fwd_cycle_or     = fwd_cycle_4_or;
            rev_cycle_or     = fwd_cycle_4_or;
            fwd_cycle        = fwd_cycle_4;
            rev_cycle        = rev_cycle_4;
            break;
        case  512:
            t.rotCntMax      = (t.rotCntMax) ? t.rotCntMax    : DEFAULT_ROT_CNT_8  ;
            t.rounds         = (t.rounds)    ? t.rounds       : DEFAULT_ROUND_CNT_8;
            t.minHW_or       = (t.minHW_or)  ? t.minHW_or     :         MIN_HW_OR_8;
            t.maxSatRnds     = (t.maxSatRnds)? t.maxSatRnds   :    MAX_SAT_ROUNDS_8;
            fwd_cycle_or_rN  = (t.rounds!=8) ? fwd_cycle_8_or :  fwd_cycle_8_or_r8 ;
            rev_cycle_or_rN  = (t.rounds!=8) ? rev_cycle_8_or :  rev_cycle_8_or_r8 ;
            fwd_cycle_or     = fwd_cycle_8_or;
            rev_cycle_or     = rev_cycle_8_or;
            fwd_cycle        = fwd_cycle_8;
            rev_cycle        = rev_cycle_8;
            break;
        case 1024:
            t.rotCntMax      = (t.rotCntMax) ? t.rotCntMax    : DEFAULT_ROT_CNT_16  ;
            t.rounds         = (t.rounds)    ? t.rounds       : DEFAULT_ROUND_CNT_16;
            t.minHW_or       = (t.minHW_or)  ? t.minHW_or     :         MIN_HW_OR_16;
            t.maxSatRnds     = (t.maxSatRnds)? t.maxSatRnds   :    MAX_SAT_ROUNDS_16;
            fwd_cycle_or_rN  = (t.rounds!=9) ? fwd_cycle_16_or: fwd_cycle_16_or_r9  ;
            rev_cycle_or_rN  = (t.rounds!=9) ? rev_cycle_16_or: rev_cycle_16_or_r9  ;
            fwd_cycle_or     = fwd_cycle_16_or;
            rev_cycle_or     = rev_cycle_16_or;
            fwd_cycle        = fwd_cycle_16;
            rev_cycle        = rev_cycle_16;
            break;
        default:
            printf("Invalid block size!");
            exit(2);
        }
    wordsPerBlock =   bitsPerBlock /      BITS_PER_WORD;
    rotsPerCycle  = (wordsPerBlock / 2) * ROUNDS_PER_CYCLE;
    memset(&r      ,0,sizeof(r));
    memset(&rBest  ,0,sizeof(rBest));
    memset(bestList,0,sizeof(bestList));

    Rand_Init(t.seed0 + (((u64b) bitsPerBlock) << 32));
    printf("******************************************************************\n");
    printf("Random seed = %u. BlockSize = %d bits. sampleCnt =%6d. rounds = %2d, minHW_or=%d\n",
                       t.seed0,bitsPerBlock,t.sampleCnt,t.rounds,t.minHW_or);

    
    timeStr = ASCII_TimeDate();
    if (t.chkFlags & CHK_FLG_STDERR)
        fprintf(stderr,"Start: %s\n",timeStr);
    printf("Start: %s  \n",timeStr);
    time(&t0);
    for (rotCnt=bestCnt=0;rotCnt < t.rotCntMax;rotCnt++)
        {
        /* get a random rotation with "reasonable" hw_OR, as a starting point*/
        rotPtr = get_rotation(t.minHW_or,t.rounds,t.minOffs,t.chkFlags,rotCnt,t.maxSatRnds);
        if (rotPtr == NULL)                         /* input file done? */
            break;
        r.rounds       = t.rounds;                  /* set up the search record */
        r.sampleCnt    = t.sampleCnt;
        r.diffBits     = t.diffBits;
        r.rotNum       = rotCnt;
        r.bitsPerBlock = bitsPerBlock;
        r.gotHdr       = 0;                         /* cosmetics: do we have header for #rotCnt yet? */
        for (rScale=1;rScale<=t.rScaleMax;rScale+=2)
            {   /* try all possible odd multiples of rotation set (i.e., maintain "OR" hamming weight) */
            for (i=0,r.CRC = ~0u;i<rotsPerCycle;i++)
                {                                   /* scale the rotation values by odd number */
                r.rotList[i] = (u08b)((rotPtr[i]*rScale) % BITS_PER_WORD);
                if (RotCnt_Bad(r.rotList[i]))       /* check for disallowed rotation constants */
                    break;
                r.CRC = CRC32(r.CRC,r.rotList[i]);  /* CRC to produce "unique-ish" ID value */
                }
            if (i < rotsPerCycle)
                continue;
            r.rotScale = rScale;
            if (Set_Min_hw_OR(&r,rotVerMask) < t.minHW_or)
                continue;                           /* should never happen... */
            if (rScale == t.rScaleMax)
                t.chkFlags |=  CHK_FLG_DO_RAND;
            else
                t.chkFlags &= ~CHK_FLG_DO_RAND;
            /* don't spend time generating random differentials unless we have a header already */
            if (r.gotHdr || !(t.chkFlags & CHK_FLG_DO_RAND))
                {
                CheckDifferentials(&r,&rBest,t.chkFlags,rotVerMask);
                if (r.rWorst && !(t.chkFlags & CHK_FLG_DO_RAND) &&
                    (bestCnt < MAX_BEST_CNT || r.rWorst > bestList[bestCnt-1].rWorst))
                    {                               /* add to bestList[]? */
                    bestList[bestCnt++] = r;        /* put it at the end */
                    qsort(bestList,bestCnt,sizeof(bestList[0]),Compare_SearchRec_Ascending);  /* then sort */
                    if (bestCnt >= MAX_BEST_CNT)    /* throw away the worst one */
                        bestCnt  = MAX_BEST_CNT;
                    }
                }
            }
        if (r.gotHdr)
            printf("\n");
        }

    /* re-grade the winners using larger sampleCnt value */
    if (bestCnt)
        {
        printf("\n+++++++++++++ Preliminary results: sampleCnt = %5d, block = %4d bits\n",t.sampleCnt,bitsPerBlock);
        qsort(bestList,bestCnt,sizeof(bestList[0]),Compare_SearchRec_Descending);
        for (i=0;i<bestCnt;i++)
            ShowSearchRec(stdout,&bestList[i],SHOW_ROTS_PRELIM);

        /* re-run several times, since there will be statistical variations */
        t.sampleCnt *= 2;
        for (n=0;n<3;n++)
            {
            t.sampleCnt *= 2;
            printf("+++ Re-running differentials with sampleCnt = %d, blockSize = %4d bits.\n",t.sampleCnt,bitsPerBlock);
            for (i=0;i<bestCnt;i++)
                {   /* give some random stats as comparison */
                if (t.chkFlags & CHK_FLG_STDERR)
                    fprintf(stderr,"%20s Re-run: samples=%d, blk=%4d. RANDOM   \r","",t.sampleCnt,wordsPerBlock);
                r=bestList[0];
                CheckDifferentials(&r,NULL,(t.chkFlags & CHK_FLG_STDERR) | CHK_FLG_DO_RAND |
                                   ((i) ? CHK_FLG_NO_HDR : 0),1);   /* just do one random version */
                }
            for (i=0;i<bestCnt;i++)
                {
                if (t.chkFlags & CHK_FLG_STDERR)
                    fprintf(stderr,"%20s Re-run: samples=%d, blk=%4d. i=%2d.    \r","",t.sampleCnt,wordsPerBlock,i);
                bestList[i].gotHdr    = 0;
                bestList[i].sampleCnt = t.sampleCnt;
                CheckDifferentials(&bestList[i],NULL,t.chkFlags & CHK_FLG_STDERR,MAX_ROT_VER_MASK);
                }
            /* sort per new stats */
            if (t.chkFlags & CHK_FLG_STDERR)
                fprintf(stderr,"\r%60s\r","");
            printf("\n+++++++++++++ Final results: sampleCnt = %5d, blockSize = %4d bits\n",t.sampleCnt,bitsPerBlock);
            qsort(bestList,bestCnt,sizeof(bestList[0]),Compare_SearchRec_Descending);
            for (i=0;i<bestCnt;i++)
                ShowSearchRec(stdout,&bestList[i],SHOW_ROTS_FINAL+i);
            }
        printf("\n+++++++++++++ Formatted results: sampleCnt = %5d, blockSize = %4d bits\n",t.sampleCnt,bitsPerBlock);
        for (i=0;i<bestCnt;i++)
            {
            ShowSearchRec(stdout,&bestList[i],SHOW_ROTS_H);
            printf("\n");
            Show_HW_rounds(bestList[i].rotList);
            printf("\n");
            }
        }
    else
        printf("\n+++++++++++++ bestCnt == 0\n");

    time(&t1);
    printf("End:   %s\n",ASCII_TimeDate());
    printf("Elapsed time = %6.3f hours\n\n",(t1-t0)/(double)3600.0);
    if (t.chkFlags & CHK_FLG_STDERR)
        fprintf(stderr,"\r%50s\n","");    /* clear the screen if needed */
    fflush(stdout);
    }

void GiveHelp(void)
    {
    printf("Usage:   skein_rot_search [options]\n"
           "Options: -Bnn     = set Skein block size in bits (default=512)\n"
           "         -Cnn     = set count of random differentials taken\n"
           "         -Dnn     = set number bits of difference pattern tested (default=1)\n"
           "         -Inn     = set rotation version mask\n"
           "         -Mnn     = set max rotation scale factor\n"
           "         -Onn     = set Hamming weight offset\n"
           "         -Rnn     = set round count\n"
           "         -Snn     = set initial random seed (0 --> randomize)\n"
           "         -Xnn     = set max test rotation count\n"
           "         -Wnn     = set minimum hamming weight\n"
           "         -Znn     = set max rounds needed for saturation using OR\n"
           "         -E       = no stderr output\n"
           "         -H       = show histogram (very wide)\n"
           "         -Q       = disable quick exit in search\n"
           "         -V       = verbose mode\n"
           "         -@rFile  = read rotations from file\n"
          );
    exit(0);
    }

int main(int argc,char *argv[])
    {
    uint        i,bMin,bMax;
    testParms   t;
    uint goodRot =       2;   /* first allowed rotation value (+/-) */
    uint seed   =        1;   /* fixed random seed, unless set to zero */
    t.rounds    =        0;   /* number of Skein rounds to test */
    t.minHW_or  =        0;   /* minHW (using OR) required */
    t.minOffs   =        4;   /* heuristic used to speed up rotation search */
    t.diffBits  =        3;   /* # consecutive bits of differential inputs tested */
    t.rScaleMax =       65;   /* even --> no randomized results */
    t.sampleCnt =     1024;   /* number of differential pairs tested */
    t.rotCntMax =        0;   /* number of rotation sets tested */
    t.maxSatRnds=        0;   /* number of rounds to Hamming weight "saturation" */
    t.chkFlags  = CHK_FLG_STDERR | CHK_FLG_QUICK_EXIT;  /* default flags */

    for (i=1;i<(uint)argc;i++)
        {   /* parse command line args */
        if (argv[i][0] == '?')
            GiveHelp();
        else if (argv[i][0] == '-')
            {
            switch (toupper(argv[i][1]))
                {
                case '?': GiveHelp();                                             break;
                case 'B': bitsPerBlock  =  atoi(argv[i]+((argv[i][2]=='=')?3:2)); break;
                case 'G': goodRot       =  atoi(argv[i]+((argv[i][2]=='=')?3:2)); break;
                case 'I': rotVerMask    =  atoi(argv[i]+((argv[i][2]=='=')?3:2)); break;
                case 'S': seed          =  atoi(argv[i]+((argv[i][2]=='=')?3:2)); break;
                case 'C': t.sampleCnt   =  atoi(argv[i]+((argv[i][2]=='=')?3:2)); break;
                case 'D': t.diffBits    =  atoi(argv[i]+((argv[i][2]=='=')?3:2)); break;
                case 'M': t.rScaleMax   =  atoi(argv[i]+((argv[i][2]=='=')?3:2)); break;
                case 'O': t.minOffs     =  atoi(argv[i]+((argv[i][2]=='=')?3:2)); break;
                case 'R': t.rounds      =  atoi(argv[i]+((argv[i][2]=='=')?3:2)); break;
                case 'W': t.minHW_or    =  atoi(argv[i]+((argv[i][2]=='=')?3:2)); break;
                case 'X': t.rotCntMax   =  atoi(argv[i]+((argv[i][2]=='=')?3:2)); break;
                case 'Z': t.maxSatRnds  =  atoi(argv[i]+((argv[i][2]=='=')?3:2)); break;
                case 'H': t.chkFlags   |=  CHK_FLG_SHOW_HIST;                     break;
                case 'V': t.chkFlags   |=  CHK_FLG_VERBOSE;                       break;
                case 'E': t.chkFlags   &= ~CHK_FLG_STDERR;                        break;
                case 'Q': t.chkFlags   &= ~CHK_FLG_QUICK_EXIT;                    break;
                case '2': dupRotMask    = ~0u;                                    break;
               }
            }
        else if (argv[i][0] == '@')
            {
            rotFileName = argv[i]+1;
            t.rScaleMax = 2;
            }
        }

    InverseChecks();          /* check fwd vs. rev transforms */

    for (i=goodRot; i <= BITS_PER_WORD - goodRot ;i++)
        goodRotCntMask |= (((u64b) 1) << i);

    if (bitsPerBlock == 0)
        {
        printf("Running search for all Skein block sizes (256, 512, and 1024)\n");
        t.rounds   = 0;   /* use defaults, since otherwise it makes little sense */
        t.minHW_or = 0;
        }

    bMin = (bitsPerBlock) ? bitsPerBlock :  256;
    bMax = (bitsPerBlock) ? bitsPerBlock : 1024;

    for (bitsPerBlock=bMin;bitsPerBlock<=bMax;bitsPerBlock*=2)
        {
        t.seed0 = (seed) ? seed : (uint) time(NULL);   /* randomize based on time if -s0 is given */
        RunSearch(t);
        }
    
    return 0;
    }
