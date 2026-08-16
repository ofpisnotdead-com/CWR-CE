#include <Poseidon/Core/Global.hpp>
#include <Poseidon/Core/Application.hpp>
#include <Poseidon/IO/Streams/QBStream.hpp>
#include <Poseidon/IO/ParamFile/ParamFile.hpp>
#include <Poseidon/IO/ParamFileExt.hpp>
#include <Poseidon/UI/Locale/Stringtable/Stringtable.hpp>
#include <Poseidon/IO/Filesystem/DirScanner.hpp>
#include <stdio.h>
#include <string.h>
#ifndef _WIN32
#include <unistd.h>
#endif
#include <initializer_list>
#include <Poseidon/Foundation/Framework/Log.hpp>
#include <Poseidon/Foundation/Strings/RString.hpp>
#include <Poseidon/Foundation/platform.hpp>
#ifdef _WIN32
#include <windows.h>

#endif

namespace Poseidon
{
extern bool g_stringtableSystemAvailable;

static RString MakeBinDir(RStringB dir, bool upperCase)
{
    if (dir.GetLength() == 0)
        return upperCase ? RString("BIN") : RString("bin");
    return dir + RString(upperCase ? "/BIN" : "/bin");
}

static bool ResolveFileInDir(RStringB dir, const char* name, RString& fullPath, RString& resolvedName)
{
    RString candidate = dir + RString("/") + RString(name);
    if (QIFStreamB::FileExist(candidate))
    {
        fullPath = candidate;
        resolvedName = name;
        return true;
    }

    DirScanner scan;
    if (!scan.First(dir, nullptr))
        return false;

    do
    {
        const char* entry = scan.GetName();
        if (strcmpi(entry, name) == 0)
        {
            resolvedName = entry;
            fullPath = dir + RString("/") + resolvedName;
            return true;
        }
    } while (scan.Next());

    return false;
}

// #include resolves against the cwd, so chdir into the file's dir and parse the basename;
// the full path after chdir doubles the prefix and parses nothing.
static bool ParseTextFileFromResolvedPath(ParamFile& target, RStringB fullPath, RStringB parseName)
{
    LSError err = target.Parse(fullPath);
    if (err == LSOK)
        return true;

    const char* fullPathStr = fullPath;
    const char* slash = strrchr(fullPathStr, '/');
    if (!slash)
        return false;

    char resolvedDir[512];
    snprintf(resolvedDir, sizeof(resolvedDir), "%.*s", (int)(slash - fullPathStr), fullPathStr);
    char buffer[512];
    ::GetCurrentDirectory(512, buffer);
    SetCurrentDirectory(resolvedDir);
    err = target.Parse(parseName);
    SetCurrentDirectory(buffer);
    return err == LSOK;
}

// A mod's bin/resource replaced the base menu resource; read to keep the mod's menu and to
// restore the base remaster UI resources it shadowed.
static bool s_menuOverriddenByMod = false;

// A mod's bin/config replaced the base master config; read to restore the base config-extra
// (CfgLanguages) it shadowed.
static bool s_configOverriddenByMod = false;

bool IsMenuOverriddenByMod()
{
    return s_menuOverriddenByMod;
}

bool IsConfigOverriddenByMod()
{
    return s_configOverriddenByMod;
}

void MergeBaseResourceExtra()
{
    // ParseResource stops at the first mod's bin/resource and clears Res, so the base
    // resource-extra.cpp (remaster Options/Mods UI) is never reached; merge it back on top.
    for (bool upperCase : {false, true})
    {
        RString binDir = MakeBinDir("", upperCase);
        RString extraFile;
        RString extraFileName;
        if (ResolveFileInDir(binDir, "resource-extra.cpp", extraFile, extraFileName))
        {
            ParamFile extra;
            ParseTextFileFromResolvedPath(extra, extraFile, extraFileName);
            Res.Update(extra);
            // Update leaves array values' _file aimed at the stack-local `extra`; re-point at Res
            // before it dies, else a later string-expression GetFloat derefs freed stack.
            Res.SetFile(&Res);
            LOG_INFO(Config, "MergeBaseResourceExtra: restored base {} over the mod resource", (const char*)extraFile);
            return;
        }
    }
}

void MergeBaseConfigExtra()
{
    // Twin of MergeBaseResourceExtra: a config-replacing mod clears Pars and shadows the base
    // config-extra.cpp (CfgLanguages); re-apply it on top.
    for (bool upperCase : {false, true})
    {
        RString binDir = MakeBinDir("", upperCase);
        RString extraFile;
        RString extraFileName;
        if (ResolveFileInDir(binDir, "config-extra.cpp", extraFile, extraFileName))
        {
            ParamFile extra;
            ParseTextFileFromResolvedPath(extra, extraFile, extraFileName);
            Pars.Update(extra);
            // Re-point at Pars before the stack-local `extra` dies (stack-use-after-return), as
            // in the resource-extra path.
            Pars.SetFile(&Pars);
            LOG_INFO(Config, "MergeBaseConfigExtra: restored base {} over the mod config", (const char*)extraFile);
            return;
        }
    }
}

static bool ParseConfigFromDir(const RStringB& dir, ParamFile& target)
{
    RString file;
    RString fileName;
    if (ResolveFileInDir(dir, "config.cpp", file, fileName))
        return ParseTextFileFromResolvedPath(target, file, fileName);
    if (ResolveFileInDir(dir, "config.bin", file, fileName))
    {
        target.ParseBin(file);
        return true;
    }
    return false;
}

bool ParseStringtable(RStringB dir, void* context)
{
    for (bool upperCase : {false, true})
    {
        RString binDir = MakeBinDir(dir, upperCase);
        RString file;
        RString fileName;
        if (ResolveFileInDir(binDir, "stringtable.csv", file, fileName))
        {
            LoadStringtable("Global", file, 0, false);
            g_stringtableSystemAvailable = true;
            return true;
        }
    }
    return false;
}

bool ParseConfig(RStringB dir, void* context)
{
    // A mod's bin/config replaces the base outright: EnumDirectories stops at the first mod that
    // returns true, so vanilla content the mod omits does not leak back in. Mirrors ParseResource.
    s_configOverriddenByMod = false; // reset so a re-init with no mod config leaves it false
    RString binDirUsed;
    for (bool upperCase : {false, true})
    {
        RString binDir = MakeBinDir(dir, upperCase);
        if (ParseConfigFromDir(binDir, Pars))
        {
            binDirUsed = binDir;
            break;
        }
    }
    if (binDirUsed.GetLength() == 0)
        return false;

    // config-extra.cpp carries the remaster's CfgLanguages as new classes, merged via Update() so
    // they apply without rebuilding CONFIG.BIN; a config-replacing mod's bin has none.
    RString extraFile;
    RString extraFileName;
    if (ResolveFileInDir(binDirUsed, "config-extra.cpp", extraFile, extraFileName))
    {
        ParamFile extra;
        ParseTextFileFromResolvedPath(extra, extraFile, extraFileName);
        Pars.Update(extra);
        Pars.SetFile(&Pars);
        LOG_INFO(Config, "ParseConfig: merged {} into Pars", (const char*)extraFile);
    }

    // Non-empty dir means a mod's config won (enumeration stopped here).
    s_configOverriddenByMod = (dir.GetLength() > 0);
    return true;
}

bool ParseRemaster(RStringB dir, void* context)
{
    for (bool upperCase : {false, true})
    {
        RString binDir = MakeBinDir(dir, upperCase);
        RString file;
        RString fileName;
        if (ResolveFileInDir(binDir, "remaster.cpp", file, fileName))
            return ParseTextFileFromResolvedPath(Remaster, file, fileName);
        if (ResolveFileInDir(binDir, "remaster.bin", file, fileName))
        {
            Remaster.ParseBin(file);
            return true;
        }
    }
    return false;
}

bool ParseResource(RStringB dir, void* context)
{
    bool ok = false;
    RString binDirUsed;
    for (bool upperCase : {false, true})
    {
        RString binDir = MakeBinDir(dir, upperCase);
        RString file;
        RString fileName;
        if (ResolveFileInDir(binDir, "resource.cpp", file, fileName))
        {
            ParseTextFileFromResolvedPath(Res, file, fileName);
            ok = true;
            binDirUsed = binDir;
            break;
        }
        if (ResolveFileInDir(binDir, "resource.bin", file, fileName))
        {
            Res.ParseBin(file);
            ok = true;
            binDirUsed = binDir;
            break;
        }
    }

    if (!ok)
        return false;

    // resource-extra.cpp adds displays/templates without rebuilding RESOURCE.BIN; Update() (not
    // Parse, which clears entries) merges it on top.
    {
        RString extraFile;
        RString extraFileName;
        if (ResolveFileInDir(binDirUsed, "resource-extra.cpp", extraFile, extraFileName))
        {
            ParamFile extra;
            ParseTextFileFromResolvedPath(extra, extraFile, extraFileName);
            Res.Update(extra);
            // Re-point at Res before the stack-local `extra` dies, else a later string-expression
            // GetFloat derefs freed stack (stack-use-after-return).
            Res.SetFile(&Res);
            LOG_INFO(Config, "ParseResource: merged {} into Res", (const char*)extraFile);
        }
    }

    // Non-empty dir means a mod's resource won (enumeration stopped here).
    s_menuOverriddenByMod = (dir.GetLength() > 0);
    return true;
}

} // namespace Poseidon
