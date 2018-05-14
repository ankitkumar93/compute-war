#include <cstring>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <iostream>
#include <string>
#include <sstream>
#include <unistd.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>

#define BLKSIZ 4096

#include "lzf/lzf.h"
#include "lzo/lzo1a.h"
#include "lzo/lzo1b.h"
#include "lzo/lzo1x.h"
#include "lz4/lz4.h"
#include <bzlib.h>
#include <lzma.h>
#include <zlib.h>
#include "snappy.h"

using namespace std;
void process_directory(const char*);
void process_file(const char*);
void process(const char*);

union {
    uint64_t summary;
    struct 
    {
        unsigned   gzip   : 1;
        unsigned   bzip   : 1;
        unsigned   lzo    : 1;
        unsigned   lzf    : 1;
        unsigned   lz4    : 1;
        unsigned   lzma   : 1;
        unsigned   snappy : 1;
        unsigned   verbose : 1;
    };
} compress_flags;

void process_directory(const char *dirname)
{
    DIR* dhandle;

    cout << "Processing directory: " << dirname << endl;
    if (NULL == (dhandle = opendir(dirname)))
    {
        cerr << "Unable to open directory " << dirname << ": " << strerror(errno) << endl;
    }
    else 
    {
        struct dirent *entry;
        char fname[BUFSIZ];
        while (NULL != (entry = readdir(dhandle)))
        {
            // Skip anything beginning with a '.'.  This includes the special entries for . and ..
            if (entry->d_name[0] == '.') {
                continue;
            }
            snprintf(fname, BUFSIZ, "%s/%s", dirname, entry->d_name);
            process(fname);
        }
    }
}

void process_file(const char* fname)
{
    cout << "Processing file: " << fname << endl;
    char inbuf[BLKSIZ];
    char outbuf[BLKSIZ*2];
    int buflen;
    struct timeval tv1, tv2, tv3;


    gettimeofday(&tv1, nullptr);
    int fd = open(fname, O_RDONLY);
    if (fd < 0)
    {
        cerr << "Unable to open " << fname << ": " << strerror(errno);
        return;
    }

    int count{1};
    while((buflen = read(fd, inbuf, BLKSIZ)) > 0)
    {
        unsigned int csize, best = BLKSIZ;
        long unsigned int gzsize;
        lzo_uint lcsize;
        int ret;
        string bname;
        // Only compress full blocks.
        if (buflen < BLKSIZ) continue;
        compress_flags.verbose && cout << "Read block " << count << " of " << buflen << " bytes" << endl;

        if (compress_flags.lzf)
        {
            compress_flags.verbose && cout << "Compressing LZF..." << endl;
            gettimeofday( &tv1, nullptr);
            csize = lzf_compress(inbuf, BLKSIZ, outbuf, BLKSIZ - 1);
            gettimeofday( &tv2, nullptr);

            timersub( &tv2, &tv1, &tv3);

            cout << "lzf|" << csize << "|" << tv3.tv_usec << "|" << fname << "|" << count << endl;
            best = csize;
            bname = "lzf";
        }

        if (compress_flags.lzo)
        {
            lzo_voidp lzo_wrkmem = malloc(65536L * lzo_sizeof_dict_t);
            compress_flags.verbose && cout << "Compressing LZO1A" << endl;

            gettimeofday( &tv1, nullptr);
            csize = lzo1a_compress((lzo_bytep) inbuf, 
                                   (lzo_uint) BLKSIZ,
                                   (lzo_bytep) outbuf, 
                                   &lcsize,
                                   lzo_wrkmem);
            gettimeofday( &tv2, nullptr);
            timersub( &tv2, &tv1, &tv3);

            cout << "lzo1a|" << lcsize << "|" << tv3.tv_usec << "|" << fname << "|" << count << endl;
            if (lcsize < best)
            {
                best = lcsize;
                bname = "lzo1a";
            }

            compress_flags.verbose && cout << "Compressing LZO1X-1" << endl;
            gettimeofday( &tv1, nullptr);
            csize = lzo1x_1_compress((lzo_bytep) inbuf, 
                                     (lzo_uint) BLKSIZ,
                                     (lzo_bytep) outbuf, 
                                     &lcsize,
                                     lzo_wrkmem);
            gettimeofday( &tv2, nullptr);
            timersub( &tv2, &tv1, &tv3);

            cout << "lzo1x-1" << "|" << lcsize << "|" << tv3.tv_usec << "|" << fname << "|" << count << endl;
            if (lcsize < best)
            {
                best = lcsize;
                bname = "lzo1x-1";
            }

            compress_flags.verbose && cout << "Compressing LZO1X-999" << endl;
            gettimeofday( &tv1, nullptr);
            ret = lzo1x_999_compress((lzo_bytep) inbuf, 
                                     (lzo_uint) BLKSIZ,
                                     (lzo_bytep) outbuf, 
                                     &lcsize,
                                     lzo_wrkmem);
            gettimeofday( &tv2, nullptr);
            timersub( &tv2, &tv1, &tv3);

            cout << "lzo1x-999" << "|" << lcsize << "|" << tv3.tv_usec << "|" << fname << "|" << count << endl;
            if (lcsize < best)
            {
                best = lcsize;
                bname = "lzo1x-999";
            }
            free(lzo_wrkmem);
        }

        if (compress_flags.gzip)
        {
            gzsize = 2 * BLKSIZ;
            compress_flags.verbose && cout << "Compressing DEFLATE(GZIP)" << endl;
            gettimeofday( &tv1, nullptr);
            ret = compress((unsigned char*)outbuf, &gzsize, (unsigned char*)inbuf, BLKSIZ);
            gettimeofday( &tv2, nullptr);
            timersub( &tv2, &tv1, &tv3);

            cout << "deflate" << "|" << gzsize << "|" << tv3.tv_usec << "|" << fname << "|" << count << endl;
            if (gzsize < best)
            {
                best = gzsize;
                bname = "deflate";
            }
        }

        if (compress_flags.bzip)
        {
            csize = 2 * BLKSIZ;
            compress_flags.verbose && cout << "Compressing BZIP2" << endl;
            gettimeofday( &tv1, nullptr);
            ret = BZ2_bzBuffToBuffCompress(outbuf, &csize, inbuf, BLKSIZ, 1, 0, 30);
            gettimeofday( &tv2, nullptr);
            timersub( &tv2, &tv1, &tv3);

            cout << "bzip2" << "|" << csize << "|" << tv3.tv_usec << "|" << fname << "|" << count << endl;
            if (csize < best)
            {
                best = csize;
                bname = "bzip2";
            }
        }

        if (compress_flags.lz4)
        {
            csize = 2 * BLKSIZ;
            compress_flags.verbose && cout << "Compressing LZ4" << endl;
            gettimeofday( &tv1, nullptr);
            csize = LZ4_compress_default(inbuf, outbuf, BLKSIZ, 2*BLKSIZ);
            gettimeofday( &tv2, nullptr);
            timersub( &tv2, &tv1, &tv3);

            cout << "lz4" << "|" << csize << "|" << tv3.tv_usec << "|" << fname << "|" << count << endl;
            if (csize < best)
            {
                best = csize;
                bname = "lz4";
            }
        }

        if (compress_flags.snappy)
        {
            string output;

            compress_flags.verbose && cout << "Compressing Snappy" << endl;
            gettimeofday( &tv1, nullptr);
            snappy::Compress(inbuf, BLKSIZ, &output);
            csize = output.size();
            gettimeofday( &tv2, nullptr);
            timersub( &tv2, &tv1, &tv3);

            cout << "snappy" << "|" << csize << "|" << tv3.tv_usec << "|" << fname << "|" << count << endl;
            if (csize < best)
            {
                best = csize;
                bname = "snappy";
            }
        }

        if (compress_flags.lzma)
        {
            lzma_stream lzst = LZMA_STREAM_INIT;
            if (lzma_easy_encoder(&lzst, 9, LZMA_CHECK_CRC64) != LZMA_OK)
            {
                switch(ret)
                {
                case LZMA_MEM_ERROR: 
                    cerr << "LZMA Memory allocation failed" << endl;
                    break;
                case LZMA_OPTIONS_ERROR: 
                    cerr << "LZMA Unsupported options" << endl;
                    break;
                case LZMA_UNSUPPORTED_CHECK:
                    cerr << "LZMA integrity check unsupported" << endl;
                    break;
                default:
                    cerr << "Unknown LZMA error during initialization" << endl;
                    break;
                }
                continue;
            } 
            else 
            {
                lzst.next_in = (unsigned char*)inbuf;
                lzst.avail_in = sizeof(inbuf);
                lzst.next_out = (unsigned char*)outbuf;
                lzst.avail_out = sizeof(outbuf);

                gettimeofday(&tv1, nullptr);

                do 
                {
                    ret = lzma_code(&lzst, LZMA_FINISH);
                    if (ret != LZMA_OK)
                    {
                        switch(ret)
                        {
                        case LZMA_MEM_ERROR: 
                            cerr << "LZMA Memory allocation failed" << endl;
                            break;
                        case LZMA_OPTIONS_ERROR: 
                            cerr << "LZMA Unsupported options" << endl;
                            break;
                        case LZMA_UNSUPPORTED_CHECK:
                            cerr << "LZMA integrity check unsupported" << endl;
                            break;
                        case LZMA_STREAM_END:
                            // Normal when we've reached end of stream...
                            break;
                        default:
                            cerr << "Unknown LZMA error during coding: " << ret << endl;
                            break;
                        }
                    }
                } 
                while(lzst.avail_in > 0);
                lzma_end(&lzst);

                gettimeofday(&tv2, nullptr);
                csize = lzst.total_out;
                timersub( &tv2, &tv1, &tv3);

                cout << "lzma" << "|" << csize << "|" << tv3.tv_usec << "|" << fname << "|" << count << endl;
                if (csize < best)
                {
                    best = csize;
                    bname = "lzma";
                }
            }
        }

        count++;
    }
}

void process(const char* filename)
{
    struct stat stbuf;

    if (stat(filename, &stbuf) < 0)
    {
        cerr << "Cannot stat " << filename << ": " << strerror(errno) << endl;
        return;
    }

    if (S_ISDIR(stbuf.st_mode))
    {
        process_directory(filename);
    }
    else if (S_ISREG(stbuf.st_mode) || S_ISLNK(stbuf.st_mode))
    {
        process_file(filename);
    }
    else if (S_ISCHR(stbuf.st_mode) || S_ISBLK(stbuf.st_mode))
    {
        cerr << "Cannot process " << filename << ": Is a device file" << endl;
    }
}

int main(int argc, char** argv) 
{
    int opt, index;
    compress_flags.summary = 0;
    struct option myopts[] = 
    {
        {"bzip",    0, NULL, 'b'},
        {"gzip",    0, NULL, 'g'},
        {"lz4",     0, NULL, '4'},
        {"lzf" ,    0, NULL, 'f'},
        {"lzo",     0, NULL, 'o'},
        {"lzma",    0, NULL, 'a'},
        {"snappy",  0, NULL, 's'},
        {"verbose", 0, NULL, 'v'},
        {NULL, 0, NULL, 0}
    };

    while(-1 != (opt = getopt_long(argc, argv, "v", myopts, &index)))
    {
        cout << "Got option: " << (char)opt << endl;
        switch(opt)
        {
        case 'b': compress_flags.bzip    = true; break;
        case 'g': compress_flags.gzip    = true; break;
        case '4': compress_flags.lz4     = true; break;
        case 'f': compress_flags.lzf     = true; break;
        case 'o': 
                  lzo_init();
                  compress_flags.lzo     = true; break;
        case 'a': compress_flags.lzma    = true; break;
        case 's': compress_flags.snappy  = true; break;
        case 'v': compress_flags.verbose = true; break;
        }
    }

    cout << "LZF: " << compress_flags.lzf << endl;

    for(int i=optind; i<argc; ++i)
    {
        process(argv[i]);
    }
}
