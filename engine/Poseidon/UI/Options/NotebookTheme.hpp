#pragma once

#include <Poseidon/Core/Global.hpp>
#include <Poseidon/IO/ParamFileExt.hpp>

namespace Poseidon::NotebookTheme
{
inline bool IsVanillaNotebookGreen(PackedColor color)
{
    return color.R8() == 0 && color.G8() == 255 && color.B8() == 0;
}

inline PackedColor ApplyRgbKeepingAlpha(PackedColor fallback, PackedColor theme)
{
    return PackedColor(theme.R8(), theme.G8(), theme.B8(), fallback.A8());
}

inline bool ResourceColor(const char* className, const char* entryName, PackedColor& out)
{
    const ParamEntry* cls = Res.FindEntry(className);
    if (!cls)
        return false;
    const ParamEntry* entry = cls->FindEntry(entryName);
    if (!entry)
        return false;
    out = GetPackedColor(*entry);
    return !IsVanillaNotebookGreen(out);
}

inline PackedColor ResourceColorOr(PackedColor fallback)
{
    PackedColor color;
    if (!ResourceColor("RscObjNotebookText", "color", color))
        return fallback;
    return ApplyRgbKeepingAlpha(fallback, color);
}
} // namespace Poseidon::NotebookTheme
