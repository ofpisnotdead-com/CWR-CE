#include <Poseidon/AI/AI.hpp>
#include <Poseidon/AI/ArcadeTemplate.hpp>
#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

using namespace Poseidon;

namespace
{
std::string ReadTextFile(const std::filesystem::path& path)
{
    std::ifstream input(path);
    REQUIRE(input);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

std::filesystem::path SourcePath(const std::filesystem::path& path)
{
    return std::filesystem::path(TESTS_ROOT_DIR).parent_path() / path;
}
} // namespace

TEST_CASE("arcadeTemplate compiles", "[ai]")
{
    REQUIRE(sizeof(ArcadeTemplate) > 0);
}

TEST_CASE("ExperienceForDestroyedCost covers the top-bucket band (no zero gap)", "[ai][experience]")
{
    // Costs map to the first bucket whose maxCost they fit under; the last entry
    // is the catch-all for anything above the second-to-last bucket. The pre-fix
    // AISubgroup lookup tested `cost > maxCost[n-1]` for the catch-all and looped
    // only to n-2, so a cost in (maxCost[n-2], maxCost[n-1]] fell through to 0 --
    // the AI_ERROR(base > 0) the headless combat stress run tripped.
    AutoArray<ExperienceDestroyInfo> saved = ExperienceDestroyTable;
    ExperienceDestroyTable.Resize(3);
    ExperienceDestroyTable[0].maxCost = 100.0f;
    ExperienceDestroyTable[0].exp = 1.0f;
    ExperienceDestroyTable[1].maxCost = 1000.0f;
    ExperienceDestroyTable[1].exp = 2.0f;
    ExperienceDestroyTable[2].maxCost = 5000.0f;
    ExperienceDestroyTable[2].exp = 3.0f; // catch-all

    REQUIRE(ExperienceForDestroyedCost(0.0f) == 1.0f);   // tiny -> first bucket
    REQUIRE(ExperienceForDestroyedCost(50.0f) == 1.0f);  // first bucket
    REQUIRE(ExperienceForDestroyedCost(500.0f) == 2.0f); // middle bucket
    // The regression: a cost between the last two buckets. Pre-fix this returned 0.
    REQUIRE(ExperienceForDestroyedCost(3000.0f) == 3.0f);
    REQUIRE(ExperienceForDestroyedCost(99999.0f) == 3.0f); // far above -> catch-all

    ExperienceDestroyTable = saved;
}

TEST_CASE("supply completion releases state before its synchronous transition", "[ai][supply]")
{
    const std::string source = ReadTextFile(SourcePath("engine/Poseidon/AI/AISubgroupFSMSupply.inc"));
    const size_t finish = source.find("auto finish =");
    REQUIRE(finish != std::string::npos);
    const size_t clearPlan = source.find("subgrp->ClearPlan()", finish);
    const size_t unallocate = source.find("transport->SetAllocSupply(nullptr)", finish);
    const size_t report = source.find("ReportStatus(subgrp->Leader(), cmd->_message)", finish);
    const size_t transition = source.find("context->_fsm->SetState(state, context)", finish);

    REQUIRE(clearPlan != std::string::npos);
    REQUIRE(unallocate != std::string::npos);
    REQUIRE(report != std::string::npos);
    REQUIRE(transition != std::string::npos);
    CHECK(clearPlan < transition);
    CHECK(unallocate < transition);
    CHECK(report < transition);
}

TEST_CASE("remote command deletion resolves the subgroup task by network id", "[ai][network]")
{
    const std::string subgroup = ReadTextFile(SourcePath("engine/Poseidon/AI/AISubgroup.cpp"));
    const size_t deletion = subgroup.find("void AISubgroup::DeleteCommand(NetworkId id)");
    REQUIRE(deletion != std::string::npos);
    CHECK(subgroup.find("task && task->GetNetworkId() == id", deletion) != std::string::npos);

    const std::string client = ReadTextFile(SourcePath("engine/Poseidon/Network/NetworkClientOnMessage.cpp"));
    const size_t message = client.find("case NMTDeleteCommand:");
    REQUIRE(message != std::string::npos);
    const size_t nextMessage = client.find("case NMTCreateObject:", message);
    REQUIRE(nextMessage != std::string::npos);
    const std::string deleteCommandCase = client.substr(message, nextMessage - message);
    CHECK(deleteCommandCase.find("dc.subgrp->DeleteCommand(dc.object)") != std::string::npos);
    CHECK(deleteCommandCase.find("dynamic_cast<Command*>") == std::string::npos);
}

TEST_CASE("attack estimation retains its commander", "[ai][attack]")
{
    const std::string source = ReadTextFile(SourcePath("engine/Poseidon/World/Detection/TargetFire.cpp"));
    const size_t estimate = source.find("int EntityAI::EstimateAttack(");
    REQUIRE(estimate != std::string::npos);
    const size_t overload =
        source.find("int EntityAI::EstimateAttack(const Vector3& hPos, float height) const", estimate);
    REQUIRE(overload != std::string::npos);
    const std::string function = source.substr(estimate, overload - estimate);

    CHECK(function.find("Ref<AIUnit> unit = CommanderUnit();") != std::string::npos);
}
