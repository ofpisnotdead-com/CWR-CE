triAssertNgsClient 14
triAssertMissionPlayable
triAssertNetworkAssetExists ["squad", "CWR", "synthetic_grid.paa"]
triAssertPlayerSquad ["CWR Test", "synthetic_grid.paa"]
triSendKey 19
triWaitFrames 10
triScreenshot "client1_squad_info"
triAssertNear [(triSamplePixel [0.497, 0.707]), "0,0,255", 16]
triAssertNear [(triSamplePixel [0.531, 0.706]), "0,255,0", 16]
triAssertNear [(triSamplePixel [0.517, 0.734]), "255,0,0", 16]
triAssertNear [(triSamplePixel [0.497, 0.761]), "255,255,0", 16]
triAssertNear [(triSamplePixel [0.537, 0.761]), "255,0,255", 16]
