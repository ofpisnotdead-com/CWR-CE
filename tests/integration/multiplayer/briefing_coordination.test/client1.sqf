// Regression test: Client 1 auto-ready in briefing
// With --mp-assign, client auto-launches to play state after countdown
// Must reach NGSBriefing (13) and auto-progress to NGSPlay (14) via
// DisplayClientGetReady::OnSimulate() auto-launch logic.
//
// This tests that clients don't get stuck in briefing when server signals play.

triAssertNgsClient 14
triAssertMissionPlayable
