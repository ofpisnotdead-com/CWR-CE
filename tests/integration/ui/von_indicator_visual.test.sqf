triSetLanguage "English"
triSimFrames 60

triSetVoiceIndicators []
triSimFrames 10
triScreenshot "00_none"

triSetVoiceIndicators [["Miller", "global"]]
triSimFrames 10
triScreenshot "01_one_global"

triSetVoiceIndicators [["Miller", "side"], ["Novak", "side"]]
triSimFrames 10
triScreenshot "02_two_same_side"

triSetVoiceIndicators [["Miller", "side"], ["Novak", "vehicle"]]
triSimFrames 10
triScreenshot "03_two_mixed_side_vehicle"

triSetVoiceIndicators [["Miller", "direct"], ["Novak", "group"], ["Petrov", "global"]]
triSimFrames 10
triScreenshot "04_three_mixed"

triSetVoiceIndicators []
triEndTest
