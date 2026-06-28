// Mouse advanced menu structure: the Mouse page should expose the basic section,
// the Advanced Mouse section, and the first advanced control without relying on
// options-page scrolling.

#include "../../../helpers/options_preamble.sqf"
#include "../../../helpers/controls_preamble.sqf"

triClickText "Mouse"
triAssertEq [(triDisplay), 9099]

triAssertIncludes [(triVisibleTexts), "Mouse"]
triAssertIncludes [(triVisibleTexts), "Mouse sensitivity X"]
triAssertIncludes [(triVisibleTexts), "Mouse DPI"]
triAssertIncludes [(triVisibleTexts), "Advanced Mouse"]
triAssertIncludes [(triVisibleTexts), "Input dead zone"]

if ((triControlText 501) != "Mouse") exitWith {
    format ["FAIL:mouse_section_label actual='%1'", triControlText 501]
};
if ((triControlText 561) != "Advanced Mouse") exitWith {
    format ["FAIL:advanced_section_label actual='%1'", triControlText 561]
};
if ((triControlText 570) != "Input dead zone") exitWith {
    format ["FAIL:input_dead_zone_label actual='%1'", triControlText 570]
};
if ((triControlText 572) != "0.00") exitWith {
    format ["FAIL:input_dead_zone_default actual='%1'", triControlText 572]
};

triEndTest
