#ifdef _MSC_VER
#pragma once
#endif

#ifndef _ENGINE_FACTORY_HPP
#define _ENGINE_FACTORY_HPP

namespace Poseidon
{
class Engine;

// GL33 backend constructor entry-point.  Args mirror the constructor signature.
Engine* CreateEngineGL33(int w, int h, bool windowed, int bpp);

// Metal backend constructor entry-point (macOS / Apple Silicon only).
Engine* CreateEngineMTL(int w, int h, bool windowed, int bpp);

} // namespace Poseidon
#endif
