#pragma once

#include <Poseidon/Foundation/platform.hpp>
#include <cstdint>


namespace Poseidon
{
class DirScanner
{
  public:
    DirScanner();
    ~DirScanner();

    bool First(const char* dir, const char* ext = ".pbo");
    bool Next();
    void Close();
    const char* GetName() const;
    bool IsDirectory() const;

  private:
#ifdef _WIN32
    void* _info; // _finddata_t*
    intptr_t _handle;
    char _wild[512]; // path with wildcard pattern
#else
    void* _dir;    // DIR*
    void* _entry;  // struct dirent*
    char _ext[16]; // extension filter (lowercase)
    char _path[512];
#endif
};

} // namespace Poseidon

using Poseidon::DirScanner;
