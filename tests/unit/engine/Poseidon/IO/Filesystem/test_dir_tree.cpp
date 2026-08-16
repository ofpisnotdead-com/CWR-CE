// Unit tests for DeleteDirectoryStructure (recursive directory delete).
// The regression is Linux-only: callers pass mission/campaign paths and the
// helper joined children with a hardcoded backslash, which _findfirst tolerates
// (it normalizes) but the raw POSIX unlink/rmdir do not — so on Linux every
// delete was a literal-filename no-op and the tree survived.

#include <catch2/catch_test_macros.hpp>

#include <Poseidon/IO/Filesystem/DirTree.hpp>

#include <filesystem>
#include <fstream>

#ifndef _WIN32 // exercises the POSIX delete path where the backslash bug lived

namespace fs = std::filesystem;

namespace
{
void writeFile(const fs::path& p)
{
    fs::create_directories(p.parent_path());
    std::ofstream(p).put(' ');
}
} // namespace

TEST_CASE("DeleteDirectoryStructure removes a nested tree", "[filesystem][dirtree]")
{
    const fs::path root = fs::temp_directory_path() / "test_dirtree_nested";
    fs::remove_all(root);
    writeFile(root / "a.txt");
    writeFile(root / "sub" / "b.txt");
    writeFile(root / "sub" / "deeper" / "c.txt");
    REQUIRE(fs::exists(root));

    Poseidon::DeleteDirectoryStructure(root.string().c_str(), true);

    // Broken state left the files (and thus the directories) in place.
    CHECK_FALSE(fs::exists(root));
}

TEST_CASE("DeleteDirectoryStructure with deleteDir=false empties but keeps root", "[filesystem][dirtree]")
{
    const fs::path root = fs::temp_directory_path() / "test_dirtree_keep";
    fs::remove_all(root);
    writeFile(root / "f.txt");
    writeFile(root / "sub" / "g.txt");

    Poseidon::DeleteDirectoryStructure(root.string().c_str(), false);

    CHECK(fs::exists(root));
    CHECK(fs::is_empty(root));

    fs::remove_all(root);
}

#endif // !_WIN32
