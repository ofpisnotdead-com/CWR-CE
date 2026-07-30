triAssertNgsClient 14
triAssertMissionPlayable
triAssertNetworkAssetExists ["squad", "CWR", "synthetic_grid.paa"]
triAssertPlayerSquad ["CWR Test", ""]
triAssertPlayerSquad ["CWR Test", "synthetic_grid.paa"]
triSendKey 19
triWaitFrames 10
triScreenshot "client1_squad_info"
triAssertNear [(triSamplePixel [0.512, 0.744]), "255,0,0", 16]
