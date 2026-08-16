#include <Evaluator/express.hpp>
using namespace Poseidon;
#include <Poseidon/Foundation/Common/Win.h>
#include <Poseidon/Foundation/Modules/Modules.hpp>
#include <Poseidon/Foundation/platform.hpp>
#include <Poseidon/Network/XML/Xml.hpp>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <stdio.h>
#include <string>
#include <vector>

namespace
{

std::vector<std::string> GRemoteExecLog;

// Convert any GameValue to its display string (for failure messages).
// Scalars use %g; strings use the raw value (GetString, no quotes).
static std::string TriValStr(const GameValue& v)
{
    if (v.GetType() == GameScalar)
    {
        char buf[32];
        snprintf(buf, sizeof(buf), "%g", (float)(GameScalarType)v);
        return buf;
    }
    return std::string(((RString)(GameStringType)v).Data());
}

// Truthy: non-zero scalar, non-empty string that is not "0".
static bool TriIsTruthy(const GameValue& v)
{
    if (v.GetType() == GameScalar)
        return (float)(GameScalarType)v != 0.0f;
    std::string s = TriValStr(v);
    return !s.empty() && s != "0";
}

// Count commas in a C string.
static int TriCountCommas(const char* s)
{
    int n = 0;
    for (; *s; ++s)
        if (*s == ',')
            ++n;
    return n;
}

// Parse exactly `n` comma-separated integers from `s`. Returns true on success.
static bool TriParseCommaInts(const char* s, int* out, int n)
{
    const char* p = s;
    for (int i = 0; i < n; ++i)
    {
        char* end;
        out[i] = (int)strtol(p, &end, 10);
        if (end == p)
            return false;
        p = end;
        if (i < n - 1)
        {
            if (*p != ',')
                return false;
            ++p;
        }
    }
    return true;
}

// Parse exactly `n` comma-separated floats from `s`. Returns true on success.
static bool TriParseCommaFloats(const char* s, float* out, int n)
{
    const char* p = s;
    for (int i = 0; i < n; ++i)
    {
        char* end;
        out[i] = strtof(p, &end);
        if (end == p)
            return false;
        p = end;
        if (i < n - 1)
        {
            if (*p != ',')
                return false;
            ++p;
        }
    }
    return true;
}

} // namespace

/// triAssert [value] — OK if value is truthy (non-zero scalar or non-empty/"0" string).
GameValue TriAssert(const GameState*, GameValuePar arg)
{
    if (arg.GetType() != GameArray)
        return GameValue("FAIL:expected [value]");
    const GameArrayType& a = arg;
    if (a.Size() < 1)
        return GameValue("FAIL:expected [value]");
    if (TriIsTruthy(a[0]))
        return GameValue("OK");
    char buf[256];
    snprintf(buf, sizeof(buf), "FAIL:expected truthy, got %s", TriValStr(a[0]).c_str());
    return GameValue(buf);
}

/// triRefute [value] — OK if value is falsy (zero scalar or empty/"0" string).
GameValue TriRefute(const GameState*, GameValuePar arg)
{
    if (arg.GetType() != GameArray)
        return GameValue("FAIL:expected [value]");
    const GameArrayType& a = arg;
    if (a.Size() < 1)
        return GameValue("FAIL:expected [value]");
    if (!TriIsTruthy(a[0]))
        return GameValue("OK");
    char buf[256];
    snprintf(buf, sizeof(buf), "FAIL:expected falsy, got %s", TriValStr(a[0]).c_str());
    return GameValue(buf);
}

/// triAssertEq [got, expected] — OK if got == expected.
/// Scalar: exact float equality. Otherwise: string representation equality.
GameValue TriAssertEq(const GameState*, GameValuePar arg)
{
    if (arg.GetType() != GameArray)
        return GameValue("FAIL:expected [got, expected]");
    const GameArrayType& a = arg;
    if (a.Size() < 2)
        return GameValue("FAIL:expected [got, expected]");
    bool eq;
    if (a[0].GetType() == GameScalar && a[1].GetType() == GameScalar)
        eq = ((float)(GameScalarType)a[0] == (float)(GameScalarType)a[1]);
    else
        eq = (TriValStr(a[0]) == TriValStr(a[1]));
    if (eq)
        return GameValue("OK");
    char buf[512];
    snprintf(buf, sizeof(buf), "FAIL:expected == %s, got %s", TriValStr(a[1]).c_str(), TriValStr(a[0]).c_str());
    return GameValue(buf);
}

/// triAssertNe [got, expected] — OK if got != expected.
GameValue TriAssertNe(const GameState*, GameValuePar arg)
{
    if (arg.GetType() != GameArray)
        return GameValue("FAIL:expected [got, expected]");
    const GameArrayType& a = arg;
    if (a.Size() < 2)
        return GameValue("FAIL:expected [got, expected]");
    bool eq;
    if (a[0].GetType() == GameScalar && a[1].GetType() == GameScalar)
        eq = ((float)(GameScalarType)a[0] == (float)(GameScalarType)a[1]);
    else
        eq = (TriValStr(a[0]) == TriValStr(a[1]));
    if (!eq)
        return GameValue("OK");
    char buf[512];
    snprintf(buf, sizeof(buf), "FAIL:expected != %s, got %s", TriValStr(a[1]).c_str(), TriValStr(a[0]).c_str());
    return GameValue(buf);
}

/// triAssertGt [got, threshold] — OK if numeric got > threshold.
GameValue TriAssertGt(const GameState*, GameValuePar arg)
{
    if (arg.GetType() != GameArray)
        return GameValue("FAIL:expected [got, threshold]");
    const GameArrayType& a = arg;
    if (a.Size() < 2)
        return GameValue("FAIL:expected [got, threshold]");
    float got = (float)(GameScalarType)a[0];
    float thr = (float)(GameScalarType)a[1];
    if (got > thr)
        return GameValue("OK");
    char buf[128];
    snprintf(buf, sizeof(buf), "FAIL:expected > %.6g, got %.6g", thr, got);
    return GameValue(buf);
}

/// triAssertGe [got, threshold] — OK if numeric got >= threshold.
GameValue TriAssertGe(const GameState*, GameValuePar arg)
{
    if (arg.GetType() != GameArray)
        return GameValue("FAIL:expected [got, threshold]");
    const GameArrayType& a = arg;
    if (a.Size() < 2)
        return GameValue("FAIL:expected [got, threshold]");
    float got = (float)(GameScalarType)a[0];
    float thr = (float)(GameScalarType)a[1];
    if (got >= thr)
        return GameValue("OK");
    char buf[128];
    snprintf(buf, sizeof(buf), "FAIL:expected >= %.6g, got %.6g", thr, got);
    return GameValue(buf);
}

/// triAssertLt [got, threshold] — OK if numeric got < threshold.
GameValue TriAssertLt(const GameState*, GameValuePar arg)
{
    if (arg.GetType() != GameArray)
        return GameValue("FAIL:expected [got, threshold]");
    const GameArrayType& a = arg;
    if (a.Size() < 2)
        return GameValue("FAIL:expected [got, threshold]");
    float got = (float)(GameScalarType)a[0];
    float thr = (float)(GameScalarType)a[1];
    if (got < thr)
        return GameValue("OK");
    char buf[128];
    snprintf(buf, sizeof(buf), "FAIL:expected < %.6g, got %.6g", thr, got);
    return GameValue(buf);
}

/// triAssertLe [got, threshold] — OK if numeric got <= threshold.
GameValue TriAssertLe(const GameState*, GameValuePar arg)
{
    if (arg.GetType() != GameArray)
        return GameValue("FAIL:expected [got, threshold]");
    const GameArrayType& a = arg;
    if (a.Size() < 2)
        return GameValue("FAIL:expected [got, threshold]");
    float got = (float)(GameScalarType)a[0];
    float thr = (float)(GameScalarType)a[1];
    if (got <= thr)
        return GameValue("OK");
    char buf[128];
    snprintf(buf, sizeof(buf), "FAIL:expected <= %.6g, got %.6g", thr, got);
    return GameValue(buf);
}

/// triAssertNear [got, expected, tol] — OK if |got - expected| <= tol.
/// Dispatch by type and comma count:
///   scalar got (or no comma): float abs diff.
///   2 commas ("R,G,B"): per-channel int abs diff (RGB mode).
///   1 comma ("x,y") or 3 commas ("x,y,w,h"): per-component float abs diff.
GameValue TriAssertNear(const GameState*, GameValuePar arg)
{
    if (arg.GetType() != GameArray)
        return GameValue("FAIL:expected [got, expected, tol]");
    const GameArrayType& a = arg;
    if (a.Size() < 3)
        return GameValue("FAIL:expected [got, expected, tol]");

    float tol = (float)(GameScalarType)a[2];

    if (a[0].GetType() == GameScalar)
    {
        float got = (float)(GameScalarType)a[0];
        float exp = (float)(GameScalarType)a[1];
        float diff = std::abs(got - exp);
        if (diff <= tol)
            return GameValue("OK");
        char buf[128];
        snprintf(buf, sizeof(buf), "FAIL:expected near %.6g +/-%.6g, got %.6g", exp, tol, got);
        return GameValue(buf);
    }

    std::string gotStr = TriValStr(a[0]);
    std::string expStr = TriValStr(a[1]);
    int commas = TriCountCommas(gotStr.c_str());

    if (commas == 2)
    {
        // RGB mode: 3 integer channels.
        int got[3], exp[3];
        if (!TriParseCommaInts(gotStr.c_str(), got, 3) || !TriParseCommaInts(expStr.c_str(), exp, 3))
            return GameValue("FAIL:cannot parse as R,G,B");
        int dr = std::abs(got[0] - exp[0]);
        int dg = std::abs(got[1] - exp[1]);
        int db = std::abs(got[2] - exp[2]);
        int itol = (int)tol;
        if (dr <= itol && dg <= itol && db <= itol)
            return GameValue("OK");
        char buf[256];
        snprintf(buf, sizeof(buf), "FAIL:expected near %s +/-%d, got %s (dr=%d,dg=%d,db=%d)", expStr.c_str(), itol,
                 gotStr.c_str(), dr, dg, db);
        return GameValue(buf);
    }

    // N-component float mode (1 comma = 2D, 3 commas = 4D, or 0 commas = scalar string).
    int n = commas + 1;
    if (n < 1 || n > 4)
    {
        char buf[128];
        snprintf(buf, sizeof(buf), "FAIL:unsupported comma count %d in near comparison", commas);
        return GameValue(buf);
    }
    if (n == 1)
    {
        // scalar string — parse as float.
        float got = strtof(gotStr.c_str(), nullptr);
        float exp = strtof(expStr.c_str(), nullptr);
        float diff = std::abs(got - exp);
        if (diff <= tol)
            return GameValue("OK");
        char buf[256];
        snprintf(buf, sizeof(buf), "FAIL:expected near %s +/-%.6g, got %s", expStr.c_str(), tol, gotStr.c_str());
        return GameValue(buf);
    }
    float gv[4] = {}, ev[4] = {};
    if (!TriParseCommaFloats(gotStr.c_str(), gv, n) || !TriParseCommaFloats(expStr.c_str(), ev, n))
        return GameValue("FAIL:cannot parse float components");
    float maxDiff = 0.0f;
    for (int i = 0; i < n; ++i)
        maxDiff = std::max(maxDiff, std::abs(gv[i] - ev[i]));
    if (maxDiff <= tol)
        return GameValue("OK");
    char buf[256];
    snprintf(buf, sizeof(buf), "FAIL:expected near %s +/-%.6g, got %s", expStr.c_str(), tol, gotStr.c_str());
    return GameValue(buf);
}

/// triAssertChanged [got_str, expected_str, min_delta] — OK if any R/G/B channel
/// of got_str differs from expected_str by >= min_delta. Inverse of triAssertNear
/// on "R,G,B" strings.
GameValue TriAssertChanged(const GameState*, GameValuePar arg)
{
    if (arg.GetType() != GameArray)
        return GameValue("FAIL:expected [got, expected, minDelta]");
    const GameArrayType& a = arg;
    if (a.Size() < 3)
        return GameValue("FAIL:expected [got, expected, minDelta]");
    std::string gotStr = TriValStr(a[0]);
    std::string expStr = TriValStr(a[1]);
    int minDelta = (int)(GameScalarType)a[2];
    int got[3], exp[3];
    if (!TriParseCommaInts(gotStr.c_str(), got, 3) || !TriParseCommaInts(expStr.c_str(), exp, 3))
        return GameValue("FAIL:cannot parse as R,G,B");
    int dr = std::abs(got[0] - exp[0]);
    int dg = std::abs(got[1] - exp[1]);
    int db = std::abs(got[2] - exp[2]);
    if (dr >= minDelta || dg >= minDelta || db >= minDelta)
        return GameValue("OK");
    char buf[256];
    snprintf(buf, sizeof(buf), "FAIL:expected changed by >= %d, got %s vs %s (dr=%d,dg=%d,db=%d)", minDelta,
             gotStr.c_str(), expStr.c_str(), dr, dg, db);
    return GameValue(buf);
}

/// triAssertIncludes [haystack, needle] — OK if needle is a substring of haystack.
GameValue TriAssertIncludes(const GameState*, GameValuePar arg)
{
    if (arg.GetType() != GameArray)
        return GameValue("FAIL:expected [haystack, needle]");
    const GameArrayType& a = arg;
    if (a.Size() < 2)
        return GameValue("FAIL:expected [haystack, needle]");
    std::string haystack = TriValStr(a[0]);
    std::string needle = TriValStr(a[1]);
    if (haystack.find(needle) != std::string::npos)
        return GameValue("OK");
    char buf[512];
    snprintf(buf, sizeof(buf), "FAIL:expected includes %s, got %s", needle.c_str(), haystack.c_str());
    return GameValue(buf);
}

/// triAssertExcludes [haystack, needle] — OK if needle is NOT a substring of haystack.
GameValue TriAssertExcludes(const GameState*, GameValuePar arg)
{
    if (arg.GetType() != GameArray)
        return GameValue("FAIL:expected [haystack, needle]");
    const GameArrayType& a = arg;
    if (a.Size() < 2)
        return GameValue("FAIL:expected [haystack, needle]");
    std::string haystack = TriValStr(a[0]);
    std::string needle = TriValStr(a[1]);
    if (haystack.find(needle) == std::string::npos)
        return GameValue("OK");
    char buf[512];
    snprintf(buf, sizeof(buf), "FAIL:expected excludes %s, got %s", needle.c_str(), haystack.c_str());
    return GameValue(buf);
}

/// triAssertMatches [string, pattern] — OK if pattern is a substring of string.
/// Alias for triAssertIncludes; kept for readability.
GameValue TriAssertMatches(const GameState* state, GameValuePar arg)
{
    return TriAssertIncludes(state, arg);
}

/// triRecordRemoteExec [value] — append a string to the local remoteExec test log.
GameValue TriRecordRemoteExec(const GameState*, GameValuePar arg)
{
    if (arg.GetType() != GameArray)
        return GameValue("FAIL:expected [value]");
    const GameArrayType& a = arg;
    if (a.Size() < 1)
        return GameValue("FAIL:expected [value]");
    GRemoteExecLog.push_back(TriValStr(a[0]));
    return GameValue("OK");
}

/// triRemoteExecLog — return the local remoteExec test log joined by '|'.
GameValue TriRemoteExecLog(const GameState*)
{
    std::string result;
    for (size_t i = 0; i < GRemoteExecLog.size(); ++i)
    {
        if (i > 0)
            result += "|";
        result += GRemoteExecLog[i];
    }
    return GameValue(result.c_str());
}

/// triClearRemoteExecLog — clear the local remoteExec test log.
GameValue TriClearRemoteExecLog(const GameState*)
{
    GRemoteExecLog.clear();
    return GameValue("OK");
}

GameValue TriHttpGet(const GameState*, GameValuePar arg)
{
    const std::string url = TriValStr(arg);
    size_t size = 0;
    char* payload = DownloadFile(url.c_str(), size, nullptr, 64 * 1024);
    if (!payload)
        return GameValue("FAIL:download failed");
    const std::string result(payload, size);
#ifdef _WIN32
    GlobalFree(payload);
#else
    free(payload);
#endif
    return GameValue(result.c_str());
}

// Called from GameStateExtTestAudio.cpp's INIT_MODULE to force this TU into
// the link when building PoseidonGame (where no other game code references
// the TriAssert* family directly).
void EnsureGameStateExtTestGenericLinked() {}

INIT_MODULE(GameStateExtTestGeneric, 3)
{
    GGameState.NewFunction(GameFunction(GameString, "triAssert", TriAssert, GameArray));
    GGameState.NewFunction(GameFunction(GameString, "triRefute", TriRefute, GameArray));
    GGameState.NewFunction(GameFunction(GameString, "triAssertEq", TriAssertEq, GameArray));
    GGameState.NewFunction(GameFunction(GameString, "triAssertNe", TriAssertNe, GameArray));
    GGameState.NewFunction(GameFunction(GameString, "triAssertGt", TriAssertGt, GameArray));
    GGameState.NewFunction(GameFunction(GameString, "triAssertGe", TriAssertGe, GameArray));
    GGameState.NewFunction(GameFunction(GameString, "triAssertLt", TriAssertLt, GameArray));
    GGameState.NewFunction(GameFunction(GameString, "triAssertLe", TriAssertLe, GameArray));
    GGameState.NewFunction(GameFunction(GameString, "triAssertNear", TriAssertNear, GameArray));
    GGameState.NewFunction(GameFunction(GameString, "triAssertChanged", TriAssertChanged, GameArray));
    GGameState.NewFunction(GameFunction(GameString, "triAssertIncludes", TriAssertIncludes, GameArray));
    GGameState.NewFunction(GameFunction(GameString, "triAssertExcludes", TriAssertExcludes, GameArray));
    GGameState.NewFunction(GameFunction(GameString, "triAssertMatches", TriAssertMatches, GameArray));
    GGameState.NewFunction(GameFunction(GameString, "triRecordRemoteExec", TriRecordRemoteExec, GameArray));
    GGameState.NewNularOp(GameNular(GameString, "triRemoteExecLog", TriRemoteExecLog));
    GGameState.NewNularOp(GameNular(GameString, "triClearRemoteExecLog", TriClearRemoteExecLog));
    GGameState.NewFunction(GameFunction(GameString, "triHttpGet", TriHttpGet, GameString));
};
