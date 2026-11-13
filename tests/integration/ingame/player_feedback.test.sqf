// Requires full game data with Malden (Abel). Also runnable with the WW4 mod.
triSimFrames 30
triAssertEq [triSetPlayerFeedback [true,true], "OK"]
triSimFrames 2
triAssertEq [triPlayerFeedback, "health=100;kills=0;notice=0;healthEnabled=1;killsEnabled=1;visible=1"]
triDamagePlayer 0.25
triSimFrames 2
triAssertEq [triPlayerFeedback, "health=75;kills=0;notice=0;healthEnabled=1;killsEnabled=1;visible=1"]
triAssertEq [triInflictDamage [hudEnemy,player,10], "OK"]
triSimFrames 3
triAssertEq [triPlayerFeedback, "health=75;kills=1;notice=1;healthEnabled=1;killsEnabled=1;visible=1"]
// U hides immediately and holding it must not toggle repeatedly.
triKeyDown 24
triSimFrames 3
triAssertEq [triPlayerFeedback, "health=75;kills=1;notice=0;healthEnabled=1;killsEnabled=1;visible=0"]
triSimFrames 20
triAssertEq [triPlayerFeedback, "health=75;kills=1;notice=0;healthEnabled=1;killsEnabled=1;visible=0"]
triKeyUp 24
triSimFrames 2
triKeyDown 24
triSimFrames 3
triKeyUp 24
triSimFrames 2
triAssertEq [triPlayerFeedback, "health=75;kills=1;notice=0;healthEnabled=1;killsEnabled=1;visible=1"]
triAssertEq [triSaveGame "player-feedback", "OK"]
triAssertEq [triInflictDamage [hudEnemyTwo,player,10], "OK"]
triSimFrames 3
triAssertEq [triPlayerFeedback, "health=75;kills=2;notice=1;healthEnabled=1;killsEnabled=1;visible=1"]
triAssertEq [triLoadGame "player-feedback", "OK"]
triSimFrames 3
triAssertEq [triPlayerFeedback, "health=75;kills=1;notice=0;healthEnabled=1;killsEnabled=1;visible=1"]
triAssertEq [triInflictDamage [hudFriendly,player,10], "OK"]
triAssertEq [triInflictDamage [hudCivilian,player,10], "OK"]
triSimFrames 3
triAssertEq [triPlayerFeedback, "health=75;kills=1;notice=0;healthEnabled=1;killsEnabled=1;visible=1"]
triSetPlayerFeedback [false,false]
triAssertEq [triInflictDamage [hudEnemyTwo,player,10], "OK"]
triSimFrames 3
triSetPlayerFeedback [true,true]
triSimFrames 2
triAssertEq [triPlayerFeedback, "health=75;kills=2;notice=0;healthEnabled=1;killsEnabled=1;visible=1"]
// Rebinding replaces U and the replacement works in both directions.
triBindAction ["TogglePlayerFeedback", 14]
triKeyDown 24
triSimFrames 3
triKeyUp 24
triSimFrames 2
triAssertEq [triPlayerFeedback, "health=75;kills=2;notice=0;healthEnabled=1;killsEnabled=1;visible=1"]
triKeyDown 14
triSimFrames 3
triKeyUp 14
triSimFrames 2
triAssertEq [triPlayerFeedback, "health=75;kills=2;notice=0;healthEnabled=1;killsEnabled=1;visible=0"]
triKeyDown 14
triSimFrames 3
triKeyUp 14
triSimFrames 2
triAssertEq [triPlayerFeedback, "health=75;kills=2;notice=0;healthEnabled=1;killsEnabled=1;visible=1"]
triEndTest
