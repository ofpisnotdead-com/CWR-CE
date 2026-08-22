triAssertIncludes [triDownloadFile "https://fixtures.test/resume.txt", "fixture-download-resumed-after-disconnect"]
triAssertIncludes [triDownloadFile "https://fixtures.test/no-range.txt", "fixture-download-resumed-after-disconnect"]
triAssertIncludes [triDownloadFile "https://fixtures.test/changed.txt", "fixture-download-resumed-after-disconnect"]
triAssertIncludes [triDownloadFile "https://fixtures.test/repeated.txt", "fixture-download-resumed-after-disconnect"]
triEndTest
