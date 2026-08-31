#pragma once

#include <Poseidon/Asset/Formats/Common/FormatDetector.hpp>
#include <Poseidon/World/Model/Model.hpp>
#include <string>

namespace Poseidon::Asset::Formats
{

// The P3D readers report malformed input by throwing. Callers that sit on an engine
// entry point cannot let that escape, so this reports the failure instead.
bool TryLoadP3D(const std::string& path, Poseidon::Model::Model& model, FormatInfo& format, std::string& error);

} // namespace Poseidon::Asset::Formats
