// The PAPA BEAR master path must carry the host's build tag end to end: the publisher
// serialises it under the wire key "vertag" in the registration JSON, the master stores
// and serves it, and the browser parses it back into a session. This locks the C++ ends
// of that contract (the Rust master crate has its own roundtrip tests).
//
// Broken state without the wiring: BuildMasterServerServiceRegistrationJson omits "vertag"
// (the registration struct has no field), and a served row's "vertag" parses to an empty
// versionTag.

#include <Poseidon/Foundation/PoseidonPCH.hpp>

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

#include <Poseidon/Network/NetworkConfig.hpp>
#include <Poseidon/Network/MasterServerServiceClient.hpp>

using namespace Poseidon;

TEST_CASE("master registration JSON serializes the version tag under 'vertag'", "[network][master][version]")
{
    MasterServerServiceRegistration registration;
    registration.app = "CWR-CE";
    registration.address = "10.0.0.5";
    registration.hostPort = 2302;
    registration.serverId = BuildMasterServerServiceServerId(registration.address, registration.hostPort);
    registration.hostName = "Tagged Server";
    registration.actualVersion = 196;
    registration.requiredVersion = 196;
    registration.versionTag = "rc1";
    registration.mod = "cwr";

    const std::string json = BuildMasterServerServiceRegistrationJson(registration);

    // The wire key is "vertag" with the registered value — the same key the Rust master
    // ingests and the C++ join validator's tag is compared against.
    REQUIRE(json.find("\"vertag\":\"rc1\"") != std::string::npos);
    REQUIRE(json.find("\"app\":\"CWR-CE\"") != std::string::npos);
}

TEST_CASE("master server-list parse roundtrips the version tag", "[network][master][version]")
{
    // A served list row, as the master returns it (the "vertag" key alongside actver/reqver).
    const char* listJson = "[{\"address\":\"10.0.0.5\",\"hostport\":2302,\"hostname\":\"Tagged Server\","
                           "\"gametype\":\"coop\",\"app\":\"CWR-CE\",\"actver\":196,\"reqver\":196,\"vertag\":\"rc1\","
                           "\"state\":14,\"numplayers\":1,\"maxplayers\":8,\"password\":false,"
                           "\"mod\":\"cwr\",\"equalModRequired\":false,\"impl\":\"papa-bear\"}]";

    std::vector<MasterServerServiceSession> sessions;
    REQUIRE(ParseMasterServerServiceListResponse(listJson, sessions));
    REQUIRE(sessions.size() == 1);
    REQUIRE(sessions[0].app == "CWR-CE");
    REQUIRE(sessions[0].versionTag == "rc1");

    // The browser projects the session into a MasterServerSessionInfo (the struct the
    // DisplayUIMultiplayer copy loop reads versionTag from); the tag carries through.
    MasterServerSessionInfo info;
    AssignMasterServerServiceSessionInfo(sessions[0], info);
    REQUIRE(std::string(info.versionTag) == "rc1");

    // A row the master serves without a tag (older host) parses to an empty tag, not garbage.
    const char* untaggedJson = "[{\"address\":\"10.0.0.6\",\"hostport\":2303,\"hostname\":\"Old Server\","
                               "\"gametype\":\"coop\",\"actver\":196,\"reqver\":196,"
                               "\"state\":14,\"numplayers\":0,\"maxplayers\":8,\"password\":false,"
                               "\"mod\":\"\",\"equalModRequired\":false,\"impl\":\"papa-bear\"}]";
    std::vector<MasterServerServiceSession> untagged;
    REQUIRE(ParseMasterServerServiceListResponse(untaggedJson, untagged));
    REQUIRE(untagged.size() == 1);
    REQUIRE(untagged[0].versionTag.empty());
}

TEST_CASE("master registration builder carries platform", "[network][master]")
{
    MasterServerServiceRegistration registration;
    REQUIRE(BuildMasterServerServiceRegistrationFromServerUrl("192.168.1.20:2302", "Dedicated", 14, false, nullptr, 300,
                                                              300, "rc5", false, 1, 16, "", false, "linux",
                                                              registration));

    REQUIRE(registration.platform == "linux");
    REQUIRE(registration.app == "CWR-CE");
    REQUIRE(BuildMasterServerServiceRegistrationJson(registration).find("\"platform\":\"linux\"") != std::string::npos);
}

TEST_CASE("master service list URL filters by app only", "[network][master][version]")
{
    const std::string url = BuildMasterServerServiceListUrl("https://master.example", MasterServerBrowserFilter{});

    REQUIRE(url.find("app=CWR-CE") != std::string::npos);
    REQUIRE(url.find("actver=") == std::string::npos);
    REQUIRE(url.find("vertag=") == std::string::npos);
}

TEST_CASE("master service requests carry structured user-agent", "[network][master][version]")
{
    REQUIRE(BuildMasterServerServiceUserAgent(nullptr) == "CWR-CE/301");

    MasterServerServiceHttpRequest request;
    REQUIRE(BuildMasterServerServiceGetRequest("https://master.example/v1/servers", request));

    REQUIRE(request.userAgent.find("CWR-CE/301") == 0);
    REQUIRE(request.userAgent.find("tag=") != std::string::npos);
    REQUIRE(request.userAgent.find("role=client") != std::string::npos);
}

TEST_CASE("master server attribution label includes the configured endpoint", "[network][master][ui]")
{
    REQUIRE(std::string(FormatNetworkMasterServerAttribution("https://master.example")) ==
            "Operated by master.example");
    REQUIRE(std::string(FormatNetworkMasterServerAttribution("http://master.example")) == "Operated by master.example");
    REQUIRE(std::string(FormatNetworkMasterServerAttribution("master.example")) == "Operated by master.example");
    REQUIRE(std::string(FormatNetworkMasterServerAttribution("")) == "Operated by disabled");
}
