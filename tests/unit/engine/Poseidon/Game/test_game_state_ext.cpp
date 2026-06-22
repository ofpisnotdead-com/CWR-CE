#include <catch2/catch_test_macros.hpp>

#include <Poseidon/AI/AI.hpp>
#include <Poseidon/Game/Commands/GameStateExt.hpp>
#include <Poseidon/Game/Commands/GameStateExtCommon.hpp>
#include <Poseidon/Foundation/Modules/Modules.hpp>
#include <Poseidon/IO/Streams/QBStream.hpp>
#include <Poseidon/IO/Streams/QStream.hpp>
#include <Poseidon/IO/PackFiles.hpp>
#include <Poseidon/IO/Serialization/ParamArchive.hpp>
#include <Poseidon/Core/SaveVersion.hpp>
#include <Poseidon/Foundation/Common/GamePaths.hpp>
#include <Poseidon/Core/Global.hpp>

#include <fstream>
#include <ctime>
#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

#include "../test_fixtures.hpp"
#include <string.h>
#include <cstdio>
#include <iterator>
#include <system_error>
#include <Poseidon/Foundation/Containers/Array.hpp>
#include <Poseidon/Foundation/Strings/RString.hpp>
#include <Poseidon/Foundation/Types/Pointers.hpp>

using namespace Poseidon;
namespace Poseidon
{
bool IsAddonMetadataAccepted(const char* product, const char* encryption, const char* formatVersion,
                             bool encryptionRequired, const char** productList);
std::string BuildMPReportPathForUserDir(const std::string& userDir, const std::tm& tm);
bool DefaultAdvancedEditorMode();
bool LoadAdvancedEditorModeFromUserParams(const char* userParamsPath);
} // namespace Poseidon

namespace Poseidon
{
IFilebankEncryption* CreateEncryptXOR1024(const void* context);
}
namespace Poseidon
{
RString GetUserDirectory();
}
using Poseidon::GetUserDirectory;
GameValue ConfigNew(const GameState* state);
GameValue ConfigLoad(const GameState* state, GameValuePar oper1);
GameValue ConfigSave(const GameState* state, GameValuePar oper1, GameValuePar oper2);
GameValue ClassOpen(const GameState* state, GameValuePar oper1, GameValuePar oper2);
GameValue ClassAdd(const GameState* state, GameValuePar oper1, GameValuePar oper2);
GameValue ValueGet(const GameState* state, GameValuePar oper1, GameValuePar oper2);
GameValue ValueAdd(const GameState* state, GameValuePar oper1, GameValuePar oper2);
extern bool GUseFileBanks;

namespace
{
bool AcceptAllNames(const char*)
{
    return true;
}

bool ContainsName(const AutoArray<RStringS>& names, const char* needle)
{
    for (int i = 0; i < names.Size(); ++i)
    {
        if (strcmp((const char*)names[i], needle) == 0)
        {
            return true;
        }
    }
    return false;
}

struct AddonAcceptanceContext : public BankContextBase
{
    const char** productList;
    bool encryptionRequired;
    std::string seenProduct;
    std::string seenEncryption;
    std::string seenPboVersion;

    AddonAcceptanceContext(const char** productListIn, bool encryptionRequiredIn)
        : productList(productListIn), encryptionRequired(encryptionRequiredIn)
    {
    }
};

bool AcceptFixtureAddon(QFBank* bank, BankContextBase* context)
{
    auto* cfg = static_cast<AddonAcceptanceContext*>(context);
    cfg->seenProduct = static_cast<const char*>(bank->GetProperty("product"));
    cfg->seenEncryption = static_cast<const char*>(bank->GetProperty("encryption"));
    cfg->seenPboVersion = static_cast<const char*>(bank->GetProperty("pboVersion"));
    return Poseidon::IsAddonMetadataAccepted(bank->GetProperty("product"), bank->GetProperty("encryption"),
                                             bank->GetProperty("pboVersion"), cfg->encryptionRequired,
                                             cfg->productList);
}

std::string FixtureBankName(const char* fixturePath)
{
    std::string fullPath = GET_FIXTURE(fixturePath);
    return fullPath.substr(0, fullPath.size() - 4);
}

std::string ReadZeroTerminated(std::istream& stream)
{
    std::string value;
    for (;;)
    {
        char c = 0;
        stream.read(&c, 1);
        if (!stream || c == 0)
        {
            break;
        }
        value.push_back(c);
    }
    return value;
}

std::map<std::string, std::string> ReadRawPboProperties(const std::filesystem::path& path)
{
    std::ifstream file(path, std::ios::binary);
    REQUIRE(file.is_open());

    const std::string name = ReadZeroTerminated(file);
    REQUIRE(name.empty());

    int32_t compressedMagic = 0;
    int32_t uncompressedSize = 0;
    int32_t startOffset = 0;
    int32_t time = 0;
    int32_t length = 0;
    file.read(reinterpret_cast<char*>(&compressedMagic), sizeof(compressedMagic));
    file.read(reinterpret_cast<char*>(&uncompressedSize), sizeof(uncompressedSize));
    file.read(reinterpret_cast<char*>(&startOffset), sizeof(startOffset));
    file.read(reinterpret_cast<char*>(&time), sizeof(time));
    file.read(reinterpret_cast<char*>(&length), sizeof(length));
    REQUIRE(file.good());
    REQUIRE(compressedMagic == VersionMagic);
    REQUIRE(uncompressedSize == 0);
    REQUIRE(startOffset == 0);
    REQUIRE(time == 0);
    REQUIRE(length == 0);

    std::map<std::string, std::string> properties;
    for (;;)
    {
        const std::string propertyName = ReadZeroTerminated(file);
        if (propertyName.empty())
        {
            break;
        }
        properties[propertyName] = ReadZeroTerminated(file);
    }

    return properties;
}

std::filesystem::path CreateTempAddonBank(const std::string& stem, const std::string& cfgPatchesClass,
                                          const QFProperty* properties, int propertyCount, bool encrypted)
{
    const auto root = std::filesystem::temp_directory_path() / stem;
    const auto sourceDir = root / "src";
    const auto bankPath = root / (stem + ".pbo");

    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    std::filesystem::create_directories(sourceDir);

    {
        std::ofstream config(sourceDir / "config.cpp", std::ios::binary);
        REQUIRE(config.is_open());
        config << "class CfgPatches{class " << cfgPatchesClass << "{units[]={};weapons[]={};requiredVersion=1.0;};};\n";
    }

    FileBankManager manager;
    if (encrypted)
    {
        Ref<IFilebankEncryption> encryption = Poseidon::CreateEncryptXOR1024(nullptr);
        REQUIRE(encryption);

        QOFStream out(bankPath.string().c_str());
        manager.Create(out, sourceDir.string().c_str(), encryption, properties, propertyCount);
        out.close();
    }
    else
    {
        REQUIRE(manager.Create(bankPath.string().c_str(), sourceDir.string().c_str(), false, true, nullptr,
                               DefFileBankNoCompress, properties, propertyCount) == LSOK);
    }

    REQUIRE(std::filesystem::exists(bankPath));
    return bankPath;
}

std::vector<std::filesystem::path> ListMPReports()
{
    std::vector<std::filesystem::path> reports;
    const std::filesystem::path userDir(GamePaths::Instance().UserDir());
    if (!std::filesystem::exists(userDir))
        return reports;

    for (const auto& entry : std::filesystem::directory_iterator(userDir))
    {
        if (!entry.is_regular_file())
            continue;

        const std::string name = entry.path().filename().string();
        if (name.rfind("mpreport_", 0) == 0 && entry.path().extension() == ".txt")
            reports.push_back(entry.path());
    }

    return reports;
}

GameFileType ExtractFile(GameValuePar value)
{
    REQUIRE(value.GetType() == GameFile);
    return static_cast<GameDataFile*>(value.GetData())->GetFile();
}

std::filesystem::path ConfigStoragePath(const char* filename)
{
    return std::filesystem::path(static_cast<const char*>(GetUserDirectory())) / "Config" / filename;
}

GameValue MakeConfigAssignment(const GameState& state, const char* name, GameValue value)
{
    GameValue pair = state.CreateGameValue(GameArray);
    GameArrayType& items = pair;
    items.Resize(2);
    items[0] = RString(name);
    items[1] = value;
    return pair;
}

std::string BankConfigQuery(const std::string& stem)
{
#ifdef _WIN32
    return stem + "\\config.cpp";
#else
    return stem + "/config.cpp";
#endif
}

std::string BankLoadRoot(const std::filesystem::path& root)
{
    std::string path = root.string();
    path.push_back(std::filesystem::path::preferred_separator);
    return path;
}

struct ScopedMissionHeader
{
    std::string world;
    RString filenameReal;

    ScopedMissionHeader() : world(Glob.header.worldname), filenameReal(Glob.header.filenameReal) {}

    ~ScopedMissionHeader()
    {
        std::snprintf(Glob.header.worldname, sizeof(Glob.header.worldname), "%s", world.c_str());
        Glob.header.filenameReal = filenameReal;
    }
};

} // namespace

TEST_CASE("gameStateExt.hpp compiles", "[game][gameStateExt]")
{
    SUCCEED("header included successfully");
}

TEST_CASE("GameType constants have expected values", "[game][gameStateExt]")
{
    REQUIRE(GameObject == GameType(0x100));
    REQUIRE(GameVector == GameType(0x200));
    REQUIRE(GameTrans == GameType(0x400));
    REQUIRE(GameOrient == GameType(0x800));
    REQUIRE(GameSide == GameType(0x1000));
    REQUIRE(GameGroup == GameType(0x2000));
    REQUIRE(GameFile == GameType(0x4000));
}

TEST_CASE("GameType constants are distinct", "[game][gameStateExt]")
{
    REQUIRE(GameObject != GameVector);
    REQUIRE(GameVector != GameTrans);
    REQUIRE(GameTrans != GameOrient);
    REQUIRE(GameOrient != GameSide);
    REQUIRE(GameSide != GameGroup);
    REQUIRE(GameGroup != GameFile);
}

TEST_CASE("GameFileType default state", "[game][gameStateExt]")
{
    GameFileType ft;
    REQUIRE(ft.GetIndex() == -1);
    REQUIRE(ft.readOnly == false);
}

TEST_CASE("GameDataFile factory creates file-typed data", "[game][gameStateExt]")
{
    GameData* data = CreateGameDataFile();
    REQUIRE(data != nullptr);
    REQUIRE(data->GetType() == GameFile);
    REQUIRE(std::string(data->GetTypeName()) == "file");
    delete data;
}

TEST_CASE("VBS-derived functions remain registered in GGameState", "[game][gameStateExt]")
{
    GGameState.Reset();
    Poseidon::Foundation::InitModules();

    AutoArray<RStringS> functions;
    AutoArray<RStringS> operators;
    AutoArray<RStringS> nulars;

    GGameState.AppendFunctionList(functions, AcceptAllNames);
    GGameState.AppendOperatorList(operators, AcceptAllNames);
    GGameState.AppendNularOpList(nulars, AcceptAllNames);

    REQUIRE(ContainsName(nulars, "newConfig"));
    REQUIRE(ContainsName(functions, "loadConfig"));
    REQUIRE(ContainsName(functions, "VBS_addHeader"));
    REQUIRE(ContainsName(functions, "VBS_addEvent"));
    REQUIRE(ContainsName(functions, "VBS_addFooter"));
    REQUIRE(ContainsName(functions, "VBS_kills"));
    REQUIRE(ContainsName(functions, "VBS_killed"));
    REQUIRE(ContainsName(functions, "VBS_injuries"));
    REQUIRE(ContainsName(functions, "createGuardedPoint"));
    REQUIRE(ContainsName(functions, "deleteWaypoint"));
    REQUIRE(ContainsName(operators, "saveConfig"));
    REQUIRE(ContainsName(operators, "openClass"));
    REQUIRE(ContainsName(operators, "addClass"));
    REQUIRE(ContainsName(operators, "getValue"));
    REQUIRE(ContainsName(operators, "addValue"));
    REQUIRE(ContainsName(operators, "triggerAttachObject"));
    REQUIRE(ContainsName(operators, "triggerAttachVehicle"));
    REQUIRE(ContainsName(operators, "setEffectCondition"));
    REQUIRE(ContainsName(operators, "addWaypoint"));
}

TEST_CASE("XOR1024 encryption registers and round-trips data", "[game][gameStateExt][encryption]")
{
    static bool registered = false;
    if (!registered)
    {
        RegisterFilebankEncryption("UNIT_TEST_XOR1024", Poseidon::CreateEncryptXOR1024);
        registered = true;
    }

    Ref<IFilebankEncryption> encryption = CreateFilebankEncryption("UNIT_TEST_XOR1024", nullptr);
    REQUIRE(encryption);

    const std::string plain =
        "CfgPatches{class UnitTestEncryptedAddon{units[]={};weapons[]={};requiredVersion=1.0;};};";

    QOStream encoded;
    encryption->Encode(encoded, plain.data(), (long)plain.size());

    REQUIRE(encoded.pcount() >= (int)plain.size());
    REQUIRE((encoded.pcount() % 1024) == 0);

    std::vector<char> decoded(plain.size());
    QIStream input(encoded.str(), encoded.pcount());
    REQUIRE(encryption->Decode(decoded.data(), (long)decoded.size(), input));

    REQUIRE(std::string(decoded.begin(), decoded.end()) == plain);
}

TEST_CASE("XOR1024 fixture decodes example encrypted addon payload", "[game][gameStateExt][encryption]")
{
    Ref<IFilebankEncryption> encryption = Poseidon::CreateEncryptXOR1024(nullptr);
    REQUIRE(encryption);

    const std::string plain =
        "CfgPatches{class UnitTestEncryptedAddon{units[]={};weapons[]={};requiredVersion=1.0;};};";

    std::ifstream file(GET_FIXTURE("pbo/xor1024_cfgpatches.bin"), std::ios::binary);
    REQUIRE(file.is_open());

    std::vector<char> encoded((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    REQUIRE(encoded.size() == 1024);

    std::vector<char> decoded(plain.size());
    QIStream input(encoded.data(), (int)encoded.size());
    REQUIRE(encryption->Decode(decoded.data(), (long)decoded.size(), input));
    REQUIRE(std::string(decoded.begin(), decoded.end()) == plain);
}

TEST_CASE("Synthetic addon metadata is accepted as a normal product", "[game][gameStateExt][addons]")
{
    static const char* productList[] = {"Synthetic Base", "Synthetic Expansion", "Advanced Product", nullptr};

    REQUIRE(Poseidon::IsAddonMetadataAccepted("Advanced Product", "XOR1024", "", false, productList));
    REQUIRE(Poseidon::IsAddonMetadataAccepted("", "", "", false, productList));
    REQUIRE_FALSE(Poseidon::IsAddonMetadataAccepted("Unknown Product", "XOR1024", "", false, productList));
    REQUIRE_FALSE(Poseidon::IsAddonMetadataAccepted("Advanced Product", "XOR1024", "2", false, productList));
}

TEST_CASE("encryption-required rejects untagged addons", "[game][gameStateExt][addons][encryption]")
{
    static const char* productList[] = {"Synthetic Base", "Synthetic Expansion", "Advanced Product", nullptr};

    REQUIRE_FALSE(Poseidon::IsAddonMetadataAccepted("Advanced Product", "", "", true, productList));
    REQUIRE(Poseidon::IsAddonMetadataAccepted("Advanced Product", "XOR1024", "", true, productList));
}

TEST_CASE("MP report helper uses timestamped user-dir path", "[game][gameStateExt][report]")
{
    std::tm tm = {};
    tm.tm_year = 2024 - 1900;
    tm.tm_mon = 4;
    tm.tm_mday = 6;
    tm.tm_hour = 7;
    tm.tm_min = 8;
    tm.tm_sec = 9;

    REQUIRE(Poseidon::BuildMPReportPathForUserDir("/tmp/cwr/", tm) == "/tmp/cwr/mpreport_20240506_070809.txt");
    REQUIRE(Poseidon::BuildMPReportPathForUserDir("/tmp/cwr", tm) == "/tmp/cwr/mpreport_20240506_070809.txt");
}

TEST_CASE("advanced editor defaults on", "[game][gameStateExt][editor]")
{
    REQUIRE(Poseidon::DefaultAdvancedEditorMode());
}

TEST_CASE("advanced editor user params helper falls back to default and honors saved override",
          "[game][gameStateExt][editor]")
{
    const auto path = std::filesystem::path(GET_TEMP_FILE("advanced-editor-userinfo.cfg"));
    std::error_code ec;
    std::filesystem::remove(path, ec);

    REQUIRE(Poseidon::LoadAdvancedEditorModeFromUserParams(path.string().c_str()));

    bool disabled = false;
    ParamArchiveSave ar(UserInfoVersion);
    REQUIRE(ar.Serialize("advancedEditor", disabled, 1) == LSOK);
    REQUIRE(ar.Save(path.string().c_str()) == LSOK);

    REQUIRE_FALSE(Poseidon::LoadAdvancedEditorModeFromUserParams(path.string().c_str()));

    std::filesystem::remove(path, ec);
}

TEST_CASE("WriteMPReport writes timestamped report into user dir", "[game][gameStateExt][report]")
{
    const auto before = ListMPReports();
    ScopedMissionHeader restoreHeader;

    std::snprintf(Glob.header.worldname, sizeof(Glob.header.worldname), "%s", "eden");
    Glob.header.filenameReal = "unit_test_report";

    AIStatsMission mission;
    mission.OnMissionStart();
    mission.WriteMPReport();

    const auto after = ListMPReports();
    REQUIRE(after.size() == before.size() + 1);

    std::filesystem::path reportPath;
    for (const auto& path : after)
    {
        if (std::find(before.begin(), before.end(), path) == before.end())
        {
            reportPath = path;
            break;
        }
    }

    REQUIRE(!reportPath.empty());

    // Scope the ifstream so its destructor closes the handle before
    // remove() — on Windows, removing an open file silently fails
    // (the error_code is ignored), leaking the file into the next
    // [report] test's `before` listing.
    std::string contents;
    {
        std::ifstream file(reportPath);
        REQUIRE(file.is_open());
        contents.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
    }
    REQUIRE(contents.find("----------**Start**----------") != std::string::npos);
    REQUIRE(contents.find("Mission: unit_test_report.eden") != std::string::npos);
    REQUIRE(contents.find("----------**Summary**----------") != std::string::npos);

    std::error_code ec;
    std::filesystem::remove(reportPath, ec);
}

TEST_CASE("WriteMPReport writes one report per mission", "[game][gameStateExt][report]")
{
    const auto before = ListMPReports();
    ScopedMissionHeader restoreHeader;

    std::snprintf(Glob.header.worldname, sizeof(Glob.header.worldname), "%s", "eden");
    Glob.header.filenameReal = "unit_test_report_once";

    AIStatsMission mission;
    mission.OnMissionStart();
    mission.WriteMPReport();
    mission.WriteMPReport();

    const auto after = ListMPReports();
    REQUIRE(after.size() == before.size() + 1);

    std::filesystem::path reportPath;
    for (const auto& path : after)
    {
        if (std::find(before.begin(), before.end(), path) == before.end())
        {
            reportPath = path;
            break;
        }
    }

    REQUIRE(!reportPath.empty());
    std::error_code ec;
    std::filesystem::remove(reportPath, ec);
}

TEST_CASE("WriteMPReport includes custom header event and footer lines", "[game][gameStateExt][report]")
{
    const auto before = ListMPReports();
    ScopedMissionHeader restoreHeader;

    std::snprintf(Glob.header.worldname, sizeof(Glob.header.worldname), "%s", "eden");
    Glob.header.filenameReal = "unit_test_report_details";

    AIStatsMission mission;
    mission.OnMissionStart();
    mission.AddReportHeader("Header line");
    mission.AddReportEvent("User event line");
    mission.AddReportFooter("Footer line");
    mission.WriteMPReport();

    const auto after = ListMPReports();
    REQUIRE(after.size() == before.size() + 1);

    std::filesystem::path reportPath;
    for (const auto& path : after)
    {
        if (std::find(before.begin(), before.end(), path) == before.end())
        {
            reportPath = path;
            break;
        }
    }

    REQUIRE(!reportPath.empty());

    // Scope the ifstream — see comment in "writes timestamped report"
    // test above.  Windows refuses remove() on open file handles.
    std::string contents;
    {
        std::ifstream file(reportPath);
        REQUIRE(file.is_open());
        contents.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
    }
    REQUIRE(contents.find("Header line") != std::string::npos);
    REQUIRE(contents.find("User event line") != std::string::npos);
    REQUIRE(contents.find("Footer line") != std::string::npos);

    std::error_code ec;
    std::filesystem::remove(reportPath, ec);
}

TEST_CASE("FILE config commands can create mutate save and reload config trees", "[game][gameStateExt][config]")
{
    GGameState.Reset();
    Poseidon::Foundation::InitModules();

    const char* filename = "unit_test_file_commands.cfg";
    const auto path = ConfigStoragePath(filename);
    std::error_code ec;
    std::filesystem::remove(path, ec);

    GameValue root = ConfigNew(&GGameState);
    const GameFileType rootFile = ExtractFile(root);
    REQUIRE(rootFile.GetIndex() >= 0);
    REQUIRE(rootFile.readOnly == false);

    GameValue cls = ClassAdd(&GGameState, root, RString("UnitTest"));
    const GameFileType classFile = ExtractFile(cls);
    REQUIRE(classFile.GetIndex() >= 0);

    ValueAdd(&GGameState, cls, MakeConfigAssignment(GGameState, "numberValue", GameValue(42.0f)));
    ValueAdd(&GGameState, cls, MakeConfigAssignment(GGameState, "textValue", GameValue(RString("alpha"))));

    GameValue nested = GGameState.CreateGameValue(GameArray);
    GameArrayType& nestedItems = nested;
    nestedItems.Resize(3);
    nestedItems[0] = 1.0f;
    nestedItems[1] = RString("two");
    nestedItems[2] = 3.0f;
    ValueAdd(&GGameState, cls, MakeConfigAssignment(GGameState, "arrayValue", nested));

    GameValue numberValue = ValueGet(&GGameState, cls, RString("numberValue"));
    REQUIRE(numberValue.GetType() == GameScalar);
    REQUIRE((float)numberValue == 42.0f);

    GameValue textValue = ValueGet(&GGameState, cls, RString("textValue"));
    REQUIRE(textValue.GetType() == GameString);
    REQUIRE(std::string((const char*)(RString)textValue) == "alpha");

    GameValue arrayValue = ValueGet(&GGameState, cls, RString("arrayValue"));
    REQUIRE(arrayValue.GetType() == GameArray);
    const GameArrayType& arrayItems = arrayValue;
    REQUIRE(arrayItems.Size() == 3);
    REQUIRE((float)arrayItems[0] == 1.0f);
    REQUIRE(std::string((const char*)(RString)arrayItems[1]) == "two");
    REQUIRE((float)arrayItems[2] == 3.0f);

    ConfigSave(&GGameState, root, RString(filename));
    REQUIRE(std::filesystem::exists(path));

    GameValue loadedRoot = ConfigLoad(&GGameState, RString(filename));
    const GameFileType loadedRootFile = ExtractFile(loadedRoot);
    REQUIRE(loadedRootFile.GetIndex() >= 0);
    REQUIRE(loadedRootFile.readOnly == false);

    GameValue loadedClass = ClassOpen(&GGameState, loadedRoot, RString("UnitTest"));
    const GameFileType loadedClassFile = ExtractFile(loadedClass);
    REQUIRE(loadedClassFile.GetIndex() >= 0);
    REQUIRE(loadedClassFile.readOnly == false);

    GameValue loadedNumber = ValueGet(&GGameState, loadedClass, RString("numberValue"));
    REQUIRE(loadedNumber.GetType() == GameScalar);
    REQUIRE((float)loadedNumber == 42.0f);

    GameValue loadedText = ValueGet(&GGameState, loadedClass, RString("textValue"));
    REQUIRE(loadedText.GetType() == GameString);
    REQUIRE(std::string((const char*)(RString)loadedText) == "alpha");

    GameValue loadedArray = ValueGet(&GGameState, loadedClass, RString("arrayValue"));
    REQUIRE(loadedArray.GetType() == GameArray);
    const GameArrayType& loadedItems = loadedArray;
    REQUIRE(loadedItems.Size() == 3);
    REQUIRE((float)loadedItems[0] == 1.0f);
    REQUIRE(std::string((const char*)(RString)loadedItems[1]) == "two");
    REQUIRE((float)loadedItems[2] == 3.0f);

    ValueAdd(&GGameState, loadedClass, MakeConfigAssignment(GGameState, "blocked", GameValue(7.0f)));
    GameValue missing = ValueGet(&GGameState, loadedClass, RString("blocked"));
    REQUIRE(missing.GetType() == GameScalar);
    REQUIRE((float)missing == 7.0f);

    std::filesystem::remove(path, ec);
}

TEST_CASE("Advanced product PBO exposes metadata and config payload", "[game][gameStateExt][pbo][addons]")
{
    const QFProperty sourceProperties[] = {
        {"product", "Advanced Product"},
    };
    const auto bankPath = CreateTempAddonBank("poseidon_advanced_product_ok", "UnitTestGeneratedAdvancedAddon",
                                              sourceProperties, std::size(sourceProperties), false);

    const auto properties = ReadRawPboProperties(bankPath.string());
    REQUIRE(properties.at("product") == "Advanced Product");

    QFBank bank;
    REQUIRE(bank.open(RString(bankPath.string().substr(0, bankPath.string().size() - 4).c_str())));

    bank.Lock();
    REQUIRE_FALSE(bank.error());

    Ref<IFileBuffer> config = bank.Read("config.cpp");
    REQUIRE(config);
    const std::string contents(static_cast<const char*>(config->GetData()), config->GetSize());
    REQUIRE(contents.find("UnitTestGeneratedAdvancedAddon") != std::string::npos);
    bank.Unlock();

    std::error_code ec;
    std::filesystem::remove_all(bankPath.parent_path(), ec);
}

TEST_CASE("Advanced product PBO follows addon acceptance rules", "[game][gameStateExt][pbo][addons]")
{
    static const char* productList[] = {"Synthetic Base", "Synthetic Expansion", "Advanced Product", nullptr};
    const QFProperty properties[] = {
        {"product", "Advanced Product"},
    };

    SECTION("accepted without encryption requirement")
    {
        const auto bankPath = CreateTempAddonBank("poseidon_advanced_product_ok", "UnitTestGeneratedAdvancedAddon",
                                                  properties, std::size(properties), false);
        Ref<AddonAcceptanceContext> context = new AddonAcceptanceContext{productList, false};
        QFBank bank;
        REQUIRE(bank.open(RString(bankPath.string().substr(0, bankPath.string().size() - 4).c_str()),
                          AcceptFixtureAddon, context));
        bank.Lock();
        REQUIRE_FALSE(bank.error());
        REQUIRE(context->seenProduct == "Advanced Product");
        REQUIRE(context->seenEncryption.empty());
        bank.Unlock();

        std::error_code ec;
        std::filesystem::remove_all(bankPath.parent_path(), ec);
    }

    SECTION("rejected when encryption metadata is required")
    {
        const auto bankPath =
            CreateTempAddonBank("poseidon_advanced_product_needs_encryption", "UnitTestGeneratedAdvancedAddon",
                                properties, std::size(properties), false);
        Ref<AddonAcceptanceContext> context = new AddonAcceptanceContext{productList, true};
        QFBank bank;
        REQUIRE(bank.open(RString(bankPath.string().substr(0, bankPath.string().size() - 4).c_str()),
                          AcceptFixtureAddon, context));
        bank.Lock();
        REQUIRE(bank.error());
        REQUIRE(context->seenProduct == "Advanced Product");
        REQUIRE(context->seenEncryption.empty());

        std::error_code ec;
        std::filesystem::remove_all(bankPath.parent_path(), ec);
    }

    SECTION("literal legacy product remains rejected")
    {
        const QFProperty legacyProperties[] = {
            {"product", "Legacy Product"},
        };
        const auto bankPath = CreateTempAddonBank("poseidon_advanced_literal_product", "UnitTestGeneratedLegacyAddon",
                                                  legacyProperties, std::size(legacyProperties), false);
        Ref<AddonAcceptanceContext> context = new AddonAcceptanceContext{productList, false};
        QFBank bank;
        REQUIRE(bank.open(RString(bankPath.string().substr(0, bankPath.string().size() - 4).c_str()),
                          AcceptFixtureAddon, context));
        bank.Lock();
        REQUIRE(bank.error());
        REQUIRE(context->seenProduct == "Legacy Product");

        std::error_code ec;
        std::filesystem::remove_all(bankPath.parent_path(), ec);
    }
}

TEST_CASE("Encrypted advanced product PBO passes metadata gate and decrypts payload",
          "[game][gameStateExt][pbo][addons][encryption]")
{
    static bool registered = false;
    if (!registered)
    {
        RegisterFilebankEncryption("XOR1024", Poseidon::CreateEncryptXOR1024);
        registered = true;
    }

    static const char* productList[] = {"Synthetic Base", "Synthetic Expansion", "Advanced Product", nullptr};
    const QFProperty properties[] = {
        {"product", "Advanced Product"},
        {"encryption", "XOR1024"},
    };
    const auto bankPath = CreateTempAddonBank("poseidon_advanced_encrypted_xor1024", "UnitTestAdvancedEncryptedAddon",
                                              properties, std::size(properties), true);
    Ref<AddonAcceptanceContext> context = new AddonAcceptanceContext{productList, true};

    QFBank bank;
    REQUIRE(bank.open(RString(bankPath.string().substr(0, bankPath.string().size() - 4).c_str()), AcceptFixtureAddon,
                      context));
    bank.Lock();
    REQUIRE_FALSE(bank.error());
    REQUIRE(context->seenProduct == "Advanced Product");
    REQUIRE(context->seenEncryption == "XOR1024");

    Ref<IFileBuffer> config = bank.Read("config.cpp");
    REQUIRE(config);
    const std::string contents(static_cast<const char*>(config->GetData()), config->GetSize());
    REQUIRE(contents.find("UnitTestAdvancedEncryptedAddon") != std::string::npos);
    bank.Unlock();

    std::error_code ec;
    std::filesystem::remove_all(bankPath.parent_path(), ec);
}

TEST_CASE("Addon metadata banks load through GFileBanks and QIFStreamB",
          "[game][gameStateExt][pbo][addons][integration]")
{
    static const char* productList[] = {"Synthetic Base", "Synthetic Expansion", "Advanced Product", nullptr};
    const int origSize = GFileBanks.Size();
    const bool origUseFileBanks = GUseFileBanks;
    GUseFileBanks = true;

    SECTION("accepted advanced bank is reachable through bank list lookup")
    {
        const QFProperty properties[] = {
            {"product", "Advanced Product"},
        };
        const std::string stem = "poseidon_advanced_banklist_ok";
        const auto bankPath =
            CreateTempAddonBank(stem, "UnitTestBankListAddon", properties, std::size(properties), false);
        const auto root = bankPath.parent_path();

        Ref<AddonAcceptanceContext> context = new AddonAcceptanceContext(productList, false);
        GFileBanks.Load(RString(BankLoadRoot(root).c_str()), "", stem.c_str(), true, AcceptFixtureAddon, nullptr,
                        context);

        REQUIRE(GFileBanks.Size() == origSize + 1);
        REQUIRE(QIFStreamB::FileExist(BankConfigQuery(stem).c_str()));

        QIFStreamB stream;
        stream.AutoOpen(BankConfigQuery(stem).c_str());
        REQUIRE_FALSE(stream.fail());
        REQUIRE(stream.rest() > 0);

        while (GFileBanks.Size() > origSize)
            GFileBanks.Delete(GFileBanks.Size() - 1);

        std::error_code ec;
        std::filesystem::remove_all(root, ec);
    }

    SECTION("rejected pboVersion bank never enters the global bank list")
    {
        const QFProperty properties[] = {
            {"product", "Advanced Product"},
            {"pboVersion", "2"},
        };
        const std::string stem = "poseidon_advanced_banklist_bad_version";
        const auto bankPath =
            CreateTempAddonBank(stem, "UnitTestBadVersionAddon", properties, std::size(properties), false);
        const auto root = bankPath.parent_path();

        Ref<AddonAcceptanceContext> context = new AddonAcceptanceContext(productList, false);
        GFileBanks.Load(RString(BankLoadRoot(root).c_str()), "", stem.c_str(), true, AcceptFixtureAddon, nullptr,
                        context);

        REQUIRE_FALSE(QIFStreamB::FileExist(BankConfigQuery(stem).c_str()));

        while (GFileBanks.Size() > origSize)
            GFileBanks.Delete(GFileBanks.Size() - 1);

        std::error_code ec;
        std::filesystem::remove_all(root, ec);
    }

    GUseFileBanks = origUseFileBanks;
}
