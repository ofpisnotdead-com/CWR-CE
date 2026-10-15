#ifndef _RANDOMGEN_HPP
#define _RANDOMGEN_HPP

#include <Poseidon/Foundation/platform.hpp>

class RandomTable
{
  public:
    enum
    {
        Size = 32768
    };

  private:
    short int _table[Size];

  public:
    RandomTable(int seed = 1);
    // returned number is always in range 0..Size-1
    int operator()(int i) const { return _table[i & (Size - 1)]; }
    int Seed(int x, int z, int y) const;
    //! seed combiner - fast (unrolled) version
    int Seed(int x, int z) const;
    //! seed combiner - reference (slow, unoptimized C source) version
    int SeedRef(int x, int z) const;
};

class RandomGenerator
{
    mutable int _seed;
    RandomTable _seedTable;
    RandomTable _valueTable;

  public:
    RandomGenerator(int seed1, int seed2);
    RandomGenerator();
    float RandomValue(int seed) const;
    float RandomValue(int x, int z) const { return RandomValue(GetSeed(x, z)); }
    float RandomValue(int x, int z, int y) const { return RandomValue(GetSeed(x, z, y)); }
    void SetSeed(int seed) { _seed = seed; }
    float RandomValue() const;
    float Gauss(float min, float mid, float max) const;
    float PlusMinus(float a, float b) const;

    __forceinline int GetSeed(int x, int z) const { return _seedTable.Seed(x, z); }
    __forceinline int GetSeedRef(int x, int z) const { return _seedTable.SeedRef(x, z); }
    __forceinline int GetSeed(int x, int z, int y) const { return _seedTable.Seed(x, z, y); }
};

// Meyers singleton accessor — constructed on first use, no static-init-order hazard.
inline RandomGenerator& GRandGen()
{
    static RandomGenerator instance;
    return instance;
}

// Let call sites spell the singleton as `GRandGen.Foo()`; expands to `GRandGen().Foo()`.
#define GRandGen GRandGen()

// Separate RNG stream for cosmetic render-side effects (smoke/dust/cloudlet particles).
// These consume a nondeterministic number of random values per frame — particle spawn
// and lifecycle depend on camera, visibility and frame timing — so sharing the sim's
// GRandGen desynced the MP-critical simulation RNG stream from the first seconds of a
// mission.  A fixed seed alone could not keep two rendered runs in lockstep: the sim
// consumed GRandGen a different number of times each run, which rarely (~10%) shifted an
// AI soldier's head-look timer by one tick, altering its skinned head collision geometry
// and hence a collision response — a determinism divergence only ever reproducible in the
// rendered client (never under --simulate, which spawns no particles).  Effects now draw
// from this independent stream; the simulation owns GRandGen exclusively.  Never use this
// for anything that influences simulation state.
inline RandomGenerator& GFxRandGen()
{
    static RandomGenerator instance;
    return instance;
}
#define GFxRandGen GFxRandGen()

#endif
