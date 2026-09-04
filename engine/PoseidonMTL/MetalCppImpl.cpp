// metal-cpp is header-only; the *_PRIVATE_IMPLEMENTATION macros must be
// defined in exactly one translation unit across the whole binary to emit
// the Objective-C bridging glue. This is that translation unit — kept
// separate from EngineMTLBootstrap.cpp/EngineMTL.cpp so either one (or both)
// can be linked into a binary without a duplicate-definition error.
#define NS_PRIVATE_IMPLEMENTATION
#define CA_PRIVATE_IMPLEMENTATION
#define MTL_PRIVATE_IMPLEMENTATION
#include <Foundation/Foundation.hpp>
#include <QuartzCore/QuartzCore.hpp>
#include <Metal/Metal.hpp>
