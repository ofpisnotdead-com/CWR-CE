#include <Poseidon/Core/Application.hpp>
#include <Poseidon/IO/MapFile.hpp>

#include <Poseidon/Foundation/Algorithms/Qsort.hpp>
#include <Poseidon/IO/Streams/QStream.hpp>
#include <Poseidon/Foundation/Common/Win.h>
#include <Poseidon/Foundation/Common/Filenames.hpp>
#include <Poseidon/Foundation/Common/FltOpts.hpp>
#include <stdio.h>
#include <string.h>
#include <Poseidon/Foundation/Strings/Bstring.hpp>

namespace Poseidon
{
using Poseidon::Foundation::QSort;

static int CmpMaps(const MapInfo* f0, const MapInfo* f1)
{
    return f0->physAddress - f1->physAddress;
}

static void GetLine(char* line, int size, QIStream& in)
{
    char* l = line;
    int c = in.get();
    while (c != EOF && c != '\n')
    {
        if (size > 1)
        {
            if (c != '\r')
            {
                *l++ = static_cast<char>(c);
                size--;
            }
        }
        c = in.get();
    }
    if (size > 0)
    {
        *l = 0;
    }
}

void MapFile::ParseMapFile()
{
#if defined _WIN32
    if (_map.Size() > 0)
    {
        return; // map file already parsed
    }
    QIFStream in;
    char sourceName[256];
    // use program name as given on command line
    const char* cLine = GetCommandLine();
    if (*cLine == '"')
    {
        const char* eName = strchr(cLine + 1, '"');
        if (!eName)
        {
            LOG_DEBUG(Core, "Cannot get program name from '{}'.", cLine);
            return;
        }
        ::strncpy(sourceName, cLine + 1, eName - 1 - cLine), sourceName[eName - 1 - cLine] = 0;
    }
    else
    {
        const char* eName = strchr(cLine, ' ');
        if (!eName)
        {
            snprintf(sourceName, sizeof(sourceName), "%s", (const char*)cLine);
        }
        else
        {
            ::strncpy(sourceName, cLine, eName - cLine), sourceName[eName - cLine] = 0;
        }
    }
    // replace .exe with .map
    char* ext = GetFileExt(GetFilenameExt(sourceName));
    if (ext)
    {
        ::strcpy(ext, ".map");
    }
    else
    {
        strncat(sourceName, ".map", sizeof(sourceName) - strlen(sourceName) - 1);
    }
    _name = sourceName;

    in.open(sourceName);
    ParseMapStream(in);
#endif
}

void MapFile::ParseMapStream(QIStream& in)
{
    char line[1024];
    for (;;)
    {
        GetLine(line, sizeof(line), in);
        if (in.eof() || in.fail())
        {
            return;
        }
        if (in.eof())
        {
            return;
        }
        if (!strcmpi(line, "  Address         Publics by Value              Rva+Base     Lib:Object"))
        {
            break;
        }
    }
    for (;;)
    { // search for first nonempty line
        char name[256];
        GetLine(line, sizeof(line), in);
        if (in.eof() || in.fail())
        {
            break;
        }
        if (!strchr(line, ':'))
        {
            continue;
        }
        if (line[0] == 0)
        {
            continue;
        }
        // scan line for: <rel addr> <name> <abs addr> <......>
        const char* c = line;
        while (isspace(*c))
        {
            c++;
        }
        int logSection = strtoul(c, (char**)&c, 16);
        if (logSection != 1)
        {
            continue;
        }
        while (*c == ':' || isspace(*c))
        {
            c++;
        }
        int logAddress = strtoul(c, (char**)&c, 16);
        while (isspace(*c))
        {
            c++;
        }
        char* d = name;
        while (!isspace(*c) && *c)
        {
            *d++ = *c++; // get name
        }
        *d = 0;
        while (isspace(*c))
        {
            c++;
        }
        int physAddress = strtoul(c, (char**)&c, 16);
        if (*name == 0)
        {
            continue; // no name
        }
        // we can ignore rest of the line
        // store name/address information
        MapInfo info;
        strcpy(info.name, name);
        info.physAddress = physAddress;
        info.logAddress = logAddress;
        _map.Add(info);
    }
    QSort(_map.Data(), _map.Size(), CmpMaps);
}

const char* MapFile::MapName(int address, MapAddressId id, int* lower)
{
    for (int i = 0; i < _map.Size(); i++)
    {
        if (_map[i].*id <= address && (i >= _map.Size() - 1 || _map[i + 1].*id > address))
        {
            if (lower)
            {
                *lower = _map[i].*id;
            }
            const char* name = _map[i].name;
            // extract class and function name
            // starting character is calling convention
            if (*name == '?') // C++ decorated name
            {
                const char* eName;
                static char resName[256];
                static char resClass[256];
                name++;
                if (*name == '?')
                {
                    name++;
                    switch (*name)
                    {
                        case '0':
                            snprintf(resName, sizeof(resName), "%s", (const char*)"constructor");
                            name++;
                            break;
                        case '1':
                            snprintf(resName, sizeof(resName), "%s", (const char*)"destructor");
                            name++;
                            break;
                        case '2':
                            snprintf(resName, sizeof(resName), "%s", (const char*)"operator new");
                            name++;
                            break;
                        case '3':
                            snprintf(resName, sizeof(resName), "%s", (const char*)"operator delete");
                            name++;
                            break;
                        case '4':
                            snprintf(resName, sizeof(resName), "%s", (const char*)"operator =");
                            name++;
                            break;
                        case 'R':
                            snprintf(resName, sizeof(resName), "%s", (const char*)"operator ()");
                            name++;
                            break;
                        default:
                            snprintf(resName, sizeof(resName), "%s", (const char*)"###");
                            char* opName = strchr(resName, 0);
                            *opName++ = *name++;
                            *opName = 0;
                            break;
                    }
                }
                else
                {
                    eName = strchr(name, '@');
                    if (!eName)
                    {
                        return name;
                    }
                    snprintf(resName, sizeof(resName), "%s", (const char*)name);
                    resName[eName - name] = 0;
                    name = eName + 1;
                }
                eName = strchr(name, '@');
                if (!eName)
                {
                    snprintf(resClass, sizeof(resClass), "%s", (const char*)"");
                }
                else
                {
                    snprintf(resClass, sizeof(resClass), "%s", (const char*)name);
                    resClass[eName - name] = 0;
                }
                if (*resClass)
                {
                    strncat(resClass, "::", sizeof(resClass) - strlen(resClass) - 1);
                    strncat(resClass, resName, sizeof(resClass) - strlen(resClass) - 1);
                    return resClass;
                }
                else
                {
                    return resName;
                }
            }
            else
            {
                return name + 1;
            }
        }
    }
    return "???";
}

int MapFile::MinAddress(MapAddressId id) const
{
    if (_map.Size() <= 0)
    {
        return 0;
    }
    return _map[0].*id;
}

int MapFile::MaxAddress(MapAddressId id) const
{
    if (_map.Size() <= 0)
    {
        return 0x7fffffff;
    }
    return _map[_map.Size() - 1].*id;
}

int MapFile::Address(const char* name, MapAddressId id) const
{
    for (int i = 0; i < _map.Size(); i++)
    {
        if (!strcmp(_map[i].name, name))
        {
            return _map[i].*id;
        }
    }
    return 0;
}

} // namespace Poseidon
