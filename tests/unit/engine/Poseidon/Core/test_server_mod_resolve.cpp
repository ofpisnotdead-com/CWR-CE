// ServerModResolver decides, when the player clicks Join on a modded server, which
// of the server's required mods are already present, which to download, which to
// disable (exact-match servers only), and which can't be auto-fetched. It's the
// pure OOP core of the MP-join flow. A mod is referred to
// as an absolute path, '@name', or bare name, in any case — `ModId` normalizes them
// all so matching is robust.

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <initializer_list>

#include <Poseidon/Core/ModCollection.hpp>
#include <Poseidon/Core/ModId.hpp>
#include <Poseidon/Core/ServerModResolve.hpp>

using Poseidon::InstalledModPackage;
using Poseidon::ModCatalog;
using Poseidon::ModCatalogEntry;
using Poseidon::ModDownload;
using Poseidon::ModId;
using Poseidon::ServerModList;
using Poseidon::ServerModResolver;

namespace
{
std::vector<ModId> Ids(std::initializer_list<const char*> tokens)
{
    std::vector<ModId> v;
    for (const char* t : tokens)
    {
        v.emplace_back(t);
    }
    return v;
}
bool Has(const std::vector<ModId>& v, const char* id)
{
    return std::any_of(v.begin(), v.end(), [&](const ModId& m) { return m == ModId(id); });
}
bool HasDownload(const std::vector<ModDownload>& v, const char* id)
{
    return std::any_of(v.begin(), v.end(), [&](const ModDownload& d) { return d.id == ModId(id); });
}
ModCatalog Catalog()
{
    ModCatalog c;
    c.Add(ModCatalogEntry(ModId("csla"), "CSLA Core", "https://papa-bear.cz/dl/csla.pbo", 88'000'000));
    c.Add(ModCatalogEntry(ModId("csla_islands"), "CSLA Islands", "https://papa-bear.cz/dl/csla_islands.pbo",
                          110'000'000));
    c.Add(ModCatalogEntry(ModId("csla_sounds"), "CSLA Sounds", "https://papa-bear.cz/dl/csla_sounds.pbo", 12'000'000,
                          "@CSLASounds"));
    return c;
}
} // namespace

TEST_CASE("ModId - strips path, '@', case, whitespace", "[mods][resolve]")
{
    REQUIRE(ModId("@CSLA").Value() == "csla");
    REQUIRE(ModId("csla").Value() == "csla");
    REQUIRE(ModId("  @CSLA_Sounds  ").Value() == "csla_sounds");
    REQUIRE(ModId("C:\\Game\\Workshop\\@CSLA").Value() == "csla");
    REQUIRE(ModId("/home/u/.local/Mods/@csla_islands").Value() == "csla_islands");
    REQUIRE(ModId("").Empty());
    REQUIRE(ModId("   ").Empty());
    REQUIRE(ModId("@CSLA") == ModId("c:\\x\\csla")); // different shapes, same identity
}

TEST_CASE("ServerModList - parses, normalizes, dedupes, carries exact-match", "[mods][resolve]")
{
    const ServerModList s("@CSLA;C:\\Workshop\\@csla;  @CSLA_Islands ;;", /*equalMod*/ true);
    REQUIRE(s.Required().size() == 2); // the two @csla tokens collapse to one
    REQUIRE(s.Required()[0] == ModId("csla"));
    REQUIRE(s.Required()[1] == ModId("csla_islands"));
    REQUIRE(s.Requires(ModId("@CSLA")));
    REQUIRE_FALSE(s.Requires(ModId("ffur1985")));
    REQUIRE(s.RequiresExactMatch());

    REQUIRE(ServerModList("", false).Empty());
    REQUIRE(ServerModList(";;;", false).Empty());
}

TEST_CASE("ModCollection - MountPathForIds resolves normalized ids to on-disk paths", "[mods][resolve]")
{
    // The MP-join apply builds the engine mod path for a server's required set: each
    // required id (normalized, no '@') resolves to its on-disk '@folder' path, in order.
    Poseidon::ModCollection c;
    Poseidon::Mod a;
    a.id = "@csla";
    a.path = "C:/Workshop/@csla";
    a.source = Poseidon::ModSource::Workshop;
    c.Add(a);
    Poseidon::Mod b;
    b.id = "@csla_islands";
    b.path = "C:/Workshop/@csla_islands";
    c.Add(b);

    REQUIRE(c.MountPathForIds({ModId("CSLA_Islands"), ModId("@csla")}) ==
            "C:/Workshop/@csla_islands;C:/Workshop/@csla");
    REQUIRE(c.MountPathForIds({ModId("ghost")}).empty()); // not on disk → skipped
}

TEST_CASE("ModCatalog - case/path-agnostic lookup, dedupes", "[mods][resolve]")
{
    const ModCatalog c = Catalog();
    REQUIRE(c.Size() == 3);
    REQUIRE(c.Contains(ModId("C:\\srv\\@CSLA")));
    REQUIRE(c.Find(ModId("csla_sounds"))->SizeBytes() == 12'000'000);
    REQUIRE(c.Find(ModId("nope")) == nullptr);
}

TEST_CASE("ServerModResolver - all required already installed -> no work, can connect", "[mods][resolve]")
{
    const ServerModResolver r(Ids({"@csla", "@csla_islands"}), Ids({"@csla", "@csla_islands"}));
    const auto res = r.Resolve(ServerModList("@csla;@csla_islands", true), Catalog());
    REQUIRE(res.Satisfied().size() == 2);
    REQUIRE(res.ToDownload().empty());
    REQUIRE(res.ToDisable().empty());
    REQUIRE(res.Blocked().empty());
    REQUIRE_FALSE(res.NeedsWork());
    REQUIRE(res.CanProceed());
}

TEST_CASE("ServerModResolver - missing-but-in-catalog -> download with url + size", "[mods][resolve]")
{
    const ServerModResolver r(Ids({"@csla"}), Ids({"@csla"}));
    const auto res = r.Resolve(ServerModList("@csla;@csla_sounds", false), Catalog());
    REQUIRE(Has(res.Satisfied(), "csla"));
    REQUIRE(res.ToDownload().size() == 1);
    REQUIRE(res.ToDownload()[0].id == ModId("csla_sounds"));
    REQUIRE(res.ToDownload()[0].downloadUrl == "https://papa-bear.cz/dl/csla_sounds.pbo");
    REQUIRE(res.ToDownload()[0].folderName == "@CSLASounds");
    REQUIRE(res.ToDownload()[0].sizeBytes == 12'000'000);
    REQUIRE(res.ToDownload()[0].name == "CSLA Sounds");
    REQUIRE(res.Blocked().empty());
    REQUIRE(res.NeedsWork());
    REQUIRE(res.CanProceed());
}

TEST_CASE("ServerModResolver - required absent and not in catalog -> blocked, cannot proceed", "[mods][resolve]")
{
    const ServerModResolver r(Ids({"@csla"}), Ids({"@csla"}));
    const auto res = r.Resolve(ServerModList("@csla;@pristar_private", false), Catalog());
    REQUIRE(Has(res.Satisfied(), "csla"));
    REQUIRE(res.ToDownload().empty());
    REQUIRE(res.Blocked().size() == 1);
    REQUIRE(res.Blocked()[0] == ModId("pristar_private"));
    REQUIRE(res.NeedsWork());
    REQUIRE_FALSE(res.CanProceed()); // teeth: a blocked mod must block the join
}

TEST_CASE("ServerModResolver - exact-match server disables active extras", "[mods][resolve]")
{
    const ServerModResolver r(Ids({"@csla", "@ffur1985"}), Ids({"@csla", "@ffur1985"}));
    const auto res = r.Resolve(ServerModList("@csla", /*equalMod*/ true), Catalog());
    REQUIRE(Has(res.Satisfied(), "csla"));
    REQUIRE(res.ToDisable().size() == 1);
    REQUIRE(res.ToDisable()[0] == ModId("ffur1985"));
}

TEST_CASE("ServerModResolver - non-exact server keeps active extras (no disable)", "[mods][resolve]")
{
    const ServerModResolver r(Ids({"@csla", "@ffur1985"}), Ids({"@csla", "@ffur1985"}));
    const auto res = r.Resolve(ServerModList("@csla", /*equalMod*/ false), Catalog());
    REQUIRE(res.ToDisable().empty()); // teeth: only equalModRequired forces extras off
}

TEST_CASE("ServerModResolver - matching is case-insensitive and path/@-agnostic", "[mods][resolve]")
{
    // Server advertises absolute paths in mixed case; installed/active differ in case + '@'.
    const ServerModResolver r(Ids({"CSLA"}), Ids({"csla"}));
    const auto res = r.Resolve(ServerModList("C:\\Srv\\@CSLA;C:\\Srv\\@CSLA_Sounds", true), Catalog());
    REQUIRE(Has(res.Satisfied(), "csla"));                 // @CSLA path <-> installed "CSLA"
    REQUIRE(HasDownload(res.ToDownload(), "csla_sounds")); // @CSLA_Sounds <-> catalog "csla_sounds"
    REQUIRE(res.ToDisable().empty());                      // csla is required, so it's not an extra
}

TEST_CASE("DecideMpJoinAction - picks the right browser-Join action", "[mods][resolve][mpjoin]")
{
    using Poseidon::DecideMpJoinAction;
    using Poseidon::MpJoinAction;
    const ServerModResolver r(Ids({"@csla"}), Ids({"@csla"}));

    // missing-but-in-catalog → download
    REQUIRE(DecideMpJoinAction(r.Resolve(ServerModList("@csla;@csla_sounds", false), Catalog()), false) ==
            MpJoinAction::Download);
    // required mod not in catalog → blocked
    REQUIRE(DecideMpJoinAction(r.Resolve(ServerModList("@csla;@ghost", false), Catalog()), false) ==
            MpJoinAction::Blocked);
    // all present, NOT already mounted → apply (re-mount) then connect
    REQUIRE(DecideMpJoinAction(r.Resolve(ServerModList("@csla", false), Catalog()), /*alreadyMounted*/ false) ==
            MpJoinAction::ApplyThenConnect);
    // all present AND already mounted → connect directly (no re-mount)
    REQUIRE(DecideMpJoinAction(r.Resolve(ServerModList("@csla", false), Catalog()), /*alreadyMounted*/ true) ==
            MpJoinAction::ConnectDirect);
    // teeth: a blocked set must block even if "already mounted" is claimed
    REQUIRE(DecideMpJoinAction(r.Resolve(ServerModList("@ghost", false), Catalog()), true) == MpJoinAction::Blocked);
}

TEST_CASE("ServerModResolver - empty server requirement -> nothing to do", "[mods][resolve]")
{
    const ServerModResolver r(Ids({"@csla"}), Ids({"@csla"}));
    const auto res = r.Resolve(ServerModList("", false), Catalog());
    REQUIRE(res.Satisfied().empty());
    REQUIRE(res.ToDownload().empty());
    REQUIRE(res.Blocked().empty());
    REQUIRE_FALSE(res.NeedsWork());

    // ...but an empty exact-match server would disable everything the player has on.
    const auto res2 = r.Resolve(ServerModList("", true), Catalog());
    REQUIRE(res2.ToDisable().size() == 1);
    REQUIRE(res2.ToDisable()[0] == ModId("csla"));
}

TEST_CASE("ServerModResolver - exact package revisions never downgrade", "[mods][resolve][revision]")
{
    ModCatalog catalog;
    catalog.Add(
        ModCatalogEntry(ModId("stable"), "Stable", "https://example.invalid/stable", 42, "@stable", 3, "abcd", "1.0"));

    const std::vector<InstalledModPackage> oldInstall = {{ModId("stable"), 2}};
    const ServerModResolver oldClient(oldInstall, Ids({"stable"}));
    const auto update = oldClient.Resolve(ServerModList({{"stable", 3}}, false), catalog);
    REQUIRE(update.ToDownload().size() == 1);
    REQUIRE(update.ToDownload()[0].packageRevision == 3);

    const std::vector<InstalledModPackage> latestInstall = {{ModId("stable"), 3}};
    const ServerModResolver latestClient(latestInstall, Ids({"stable"}));
    const auto oldServer = latestClient.Resolve(ServerModList({{"stable", 2}}, false), catalog);
    REQUIRE_FALSE(oldServer.CanProceed());
    REQUIRE(oldServer.ToDownload().empty());

    const auto alreadyOld = oldClient.Resolve(ServerModList({{"stable", 2}}, false), catalog);
    REQUIRE_FALSE(alreadyOld.CanProceed());

    const ModCatalog unreachable;
    const auto exactInstalled = latestClient.Resolve(ServerModList({{"stable", 3}}, false), unreachable);
    REQUIRE(exactInstalled.CanProceed());
    REQUIRE(exactInstalled.Satisfied().size() == 1);

    ModCatalog reachableWithoutPackage;
    reachableWithoutPackage.MarkReachable();
    const auto unavailable = latestClient.Resolve(ServerModList({{"stable", 3}}, false), reachableWithoutPackage);
    REQUIRE_FALSE(unavailable.CanProceed());
}
