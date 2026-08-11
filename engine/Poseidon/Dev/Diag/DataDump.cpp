#include <Poseidon/Dev/Diag/DataDump.hpp>

#include <Poseidon/AI/EntityAIType.hpp>
#include <Poseidon/Dev/Debug/DebugCommands.hpp>
#include <Poseidon/Foundation/Common/GamePaths.hpp>
#include <Poseidon/Foundation/Framework/Log.hpp>
#include <Poseidon/Graphics/Rendering/Shape/Shape.hpp>
#include <Poseidon/IO/ParamFileExt.hpp>
#include <Poseidon/IO/Streams/FileInfo.h>
#include <Poseidon/IO/Streams/QBStream.hpp>
#include <Poseidon/World/Entities/Infantry/SoldierOld.hpp>
#include <Poseidon/World/Entities/Vehicles/House.hpp>
#include <Poseidon/World/Entities/Vehicles/Vehicle.hpp>
#include <Poseidon/World/Entities/Weapons/Weapons.hpp>
#include <Poseidon/World/Scene/ObjectClasses.hpp>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace Poseidon::Dev
{
namespace DataDump
{

namespace
{

bool OpenDump(const char* stem, std::ofstream& os, std::string& outPath, std::string& error)
{
    std::string dir = Foundation::GamePaths::Instance().CacheDir();
    if (!dir.empty() && dir.back() != '/' && dir.back() != '\\')
        dir += PATH_SEP;
    dir += "dumps";

    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    if (ec)
    {
        error = "cannot create " + dir + ": " + ec.message();
        return false;
    }

    outPath = dir + PATH_SEP + stem + ".txt";
    os.open(outPath);
    if (!os.is_open())
    {
        error = "cannot write " + outPath;
        return false;
    }
    return true;
}

std::string Bytes(long long n)
{
    char buf[32];
    if (n >= 1024LL * 1024 * 1024)
        snprintf(buf, sizeof(buf), "%.1fG", double(n) / (1024.0 * 1024 * 1024));
    else if (n >= 1024 * 1024)
        snprintf(buf, sizeof(buf), "%.1fM", double(n) / (1024.0 * 1024));
    else if (n >= 1024)
        snprintf(buf, sizeof(buf), "%.1fK", double(n) / 1024.0);
    else
        snprintf(buf, sizeof(buf), "%lldB", n);
    return buf;
}

const char* Str(const RString& s)
{
    return (const char*)s;
}

std::string Lower(const char* s)
{
    std::string r = s ? s : "";
    for (char& c : r)
        c = char(tolower((unsigned char)c));
    return r;
}

// --- banks -------------------------------------------------------------

struct BankStats
{
    int files = 0;
    long long stored = 0;
    long long uncompressed = 0;
    int compressed = 0;
};

void AccumulateFile(const FileInfoO& fi, const FileBankType*, void* context)
{
    BankStats* s = static_cast<BankStats*>(context);
    s->files++;
    s->stored += fi.length;
    const bool packed = fi.compressedMagic == CompMagic;
    s->uncompressed += packed ? fi.uncompressedSize : fi.length;
    if (packed)
        s->compressed++;
}

// --- config ------------------------------------------------------------

ParamFile* RootOf(ConfigRoot root)
{
    switch (root)
    {
        case ConfigRoot::Pars:
            return &Pars;
        case ConfigRoot::Res:
            return &Res;
        case ConfigRoot::Remaster:
            return &Remaster;
        case ConfigRoot::Mission:
            return &ExtParsMission;
        case ConfigRoot::Campaign:
            return &ExtParsCampaign;
    }
    return &Pars;
}

const char* RootName(ConfigRoot root)
{
    switch (root)
    {
        case ConfigRoot::Pars:
            return "Pars";
        case ConfigRoot::Res:
            return "Res";
        case ConfigRoot::Remaster:
            return "Remaster";
        case ConfigRoot::Mission:
            return "ExtParsMission";
        case ConfigRoot::Campaign:
            return "ExtParsCampaign";
    }
    return "Pars";
}

const char* AccessName(ParamAccessMode mode)
{
    switch (mode)
    {
        case PAReadAndWrite:
            return " rw";
        case PAReadAndCreate:
            return " create-only";
        case PAReadOnly:
            return " read-only";
        case PAReadOnlyVerified:
            return " read-only-verified";
        case PADefault:
            break;
    }
    return "";
}

std::string ArrayValue(const ParamEntry& entry)
{
    std::string out = "{";
    const int n = entry.GetSize();
    for (int i = 0; i < n; i++)
    {
        if (i)
            out += ", ";
        if (i == 8 && n > 10)
        {
            char buf[48];
            snprintf(buf, sizeof(buf), "... %d more", n - i);
            out += buf;
            break;
        }
        const IParamArrayValue& v = entry[i];
        out += v.IsArrayValue() ? "{...}" : Str(v.GetValue());
    }
    return out + "}";
}

struct ClassCounts
{
    int classes = 0;
    int entries = 0;
    int depth = 0;
};

void CountClass(const ParamClass& cls, int depth, ClassCounts& c)
{
    if (depth > c.depth)
        c.depth = depth;
    const int n = cls.GetEntryCount();
    for (int i = 0; i < n; i++)
    {
        const ParamEntry& e = cls.GetEntry(i);
        if (e.IsClass())
        {
            c.classes++;
            CountClass(*e.GetClassInterface(), depth + 1, c);
        }
        else
        {
            c.entries++;
        }
    }
}

void WalkClass(std::ofstream& os, const ParamClass& cls, int depth, const ConfigOptions& opt, const std::string& indent)
{
    const int n = cls.GetEntryCount();
    for (int i = 0; i < n; i++)
    {
        const ParamEntry& e = cls.GetEntry(i);
        if (e.IsClass())
        {
            const ParamClass& sub = *e.GetClassInterface();
            const char* base = sub.GetBaseName();
            os << indent << Str(sub.GetName());
            if (base && *base)
                os << " : " << base;
            os << "  [" << sub.GetEntryCount() << "]";

            const RStringB& owner = sub.GetOwner();
            if (!owner.GetLength())
                os << "  <->";
            else
                os << "  <" << Str(owner) << ">";
            os << AccessName(sub.GetAccessMode());
            if (sub.HasChecksum())
                os << " crc";
            os << "\n";

            if (depth < opt.maxDepth)
                WalkClass(os, sub, depth + 1, opt, indent + "  ");
        }
        else if (opt.values)
        {
            os << indent << Str(e.GetName());
            if (e.IsArray())
                os << "[" << e.GetSize() << "] = " << ArrayValue(e);
            else
                os << " = " << Str(e.GetValue());
            os << "\n";
        }
    }
}

// --- types -------------------------------------------------------------

void DumpShapeNames(std::ofstream& os)
{
    std::vector<std::string> names;
    Shapes.ForEach(
        [&names](LODShapeWithShadow& shape)
        {
            if (shape.Name())
                names.push_back(shape.Name());
        });
    std::sort(names.begin(), names.end());
    os << "\n=== Shapes (" << names.size() << " loaded) ===\n";
    for (const std::string& n : names)
        os << "  " << n << "\n";
}

// --- slots -------------------------------------------------------------

RStringB HitPointName(const EntityAIType& type, const HitPoint& hp)
{
    LODShapeWithShadow* shape = type.GetShape();
    if (!shape)
        return RStringB();
    Shape* level = shape->HitpointsLevel();
    const int sel = hp.GetSelection();
    if (!level || sel < 0 || sel >= level->NNamedSel())
        return RStringB();
    return level->NamedSel(sel).GetName();
}

bool HasIndexTables(const EntityType& base)
{
    if (base._animations.Size())
        return true;
    if (const EntityAIType* type = dynamic_cast<const EntityAIType*>(&base))
        if (type->NWeaponSystems() || type->GetHitPoints().Size())
            return true;
    if (const ManType* man = dynamic_cast<const ManType*>(&base))
        if (man->MoveIdN())
            return true;
    if (const BuildingType* building = dynamic_cast<const BuildingType*>(&base))
        if (building->GetLadderCount())
            return true;
    return false;
}

void DumpOneType(std::ofstream& os, const EntityType& base)
{
    const EntityAIType* type = dynamic_cast<const EntityAIType*>(&base);

    os << "\n=== " << Str(base.GetName()) << " ===\n";

    if (type)
    {
        // Slot index = weapon-major, then muzzle, then mode -- exactly the order
        // EntityAI::AddWeapon walks when it builds _magazineSlots, so inserting a
        // mode or muzzle anywhere shifts every slot after it.
        os << "  magazine slots (weapon x muzzle x mode)\n";
        int slot = 0;
        for (int w = 0; w < type->NWeaponSystems(); w++)
        {
            const WeaponType* weapon = type->GetWeaponSystem(w);
            if (!weapon)
                continue;
            os << "    [weapon " << w << "] " << Str(weapon->GetName()) << "\n";
            for (int m = 0; m < weapon->_muzzles.Size(); m++)
            {
                const MuzzleType* muzzle = weapon->_muzzles[m];
                if (!muzzle)
                    continue;
                const MagazineType* mag = muzzle->_typicalMagazine;
                os << "      [muzzle " << m << "] " << Str(muzzle->GetName()) << "  modes " << muzzle->_nModes;
                if (mag)
                    os << "  typical mag " << Str(mag->GetName());
                os << "\n";
                for (int mode = 0; mode < muzzle->_nModes; mode++, slot++)
                {
                    os << "        slot " << slot;
                    const WeaponModeType* wm = (mag && mode < mag->_modes.Size()) ? mag->_modes[mode] : nullptr;
                    if (wm)
                        os << "  " << Str(wm->GetName()) << "  disp " << wm->_dispersion << "  reload "
                           << wm->_reloadTime;
                    os << "\n";
                }
            }
        }
        if (!slot)
            os << "    (none)\n";

        const HitPointList& hits = type->GetHitPoints();
        if (hits.Size())
        {
            os << "  hit points (" << hits.Size() << ") -- index feeds the saved and networked _hit[]\n";
            for (int i = 0; i < hits.Size(); i++)
            {
                const HitPoint* hp = hits[i];
                if (!hp)
                    continue;
                RStringB name = HitPointName(*type, *hp);
                os << "    [" << hp->GetIndex() << "] " << (name.GetLength() ? Str(name) : "(unnamed)") << "  armor "
                   << hp->GetArmor() << "\n";
            }
            if (type->_reflectors.Size())
                os << "    (" << type->_reflectors.Size() << " reflectors feed this list from config array order)\n";
        }
    }

    if (base._animations.Size())
    {
        os << "  animations (" << base._animations.Size() << ") -- positional on the wire, no names sent\n";
        for (int i = 0; i < base._animations.Size(); i++)
        {
            const AnimationType* anim = base._animations[i];
            if (anim)
                os << "    [" << i << "] " << Str(anim->GetName()) << "\n";
        }
    }

    if (const ManType* man = dynamic_cast<const ManType*>(&base))
    {
        const int moves = man->MoveIdN();
        os << "  move ids (" << moves << ") -- ordinal comes from CfgMoves >> States order\n";
        for (int i = 0; i < moves; i++)
            os << "    [" << i << "] " << Str(man->GetMoveName(i)) << "\n";
    }

    if (const BuildingType* building = dynamic_cast<const BuildingType*>(&base))
    {
        const int ladders = building->GetLadderCount();
        if (ladders)
        {
            os << "  ladders (" << ladders << ") -- index is saved and networked as _ladderIndex\n";
            for (int i = 0; i < ladders; i++)
                os << "    [" << i << "] top " << building->GetLadder(i)._top << "  bottom "
                   << building->GetLadder(i)._bottom << "\n";
        }
    }
}

} // namespace

bool DumpBanks(std::string& outPath, std::string& error)
{
    std::ofstream os;
    if (!OpenDump("banks", os, outPath, error))
        return false;

    os << "=== GFileBanks (" << GFileBanks.Size() << " banks) ===\n"
       << "Mount order is lookup priority: QIFStreamB::AutoBank returns the first\n"
       << "prefix match front to back, so index 0 wins. Boot loads mods in reverse\n"
       << "command-line order so the last -mod= entry lands nearest the front.\n\n";

    std::vector<std::string> seen;
    std::vector<std::string> shadowed;
    long long totalFiles = 0;
    long long totalStored = 0;

    for (int i = 0; i < GFileBanks.Size(); i++)
    {
        const QFBank& bank = GFileBanks[i];

        BankStats stats;
        bank.ForEach(AccumulateFile, &stats);
        totalFiles += stats.files;
        totalStored += stats.stored;

        const std::string prefix = Lower(Str(bank.GetPrefix()));
        const bool isShadowed = std::find(seen.begin(), seen.end(), prefix) != seen.end();
        if (isShadowed)
            shadowed.push_back(prefix);
        else
            seen.push_back(prefix);

        os << "[" << i << "] " << Str(bank.GetPrefix()) << "\n"
           << "    source     " << Str(bank.GetOpenName()) << "\n"
           << "    files      " << stats.files << " (" << stats.compressed << " compressed)\n"
           << "    size       " << Bytes(stats.stored) << " stored, " << Bytes(stats.uncompressed) << " uncompressed\n"
           << "    state      " << (bank.IsLocked() ? "locked" : "unlocked") << ", "
           << (bank.GetLockable() ? "lockable" : "not lockable") << ", "
           << (bank.CanBeUnloaded() ? "unloadable" : "in use") << (bank.error() ? ", ERROR" : "") << "\n";

        const RString product = bank.GetProperty("product");
        const RString version = bank.GetProperty("pboVersion");
        const RString encryption = bank.GetProperty("encryption");
        if (product.GetLength() || version.GetLength() || encryption.GetLength())
            os << "    properties product=" << Str(product) << " pboVersion=" << Str(version)
               << " encryption=" << Str(encryption) << "\n";

        if (isShadowed)
            os << "    SHADOWED   an earlier bank already claims this prefix; AutoBank never reaches this one\n";
        os << "\n";
    }

    os << "--- totals ---\n"
       << "banks    " << GFileBanks.Size() << "\n"
       << "files    " << totalFiles << "\n"
       << "stored   " << Bytes(totalStored) << "\n";
    if (!shadowed.empty())
    {
        os << "\n" << shadowed.size() << " shadowed bank(s), unreachable through AutoBank:\n";
        for (const std::string& s : shadowed)
            os << "  " << s << "\n";
    }
    os.close();

    LOG_INFO(Core, "[datadump] banks: {} banks, {} files, {} shadowed -> {}", GFileBanks.Size(), totalFiles,
             shadowed.size(), outPath);
    return true;
}

bool DumpConfig(const ConfigOptions& opt, std::string& outPath, std::string& error)
{
    ParamFile* root = RootOf(opt.root);
    if (!root)
    {
        error = "no such config root";
        return false;
    }

    const ParamClass* start = root;
    if (!opt.path.empty())
    {
        size_t at = 0;
        while (at <= opt.path.size())
        {
            const size_t dot = opt.path.find('.', at);
            const std::string part = opt.path.substr(at, dot == std::string::npos ? std::string::npos : dot - at);
            if (!part.empty())
            {
                const ParamEntry* next = start->FindEntry(RStringB(part.c_str()));
                if (!next || !next->IsClass())
                {
                    error = "no class '" + opt.path + "' in " + RootName(opt.root);
                    return false;
                }
                start = next->GetClassInterface();
            }
            if (dot == std::string::npos)
                break;
            at = dot + 1;
        }
    }

    std::ofstream os;
    if (!OpenDump("config", os, outPath, error))
        return false;

    ClassCounts totals;
    CountClass(*start, 0, totals);

    os << "=== " << RootName(opt.root);
    if (!opt.path.empty())
        os << " >> " << opt.path;
    os << " ===\n"
       << totals.classes << " classes, " << totals.entries << " entries, max depth " << totals.depth << "\n\n"
       << "<owner> is the addon that FIRST DEFINED the class. It is not override\n"
       << "attribution: ParamClass::Update stamps the owner only when it creates a\n"
       << "class, so a later addon overwriting members leaves the owner unchanged.\n"
       << "Scalars carry no owner at all -- ParamEntry::GetOwner always returns empty.\n"
       << "<-> means no owner was recorded (base game config, not an addon).\n\n";

    if (opt.maxDepth <= 0)
    {
        os << "--- top level summary ---\n";
        for (int i = 0; i < start->GetEntryCount(); i++)
        {
            const ParamEntry& e = start->GetEntry(i);
            if (!e.IsClass())
                continue;
            ClassCounts c;
            CountClass(*e.GetClassInterface(), 0, c);
            os << "  " << Str(e.GetName()) << "  " << c.classes << " classes, " << c.entries << " entries, depth "
               << c.depth << "\n";
        }
    }
    else
    {
        WalkClass(os, *start, 0, opt, "");
    }
    os.close();

    LOG_INFO(Core, "[datadump] config {}: {} classes, {} entries -> {}", RootName(opt.root), totals.classes,
             totals.entries, outPath);
    return true;
}


bool DumpTypes(std::string& outPath, std::string& error)
{
    std::ofstream os;
    if (!OpenDump("types", os, outPath, error))
        return false;

    os << "=== instantiated type banks ===\n"
       << "These are the objects the simulation actually runs on, built once from\n"
       << "config and cached by name. A config value that has already been copied\n"
       << "into one of these is not re-read. The engine does not record whether a\n"
       << "type was built by a Preload pass or lazily on first use.\n";

    os << "\n=== VehicleTypes (" << VehicleTypes.Size() << ") ===\n";
    for (int i = 0; i < VehicleTypes.Size(); i++)
        if (const EntityType* type = VehicleTypes[i])
            os << "  " << Str(type->GetName()) << "\n";

    os << "\n=== WeaponTypes (" << WeaponTypes.Size() << ") ===\n";
    for (int i = 0; i < WeaponTypes.Size(); i++)
        if (const WeaponType* type = WeaponTypes[i])
            os << "  " << Str(type->GetName()) << "\n";

    os << "\n=== MagazineTypes (" << MagazineTypes.Size() << ") ===\n";
    for (int i = 0; i < MagazineTypes.Size(); i++)
        if (const MagazineType* type = MagazineTypes[i])
            os << "  " << Str(type->GetName()) << "\n";

    os << "\n=== RoadTypes (" << RoadTypes.Size() << ") ===\n";
    for (int i = 0; i < RoadTypes.Size(); i++)
        if (const RoadType* type = RoadTypes[i])
            os << "  " << (type->GetName() ? type->GetName() : "(no shape)") << "\n";

    // MovesTypes is keyed by a shape plus motion-type pair rather than a class
    // name, and holds weak links that go null when the MovesType dies.
    os << "\n=== MovesTypes (" << MovesTypes.Size() << " slots) ===\n";
    for (int i = 0; i < MovesTypes.Size(); i++)
    {
        const MovesType* type = MovesTypes[i];
        if (!type)
        {
            os << "  [" << i << "] (released)\n";
            continue;
        }
        const LODShapeWithShadow* shape = type->GetName().shape;
        os << "  [" << i << "] " << (shape && shape->Name() ? shape->Name() : "(no shape)") << "\n";
    }

    DumpShapeNames(os);
    os.close();

    LOG_INFO(Core, "[datadump] types: {} vehicles, {} weapons, {} magazines, {} shapes -> {}", VehicleTypes.Size(),
             WeaponTypes.Size(), MagazineTypes.Size(), Shapes.Size(), outPath);
    return true;
}

bool DumpSlots(std::string_view classFilter, std::string& outPath, std::string& error)
{
    std::ofstream os;
    if (!OpenDump("slots", os, outPath, error))
        return false;

    os << "=== positional index tables ===\n"
       << "Every index below is derived from the ORDER of a config array, and each\n"
       << "one is stored on live objects, written into savegames, or sent over the\n"
       << "wire. Changing a scalar is safe. Adding or reordering an entry in\n"
       << "weapons[], muzzles[], modes[], Reflectors, Animations or ladders shifts\n"
       << "these indices and silently invalidates anything already holding one.\n"
       << "\nComputed from the type alone, so no mission needs to be running.\n";

    const std::string filter = Lower(std::string(classFilter).c_str());
    int matched = 0;
    int dumped = 0;
    for (int i = 0; i < VehicleTypes.Size(); i++)
    {
        const EntityType* type = VehicleTypes[i];
        if (!type)
            continue;
        if (!filter.empty() && Lower(Str(type->GetName())).find(filter) == std::string::npos)
            continue;
        matched++;
        // A type only reaches the bank when something instantiated it, and many
        // that do (crew proxies, static props) carry no positional table at all.
        if (!HasIndexTables(*type))
            continue;
        DumpOneType(os, *type);
        dumped++;
    }

    if (!dumped)
        os << "\n"
           << matched << " type(s) matched '" << std::string(classFilter)
           << "', none with positional index tables.\n"
              "Types appear here only once instantiated; load a mission that uses one to see it.\n";
    os.close();

    LOG_INFO(Core, "[datadump] slots: {} types -> {}", dumped, outPath);
    return true;
}

namespace
{

std::string NextWord(std::string_view& rest)
{
    while (!rest.empty() && (rest.front() == ' ' || rest.front() == '\t'))
        rest.remove_prefix(1);
    size_t end = 0;
    while (end < rest.size() && rest[end] != ' ' && rest[end] != '\t')
        end++;
    std::string word(rest.substr(0, end));
    rest.remove_prefix(end);
    return word;
}

void Report(bool ok, const std::string& path, const std::string& error, std::string& out)
{
    out = ok ? "written " + path : "failed: " + error;
}

void InvokeBanks(std::string_view, std::string& out)
{
    std::string path, error;
    Report(DumpBanks(path, error), path, error, out);
}

void InvokeConfig(std::string_view args, std::string& out)
{
    ConfigOptions opt;
    std::string_view rest = args;

    const std::string first = NextWord(rest);
    const std::string lowered = Lower(first.c_str());
    bool rootGiven = true;
    if (lowered == "pars")
        opt.root = ConfigRoot::Pars;
    else if (lowered == "res")
        opt.root = ConfigRoot::Res;
    else if (lowered == "remaster")
        opt.root = ConfigRoot::Remaster;
    else if (lowered == "mission")
        opt.root = ConfigRoot::Mission;
    else if (lowered == "campaign")
        opt.root = ConfigRoot::Campaign;
    else
        rootGiven = false;

    // Without a leading root name the first word is already the path.
    std::string next = rootGiven ? NextWord(rest) : first;
    // A bare number in the path slot is the depth, so the whole root can be
    // summarised without naming a class.
    const bool numeric = !next.empty() && next.find_first_not_of("0123456789") == std::string::npos;
    if (!numeric)
    {
        opt.path = next;
        next = NextWord(rest);
    }
    if (!next.empty())
        opt.maxDepth = atoi(next.c_str());

    std::string path, error;
    Report(DumpConfig(opt, path, error), path, error, out);
}

void InvokeTypes(std::string_view, std::string& out)
{
    std::string path, error;
    Report(DumpTypes(path, error), path, error, out);
}

void InvokeSlots(std::string_view args, std::string& out)
{
    std::string_view rest = args;
    const std::string filter = NextWord(rest);
    std::string path, error;
    Report(DumpSlots(filter, path, error), path, error, out);
}

} // namespace

void RegisterCommands()
{
    static bool registered = false;
    if (registered)
        return;
    registered = true;

    {
        DebugCommands::Register({"dumpbanks", "Dump mounted PBO banks in priority order, with shadowed banks flagged.",
                                 nullptr, InvokeBanks});
        DebugCommands::Register({"dumpconfig",
                                 "Dump a config tree: dumpconfig [pars|res|remaster|mission|campaign] "
                                 "[Class.Sub] [depth]. Depth 0 gives a top-level summary.",
                                 nullptr, InvokeConfig});
        DebugCommands::Register(
            {"dumptypes", "Dump the instantiated type banks and loaded shapes.", nullptr, InvokeTypes});
        DebugCommands::Register({"dumpslots",
                                 "Dump positional index tables (weapon slots, hit points, animations, "
                                 "move ids, ladders) for types matching <substring>.",
                                 nullptr, InvokeSlots});
    }
}

} // namespace DataDump
} // namespace Poseidon::Dev
