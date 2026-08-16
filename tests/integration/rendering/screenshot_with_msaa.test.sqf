// Pins that a screenshot taken while MSAA is on reads the resolved window
// image. triScreenshot flushes mid-frame, while the multisampled scaled target
// is still bound, and a GL error there is fatal under strict mode.
//
// A render scale above 1 also makes the frame target larger than the window,
// so the capture has to size the image from the window.

triSimFrames 5;
triSetMsaa 8;
triSetRenderScale 1.5;
triSimFrames 5;

triScreenshot "msaa_8x_supersampled";

triEndTest;
