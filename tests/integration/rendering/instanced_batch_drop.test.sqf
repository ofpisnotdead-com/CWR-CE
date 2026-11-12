// A batched instanced run must draw every member. When the run's head bails out before it
// reaches either EmitDraw or QueueAdd it submits nothing, and a completion contract that
// only asks "did anything complain?" reports success, so Scene skips the whole run and the
// objects vanish. The treeline here is one such run: the trees survive the first look and
// disappear once the camera has orbited away and back, because the draw order decides which
// instance becomes the head.
//
// The samples sit on canopy that is open sky once the run is dropped, and they were checked
// to discriminate at 800x600, 1280x720 and 1920x1080 - the game letterboxes at 16:9, so a
// point that works at 4:3 alone is not enough. --sw-tl is load-bearing: on hardware every
// run completes, so the head-bails state is only reachable on the software path.
//
// Broken-state delta: both samples read sky, max channel 192 and 195, against a threshold
// of 130. With the fix they read canopy at 55 to 79.

triSetLanguage "English"
triSimFrames 60

cwrCam = "camera" camCreate [6825, 10425, 2]
cwrCam cameraEffect ["internal", "back"]

cwrCam camSetPos [6825, 10425, 2]
cwrCam camSetTarget [6925, 10425, 2]
cwrCam camCommit 0
triSimFrames 35
triScreenshot "01_treeline_first"
triAssertLt [(triGetPixelMaxChannel [0.55, 0.42]), 130]
triAssertLt [(triGetPixelMaxChannel [0.60, 0.38]), 130]

cwrCam camSetPos [6825, 10425, 2]
cwrCam camSetTarget [6825, 10525, 2]
cwrCam camCommit 0
triSimFrames 35

cwrCam camSetPos [6825, 10425, 2]
cwrCam camSetTarget [6725, 10425, 2]
cwrCam camCommit 0
triSimFrames 35

cwrCam camSetPos [6825, 10425, 2]
cwrCam camSetTarget [6825, 10325, 2]
cwrCam camCommit 0
triSimFrames 35

cwrCam camSetPos [6825, 10425, 2]
cwrCam camSetTarget [6925, 10425, 2]
cwrCam camCommit 0
triSimFrames 35
triScreenshot "02_treeline_after_orbit"
triAssertLt [(triGetPixelMaxChannel [0.55, 0.42]), 130]
triAssertLt [(triGetPixelMaxChannel [0.60, 0.38]), 130]

triEndTest
