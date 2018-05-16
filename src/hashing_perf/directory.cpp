#include "directory.h"
#include <boost/filesystem.hpp>
#include <boost/range/iterator_range.hpp>

using namespace boost::filesystem;

Directory::Directory(string path) :
    mPath(path)
{}

void Directory::GetAllFiles()
{
    ASSERT(exists(mPath));
    ASSERT(is_directory(mPath));

    queue<string> directoryQueue;
    directoryQueue.emplace(mPath);

    while (!directoryQueue.empty())
    {
        string dirPath = directoryQueue.front();
        directoryQueue.pop();

        for (auto& entry : boost::make_iterator_range(directory_iterator(dirPath), {}))
        {
            string entryPath = entry.path().relative_path().string();
            if (is_regular_file(entryPath))
            {
                mFiles.emplace(entryPath);
            }
            else
            {
                directoryQueue.emplace(entryPath);
            }
        }
    }
}

bool Directory::HasMoreFiles()
{
    return !mFiles.empty();
}

string Directory::GetNextFile()
{
    string filePath = mFiles.front();
    mFiles.pop();

    return filePath;
}