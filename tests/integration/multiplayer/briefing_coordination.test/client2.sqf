// Regression test: Client 2 auto-ready in briefing
// With --mp-assign, client auto-launches to play state after countdown
// Must reach NGSBriefing (13) and auto-progress to NGSPlay (14) via
// DisplayClientGetReady::OnSimulate() auto-launch logic.
//
// This tests that both clients coordinate correctly and don't deadlock.

triAssertNgsClient 14
triAssertMissionPlayable
