#include <Poseidon/Core/Application.hpp>
#include <Poseidon/IO/ParamFileExt.hpp>

#include <Poseidon/IO/Streams/QBStream.hpp>

#include <Poseidon/UI/Locale/StringtableExt.hpp>
#include <Poseidon/Foundation/Strings/Mbcs.hpp>
#include <string.h>
#include <Poseidon/Foundation/Common/FltOpts.hpp>
#include <Poseidon/Foundation/Framework/Log.hpp>
#include <Poseidon/Foundation/platform.hpp>

#ifndef ACCESS_ONLY
#include <Poseidon/IO/Serialization/ParamArchive.hpp>
#endif

namespace Poseidon
{

FontID GetFontID(RString baseName)
{
    int langID = English;
    // Strip path prefix (some resources use "fonts\X" or "\fonts\X" instead of just "X")
    const char* name = baseName;
    if (name[0] == '\\' || name[0] == '/')
        name++;
    if (_strnicmp(name, "fonts\\", 6) == 0 || _strnicmp(name, "fonts/", 6) == 0)
        name += 6;
    RString lookupName = name;

    const ParamEntry* cls = (Pars >> "CfgFonts").FindEntry(GLanguage);
    if (cls)
    {
        const ParamEntry* entry = cls->FindEntry(lookupName);
        if (entry)
        {
            lookupName = *entry;
            langID = Poseidon::Foundation::GetLangID();
        }
    }
    return FontID(GetDefaultName(lookupName, "fonts\\", ""), langID);
}

PackedColor GetPackedColor(const ParamEntry& entry)
{
    if (entry.GetSize() != 4)
    {
        return PackedWhite;
    }
    int r8 = toInt(entry[0].GetFloat() * 255);
    int g8 = toInt(entry[1].GetFloat() * 255);
    int b8 = toInt(entry[2].GetFloat() * 255);
    int a8 = toInt(entry[3].GetFloat() * 255);
    saturate(r8, 0, 255);
    saturate(g8, 0, 255);
    saturate(b8, 0, 255);
    saturate(a8, 0, 255);
    return PackedColor(r8, g8, b8, a8);
}

Color GetColor(const ParamEntry& entry)
{
    if (entry.GetSize() != 4)
    {
        return HWhite;
    }
    return Color(entry[0].GetFloat(), entry[1].GetFloat(), entry[2].GetFloat(), entry[3].GetFloat());
}

bool GetValue(SoundPars& pars, const ParamEntry& entry)
{
    if (entry.GetSize() < 3)
    {
        const static SoundPars nil = {};
        pars = nil;
        return false;
    }
    pars.name = GetSoundName(entry[0]);
    pars.vol = entry[1].GetFloat();
    pars.freq = entry[2].GetFloat();
    pars.freqRnd = 0;
    pars.volRnd = 0.05;
    return true;
}

bool GetValue(SoundPars& pars, const IParamArrayValue& entry)
{
    if (entry.GetItemCount() < 3)
    {
        const static SoundPars nil = {};
        pars = nil;
        return false;
    }
    pars.name = GetSoundName(entry[0]);
    pars.vol = entry[1].GetFloat();
    pars.freq = entry[2].GetFloat();
    pars.freqRnd = 0;
    pars.volRnd = 0.05;
    return true;
}

bool GetValue(PackedColor& val, const ParamEntry& entry)
{
    if (entry.GetSize() != 4)
    {
        val = PackedWhite;
        return false;
    }
    val = PackedColor(Color(entry[0].GetFloat(), entry[1].GetFloat(), entry[2].GetFloat(), entry[3].GetFloat()));
    return true;
}

bool GetValue(Color& val, const ParamEntry& entry)
{
    if (entry.GetSize() != 4)
    {
        val = HWhite;
        return false;
    }
    val = Color(entry[0].GetFloat(), entry[1].GetFloat(), entry[2].GetFloat(), entry[3].GetFloat());
    return true;
}

RString GetDefaultName(RString baseName, const char* dDir, const char* dExt)
{
    if (!baseName || !baseName[0])
    {
        return "";
    }
    char buf[256];
    *buf = 0;

    const char* name = baseName;
    // Leading slash: caller already supplied a fully-qualified path
    if (name[0] == '\\' || name[0] == '/')
    {
        name++;
    }
    else
    {
        // Skip the prefix if the name already starts with dDir (case-insensitive,
        // accepting '/' as an alternative to dDir's trailing '\\').
        // Mission description.ext entries like "sound/s01r01.ogg" otherwise
        // become "sound\sound/s01r01.ogg" and fail to resolve.
        const size_t dDirLen = strlen(dDir);
        const bool alreadyPrefixed =
            dDirLen > 0 && (_strnicmp(name, dDir, dDirLen) == 0 ||
                            (dDir[dDirLen - 1] == '\\' && _strnicmp(name, dDir, dDirLen - 1) == 0 &&
                             (name[dDirLen - 1] == '/' || name[dDirLen - 1] == '\\')));
        if (!alreadyPrefixed)
        {
            strncat(buf, dDir, sizeof(buf) - strlen(buf) - 1);
        }
    }
    strncat(buf, name, sizeof(buf) - strlen(buf) - 1);

    const char* ext = strchr(name, '.');
    if (!ext)
    {
        strncat(buf, dExt, sizeof(buf) - strlen(buf) - 1);
    }
    strlwr(buf);
    return buf;
}

RString GetShapeName(RString baseName)
{
    return GetDefaultName(baseName, "data3d\\", ".p3d");
}
RString GetAnimationName(RString baseName)
{
    return GetDefaultName(baseName, "anim\\", ".rtm");
}
RString GetPictureName(RString baseName)
{
    return GetDefaultName(baseName, "data\\", ".paa");
}
RString GetSoundName(RString baseName)
{
    return GetDefaultName(baseName, "sound\\", ".wss");
}

#ifndef ACCESS_ONLY
LSError SoundPars::Serialize(ParamArchive& ar)
{
    PARAM_CHECK(ar.Serialize("name", name, 1))
    PARAM_CHECK(ar.Serialize("vol", vol, 1))
    PARAM_CHECK(ar.Serialize("freq", freq, 1))
    PARAM_CHECK(ar.Serialize("volRnd", volRnd, 1))
    PARAM_CHECK(ar.Serialize("freqRnd", freqRnd, 1))
    return LSOK;
}
#endif

RString GetWorldName(RString baseName)
{
    const ParamEntry* entry = (Pars >> "CfgWorlds").FindEntry(baseName);
    if (!entry)
    {
        baseName = Pars >> "CfgWorlds" >> "initWorld";
        entry = (Pars >> "CfgWorlds").FindEntry(baseName);
    }
    RString world = (*entry) >> "worldName";
    return GetDefaultName(world, "worlds\\", ".wrp");
}

RString SelectMenuInitWorld(RString initWorld, RString demoWorld, bool preferDemoWorld, bool initWorldExists,
                            bool demoWorldExists)
{
    if (preferDemoWorld && demoWorld.GetLength() > 0)
        return demoWorld;

    if (initWorldExists || demoWorld.GetLength() == 0 || !demoWorldExists)
        return initWorld;

    return demoWorld;
}

bool WorldInstalled(RString worldClass)
{
    if (worldClass.GetLength() == 0)
        return false;

    const ParamEntry* entry = (Pars >> "CfgWorlds").FindEntry(worldClass);
    if (!entry)
        return false;

    RString world = (*entry) >> "worldName";
    if (world.GetLength() == 0)
        return false;

    return QIFStreamB::FileExist(GetDefaultName(world, "worlds\\", ".wrp"));
}

RString GetMenuInitWorld()
{
    RString initWorld = Pars >> "CfgWorlds" >> "initWorld";
    RString demoWorld = Pars >> "CfgWorlds" >> "demoWorld";
    const bool preferDemoWorld = GApp && GApp->UseDemoWorld();
    const bool initWorldExists = WorldInstalled(initWorld);
    const bool demoWorldExists = WorldInstalled(demoWorld);

    RString selected = SelectMenuInitWorld(initWorld, demoWorld, preferDemoWorld, initWorldExists, demoWorldExists);
    if (!preferDemoWorld && selected != initWorld)
    {
        LOG_INFO(Core, "Menu intro world '{}' missing; using configured demoWorld '{}'", (const char*)initWorld,
                 (const char*)selected);
    }
    return selected;
}

static RString PathFirstFolder(const char* path)
{
    const char* next = strchr(path, '/');
    if (!next)
    {
        return "";
    }
    return RString(path, next - path);
}

} // namespace Poseidon

#include <Poseidon/Foundation/Algorithms/Crc.hpp>

using Poseidon::ParamEntry;
using Poseidon::ParamFile;

// PASumCalculator must be in namespace Poseidon because ParamFile.hpp
// declares the virtual interface using Poseidon::PASumCalculator.
namespace Poseidon
{

class PASumCalculator : public Foundation::CRCCalculator
{
};

} // namespace Poseidon

// At global scope so its RTTI name stays unmangled.
class PASumCalculatorFunctions : public Poseidon::CRCFunctions
{
  public:
    void Add(Poseidon::PASumCalculator& sum, const void* buffer, int size) override;
} GPASumCalculatorFunctions;

void PASumCalculatorFunctions::Add(Poseidon::PASumCalculator& sum, const void* buffer, int size)
{
    sum.Add(buffer, size);
}

const ParamEntry* FindConfigParamEntry(const char* path)
{
    if (*path == 0)
    {
        return nullptr;
    }
    // check path base
    const ParamEntry* entry = nullptr;
    Poseidon::Foundation::RString base = Poseidon::PathFirstFolder(path);
    if (!strcmpi(base, "cfg"))
    {
        entry = &Pars;
    }
    else if (!strcmpi(base, "rsc"))
    {
        entry = &Res;
    }
    else
    {
        LOG_DEBUG(Core, "Invalid base in {}", path);
        return nullptr;
    }
    path += strlen(base) + 1;

    while (strlen(path) > 0)
    {
        Poseidon::Foundation::RString base = Poseidon::PathFirstFolder(path);
        if (base.GetLength() <= 0)
        {
            ParamEntry* nEntry = entry->FindEntry(path);
            if (!nEntry)
            {
                break;
            }
            if (!nEntry->IsClass())
            {
                break;
            }
            entry = nEntry;
            break;
        }
        else
        {
            ParamEntry* nEntry = entry->FindEntry(base);
            if (!nEntry)
            {
                break;
            }
            if (!nEntry->IsClass())
            {
                break;
            }
            entry = nEntry;
            path += strlen(base) + 1;
        }
    }
    return entry;
}
unsigned int CalculateConfigCRC(const char* path)
{
    // check path base
    const ParamEntry* entry = FindConfigParamEntry(path);
    if (!entry)
    {
        Poseidon::PASumCalculator sum;
        sum.Reset();
        Pars.CalculateCheckValue(sum);
        Res.CalculateCheckValue(sum);
        return sum.GetResult();
    }

    Poseidon::PASumCalculator sum;
    sum.Reset();
    entry->CalculateCheckValue(sum);
    return sum.GetResult();
}

// Explicit registration — call once from program startup (typically via
// Poseidon::InitDefaults()). Registering here wins over any static-init-order
// clobber from ParamFile's dynamic default init.
void InitParamFileExtDefaults()
{
    ParamFile::SetDefaultCRCFunctions(&GPASumCalculatorFunctions);
}
