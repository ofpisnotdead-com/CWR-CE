#pragma once

namespace Poseidon
{

// Recursively delete the contents of a directory, and the directory itself when
// deleteDir is true. The path may use either separator: it is normalized to the
// platform separator before the separator-sensitive POSIX unlink/rmdir, so a
// mission/campaign path built with backslashes still deletes on Linux.
void DeleteDirectoryStructure(const char* name, bool deleteDir = true);

} // namespace Poseidon

using Poseidon::DeleteDirectoryStructure;
