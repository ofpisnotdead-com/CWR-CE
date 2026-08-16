#include <Poseidon/IO/Filesystem/DirTree.hpp>

#include <Poseidon/Foundation/platform.hpp>

#include <stdio.h>
#include <string.h>
#ifndef _WIN32
#include <unistd.h>
#endif
#ifdef _WIN32
#include <io.h>
#include <direct.h>
#endif
#include <sys/stat.h>

namespace Poseidon
{

void DeleteDirectoryStructure(const char* name, bool deleteDir)
{
    if (!name || *name == 0)
    {
        return;
    }

    // Normalize to the platform separator up front. _findfirst tolerates either
    // separator, but the raw chmod/unlink/rmdir below are separator-sensitive:
    // callers pass mission/campaign paths built with backslashes, so on Linux
    // an un-normalized path would make every delete a literal-filename no-op.
    char base[MaxFileName];
    snprintf(base, sizeof(base), "%s", name);
    platformPath(base);

    char buffer[MaxFileName];
    snprintf(buffer, sizeof(buffer), "%s%c*.*", base, PATH_SEP);

    _finddata_t info;
    intptr_t h = _findfirst(buffer, &info);
    if (h != -1)
    {
        do
        {
            if ((info.attrib & _A_SUBDIR) != 0)
            {
                if (strcmp(info.name, ".") != 0 && strcmp(info.name, "..") != 0)
                {
                    snprintf(buffer, sizeof(buffer), "%s%c%s", base, PATH_SEP, info.name);
                    DeleteDirectoryStructure(buffer, true);
                }
            }
            else
            {
                snprintf(buffer, sizeof(buffer), "%s%c%s", base, PATH_SEP, info.name);
                chmod(buffer, _S_IREAD | _S_IWRITE);
                unlink(buffer);
            }
        } while (_findnext(h, &info) == 0);
        _findclose(h);
    }
    if (deleteDir)
    {
        chmod(base, _S_IREAD | _S_IWRITE);
        rmdir(base);
    }
}

} // namespace Poseidon
