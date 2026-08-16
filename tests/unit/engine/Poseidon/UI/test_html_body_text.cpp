// HTML entities in a mission description decode to UTF-8, and raw UTF-8 text in the
// same run is left intact.

#include <catch2/catch_test_macros.hpp>

#include <Poseidon/UI/Controls/HtmlBodyText.hpp>
#include <Poseidon/IO/Streams/QStream.hpp>

#include <cstring>

using namespace Poseidon;

namespace
{
RString ProcessHtmlText(const char* html)
{
    QIStream in;
    in.init(html, static_cast<int>(std::strlen(html)));
    return ReadHtmlBodyText(in, 0);
}
} // namespace

TEST_CASE("ReadHtmlBodyText: named accent entities across languages become UTF-8", "[ui][html][utf8]")
{
    CHECK(ProcessHtmlText("caf&eacute;<") == RString("caf\xC3\xA9"));
    CHECK(ProcessHtmlText("gar&ccedil;on<") == RString("gar\xC3\xA7on"));
    CHECK(ProcessHtmlText("gr&uuml;n<") == RString("gr\xC3\xBCn"));
    CHECK(ProcessHtmlText("gro&szlig;<") == RString("gro\xC3\x9F"));
    CHECK(ProcessHtmlText("a&ntilde;o<") == RString("a\xC3\xB1o"));
    CHECK(ProcessHtmlText("citt&agrave;<") == RString("citt\xC3\xA0"));
}

TEST_CASE("ReadHtmlBodyText: numeric entity becomes UTF-8", "[ui][html][utf8]")
{
    CHECK(ProcessHtmlText("caf&#233;<") == RString("caf\xC3\xA9"));
}

TEST_CASE("ReadHtmlBodyText: non-breaking-space entity becomes UTF-8", "[ui][html][utf8]")
{
    // A `<p>&nbsp;</p>` spacer decodes to U+00A0.
    CHECK(ProcessHtmlText("&nbsp;<") == RString("\xC2\xA0"));
}

TEST_CASE("ReadHtmlBodyText: ASCII entity stays a single ASCII byte", "[ui][html][utf8]")
{
    CHECK(ProcessHtmlText("R&amp;R<") == RString("R&R"));
}

TEST_CASE("ReadHtmlBodyText: raw UTF-8 next to an entity both stay UTF-8", "[ui][html][utf8]")
{
    // Raw UTF-8 e-acute next to the entity &agrave;; both stay UTF-8.
    CHECK(ProcessHtmlText("\xC3\xA9&agrave;<") == RString("\xC3\xA9\xC3\xA0"));
}
