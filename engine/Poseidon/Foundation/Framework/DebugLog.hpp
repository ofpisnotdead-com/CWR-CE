#pragma once

#include <Poseidon/Foundation/Framework/Log.hpp>

#ifndef CCALL
#ifdef _MSC_VER
#define CCALL __cdecl
#else
#define CCALL
#endif
#endif

// critical error - terminate application

namespace Poseidon::Foundation
{
void CCALL ErrorMessage(const char* format, ...);
// noncritical error - may terminate application
void CCALL WarningMessage(const char* format, ...);

// assertion failed - soft mode skips __debugbreak (for simulate/headless)
extern bool gSoftAssert;

#ifdef _MSC_VER
#ifdef NDEBUG
#define FailHook(text)
#else
#define FailHook(text)      \
    do                      \
    {                       \
        if (!gSoftAssert)   \
            __debugbreak(); \
    } while (0)
#endif
#else
#define FailHook(text)
#endif

// Debug macros — routed through LOG_* (Log.hpp)

#define DoVerify(expr)                                                                    \
    {                                                                                     \
        if (!(expr))                                                                      \
        {                                                                                 \
            LOG_ERROR(Core, "{}({}) : Assertion failed '{}'", __FILE__, __LINE__, #expr); \
            FailHook(#expr);                                                              \
        }                                                                                 \
    }
#define DoAssert(expr) DoVerify(expr)

#ifdef _DEBUG
#define DebugLog(fmt, ...) LOG_DEBUG(Core, fmt, ##__VA_ARGS__)
#define AssertDebug(expr) Verify(expr)
#define DebF(fmt, ...) LOG_DEBUG(Core, fmt, ##__VA_ARGS__)
#else
#define DebugLog(...) ((void)0)
#define AssertDebug(expr)
#define DebF(fmt, ...) LOG_ERROR(Core, fmt, ##__VA_ARGS__)
#endif

#define RptF(...) ((void)0)

#ifdef NDEBUG
#define PoseidonAssert(expr)
#undef Verify
#define Verify(expr) (expr)
#define Fail(text) DebF("{}({}) : {}", __FILE__, __LINE__, text)
#define Log(...) ((void)0)
#else
#define PoseidonAssert(expr) DoAssert(expr)
#undef Verify
#define Verify(expr) DoAssert(expr)
#define Fail(text)                                     \
    {                                                  \
        DebF("{}({}) : {}", __FILE__, __LINE__, text); \
        FailHook(text);                                \
    }
#define Log(fmt, ...) LOG_DEBUG(Core, fmt, ##__VA_ARGS__)
#endif

// Diagnostic check: log an error in category <cat> on a failed condition and continue —
// used across engine subsystems in place of the assert macros so invariant / protocol /
// state-sync violations surface in release and test logs instead of being compiled out or
// aborting. Brace-block form mirrors DoAssert, so a trailing ';' at the call site stays
// optional. Per-subsystem aliases keep the log category right: NET_ERROR (Network folder),
// AI_ERROR (AI folder).
#define POSEIDON_LOG_CHECK(cat, expr)                                                      \
    {                                                                                      \
        if (!(expr))                                                                       \
            LOG_ERROR(cat, "{}({}): check failed: {}", __FILE__, __LINE__, #expr);         \
    }
#define NET_ERROR(expr) POSEIDON_LOG_CHECK(Network, expr)
#define AI_ERROR(expr) POSEIDON_LOG_CHECK(AI, expr)

// AI_HEAVY_CHECK: like AI_ERROR, but for expensive O(n) validators such as
// AssertValid() / CheckVehicleStructure() that would otherwise run every frame.
// Unlike AI_ERROR (evaluated in release by design), this compiles out under
// NDEBUG so the structural sweep costs nothing in shipping builds. Reserve it
// for heavy validators; keep AI_ERROR for cheap one-liners that should stay in
// release logs.
#ifdef NDEBUG
#define AI_HEAVY_CHECK(expr) ((void)0)
#else
#define AI_HEAVY_CHECK(expr) POSEIDON_LOG_CHECK(AI, expr)
#endif

#include <stdio.h>

#pragma warning(disable : 4996)
inline const char* FileLineF(const char* file, int line, const char* postfix)
{
    static char buf[512];
    snprintf(buf, sizeof(buf), "%s(%d): %s", file, line, postfix);
    buf[sizeof(buf) - 1] = 0;
    return buf;
}

#define FileLine(postfix) FileLineF(__FILE__, __LINE__, postfix)

} // namespace Poseidon::Foundation

using Poseidon::Foundation::gSoftAssert;

