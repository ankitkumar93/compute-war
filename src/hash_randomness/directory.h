#ifndef __DIRECTORY_H__
#define __DIRECTORY_H__

#include "shared.h"

class Directory
{
public:
    Directory(string path);
    void GetAllFiles();
    bool HasMoreFiles();
    string GetNextFile();

private:
    string mPath;
    queue<string> mFiles;
};

#endif // __DIRECTORY_H__