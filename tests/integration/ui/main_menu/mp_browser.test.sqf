// MP server browser: chrome labels, language refresh, seeded-rows smoke.

triSetLanguage "English"
triAssertEq [(triDisplay), 0]
triScreenshot "00_main_menu"

triClick 105
triAssertEq [(triDisplay), 8]

// Chrome labels and status controls.
triAssertIncludes [(triVisibleTexts), "Server"]
triAssertIncludes [(triVisibleTexts), "Mission"]
triAssertIncludes [(triVisibleTexts), "Players"]
triAssertIncludes [(triVisibleTexts), "Ping"]
triAssertIncludes [(triVisibleTexts), "Cancel"]
triAssertIncludes [(triVisibleTexts), "Join"]
triAssertIncludes [(triVisibleTexts), "New"]
triAssertEq [(triControlText 109), "Operated by master.example"]
triAssertEq [(triControlText 122), "Address: LAN"]
triAssertEq [(triControlText 107), "Password: No password"]
triScreenshot "01_mp_server_browser"

// Language switch updates localized status controls.
triSetLanguage "Czech"
triAssertEq [(triControlText 109), "Operated by master.example"]
triAssertEq [(triControlText 122), "Adresa: LAN"]
triAssertEq [(triControlText 107), "Heslo: Bez hesla"]
triSetLanguage "English"

// Seeded rows: browser survives session injection.
triSeedSessions 8
triAssertEq [(triDisplay), 8]
triAssertIncludes [(triVisibleTexts), "Server"]
triWaitFrames 30
triScreenshot "mp_browser_tags"

triClick 2
triAssertEq [(triDisplay), 0]
triEndTest
