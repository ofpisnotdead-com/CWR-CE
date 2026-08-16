triAssertNgsClient 14
triAssertMissionPlayable
triAssertNetworkAssetExists ["squad", "CWR", "synthetic_grid.paa"]
triAssertRemotePlayerSquad ["CWR Test", "synthetic_grid.paa"]
triSetView [6710.53,84.4,5411.4,0,-0.1,-1]
triWaitFrames 10
triScreenshot "client2_squad_insignia"
triAssertEq [(triAssertRegionHasColor [0.05, 0.82, 0.60, 0.99, 0, 0, 255, 24]), "OK"]
triAssertEq [(triAssertRegionHasColor [0.05, 0.82, 0.60, 0.99, 0, 255, 0, 24]), "OK"]
triAssertEq [(triAssertRegionHasColor [0.05, 0.82, 0.60, 0.99, 255, 0, 0, 24]), "OK"]
