#pragma once

#include <Poseidon/IO/ParamFile/ParamFile.hpp>
#include <Poseidon/IO/Serialization/ParamArchive.hpp>
#include <Poseidon/Graphics/Rendering/Colors.hpp>
#include <Poseidon/Foundation/Strings/Mbcs.hpp>

extern Poseidon::ParamFile Pars;
extern Poseidon::ParamFile ExtParsCampaign;
extern Poseidon::ParamFile ExtParsMission;
extern Poseidon::ParamFile Res;
extern Poseidon::ParamFile Remaster;

namespace Poseidon
{

RString GetDefaultName(RString baseName, const char* dDir, const char* dExt);
RString GetAnimationName(RString baseName);
RString GetShapeName(RString baseName);
RString GetPictureName(RString baseName);
RString GetSoundName(RString baseName);
FontID GetFontID(RString baseName);
RString GetWorldName(RString baseName);
// GetWorldName falls back to initWorld for an unknown island; this tells the two apart.
bool WorldInstalled(RString worldClass);
RString SelectMenuInitWorld(RString initWorld, RString demoWorld, bool preferDemoWorld, bool initWorldExists,
                            bool demoWorldExists);
// Menu background cutscene world: CfgWorlds::demoWorld for the demo build (see
// Application::UseDemoWorld). Otherwise CfgWorlds::initWorld, falling back to
// demoWorld when the configured initWorld terrain is not present in the mounted
// data set.
RString GetMenuInitWorld();

struct SoundPars : public SerializeClass
{
    RString name;
    float vol, freq;
    float volRnd, freqRnd;

#ifndef ACCESS_ONLY
    LSError Serialize(ParamArchive& ar) override;
#else
    LSError Serialize(ParamArchive& ar) { return LSOK; }
#endif
};

PackedColor GetPackedColor(const ParamEntry& entry);
Color GetColor(const ParamEntry& entry);

bool GetValue(PackedColor& val, const ParamEntry& entry);
bool GetValue(Color& val, const ParamEntry& entry);
bool GetValue(SoundPars& val, const ParamEntry& entry);
bool GetValue(SoundPars& val, const IParamArrayValue& entry);

} // namespace Poseidon

using Poseidon::GetAnimationName;
using Poseidon::GetColor;
using Poseidon::GetDefaultName;
using Poseidon::GetFontID;
using Poseidon::GetMenuInitWorld;
using Poseidon::GetPackedColor;
using Poseidon::GetPictureName;
using Poseidon::GetShapeName;
using Poseidon::GetSoundName;
using Poseidon::GetValue;
using Poseidon::GetWorldName;
using Poseidon::SelectMenuInitWorld;
