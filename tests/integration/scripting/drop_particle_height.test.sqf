// A standalone drop position is [x, y, height above the surface], and the
// particle reports its own height back to its onTimer script in the same form.

triSimFrames 30

dropReports = 0
dropReportedZ = -1e6
groundPos = getPos player

drop ["cl_basic", "", "Billboard", 0.05, 0.6, [(groundPos select 0) + 2, (groundPos select 1), 5], [0,0,0], 0, 1, 0.001, 0, [1,1], [[1,1,1,1],[1,1,1,0]], [0], 0, 0, "particle_pos.sqs", "", ""]

triSimFrames 6

triAssertGt [dropReports, 0]
triAssertNear [dropReportedZ, 5, 2]

triEndTest
