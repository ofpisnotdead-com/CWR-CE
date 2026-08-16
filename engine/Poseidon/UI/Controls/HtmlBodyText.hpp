#pragma once

#include <Poseidon/Foundation/Strings/RString.hpp>

namespace Poseidon
{
class QIStream;

RString ReadHtmlBodyText(QIStream& in, int langID);
} // namespace Poseidon
