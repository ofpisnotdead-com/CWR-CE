// The multiplayer wizard must load a template's overview and re-resolve it to the
// localized name when the language changes (the wizard registers a language-changed
// callback that reloads the overview). Demo ships a single MP template, so this
// covers the overview load and the localization callback; without the callback the
// overview stays in the language it was first shown in.

triSetLanguage "English"
triAssertEq [(triDisplay), 0]

// Main menu -> Multiplayer -> New server.
triClick 105
triAssertEq [(triDisplay), 8]
triClick 104
triAssertEq [(triDisplay), 17]

// Pick the demo world and open its template wizard.
triAssertEq [triSelectListByData [101, "demo"], true]
triSelectList [102, 1]
triClick 1
triAssertEq [(triDisplay), 67]

// The template exposes its id as row data and its overview resolves the name.
triAssertEq [triSelectListByData [101, "1-6_C_Cooperative"], true]
triWaitFrames 8
triAssertEq [(triAssertIncludes [(triControlText 102), "Clean Sweep"]), "OK"]

// Switching language must reload the overview to the localized name.
triSetLanguage "Czech"
triWaitFrames 8
triAssertEq [(triAssertIncludes [(triControlText 102), "Čistka"]), "OK"]
triAssertEq [(triAssertExcludes [(triControlText 102), "Clean Sweep"]), "OK"]

triEndTest
