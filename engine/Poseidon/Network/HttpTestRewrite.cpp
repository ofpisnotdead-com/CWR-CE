#include <Poseidon/Network/HttpTestRewrite.hpp>

#include <mutex>
#include <utility>

namespace Poseidon
{
namespace
{

std::mutex GHttpTestRewritesMutex;
std::vector<HttpTestRewrite> GHttpTestRewrites;

bool StartsWith(std::string_view value, std::string_view prefix)
{
    return value.size() >= prefix.size() && value.substr(0, prefix.size()) == prefix;
}

} // namespace

bool IsHttpTestRewriteValid(std::string_view source, std::string_view target)
{
    const bool sourceIsHttp = StartsWith(source, "http://") || StartsWith(source, "https://");
    const bool targetIsLoopback = StartsWith(target, "http://127.0.0.1:") || StartsWith(target, "http://localhost:") ||
                                  StartsWith(target, "http://[::1]:");
    return sourceIsHttp && targetIsLoopback;
}

std::string ResolveHttpTestUrl(std::string_view url, const std::vector<HttpTestRewrite>& rewrites)
{
    for (const HttpTestRewrite& rewrite : rewrites)
    {
        if (url == rewrite.source)
            return rewrite.target;
    }
    return std::string(url);
}

bool RegisterHttpTestRewrite(std::string source, std::string target)
{
    if (!IsHttpTestRewriteValid(source, target))
        return false;

    std::lock_guard<std::mutex> lock(GHttpTestRewritesMutex);
    for (HttpTestRewrite& rewrite : GHttpTestRewrites)
    {
        if (rewrite.source == source)
        {
            rewrite.target = std::move(target);
            return true;
        }
    }
    GHttpTestRewrites.push_back({std::move(source), std::move(target)});
    return true;
}

std::string ResolveHttpTestUrl(std::string_view url)
{
    std::lock_guard<std::mutex> lock(GHttpTestRewritesMutex);
    return ResolveHttpTestUrl(url, GHttpTestRewrites);
}

} // namespace Poseidon
