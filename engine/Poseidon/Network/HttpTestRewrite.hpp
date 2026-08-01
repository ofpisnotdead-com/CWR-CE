#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace Poseidon
{

struct HttpTestRewrite
{
    std::string source;
    std::string target;
};

bool IsHttpTestRewriteValid(std::string_view source, std::string_view target);

std::string ResolveHttpTestUrl(std::string_view url, const std::vector<HttpTestRewrite>& rewrites);

bool RegisterHttpTestRewrite(std::string source, std::string target);

std::string ResolveHttpTestUrl(std::string_view url);

} // namespace Poseidon
