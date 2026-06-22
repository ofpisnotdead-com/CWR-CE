#include "StudioConfig.hpp"
#include <Poseidon/IO/ParamFile/ParamFile.hpp>
#include <Poseidon/Foundation/Common/PlatformPaths.hpp>
#include <filesystem>

namespace Poseidon
{

struct IntField
{
    const char* name;
    int StudioConfig::* ptr;
};

static const IntField intFields[] = {
    {"winX", &StudioConfig::winX}, {"winY", &StudioConfig::winY},           {"winW", &StudioConfig::winW},
    {"winH", &StudioConfig::winH}, {"maximized", &StudioConfig::maximized}, {"sidebarPct", &StudioConfig::sidebarPct},
};

std::string StudioConfig::configPath()
{
    return Poseidon::Foundation::getUserConfigDir("PoseidonStudio") + "/studio.cfg";
}

void StudioConfig::load()
{
    ParamFile cfg;
    if (cfg.Parse(configPath().c_str()) != LSOK)
        return;

    for (auto& f : intFields)
        if (auto* e = cfg.FindEntry(f.name))
            this->*f.ptr = (int)*e;
}

void StudioConfig::save() const
{
    std::filesystem::create_directories(std::filesystem::path(configPath()).parent_path());

    ParamFile cfg;
    for (auto& f : intFields)
        cfg.Add(f.name, this->*f.ptr);

    cfg.Save(configPath().c_str());
}

} // namespace Poseidon
