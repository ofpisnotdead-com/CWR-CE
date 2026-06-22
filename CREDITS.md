# Credits

*Arma: Cold War Assault Remastered* (engine source, codename **Poseidon** / CWR) exists thanks
to the people and projects below.

## Bohemia Interactive

Developed and published by **[Bohemia Interactive](https://www.bohemia.net/)**.

The engine and game began life as **Operation Flashpoint: Cold War Crisis** (2001),
later re-released as **Arma: Cold War Assault**. This source release, and the
decision to make it available to the community under the GNU GPL, are the work of
Bohemia Interactive and the studio's original and ongoing development teams.

Our thanks to the original Operation Flashpoint / Cold War Crisis team, whose work
on the **Poseidon** engine — the foundation of Bohemia's later *Real Virtuality*
and *Enfusion* technology — is what you are reading here.

## The community

Thanks to the *Operation Flashpoint* / *Arma* modding and content-creation
community, who have kept this game alive for more than two decades and for whom this
release is intended.

Special thanks to Josef Šimánek, "simi", for development archaeology, codebase work, and preparation of this source release.

Thanks to the community testers:

- Petr "Killer14" Švarc
- Vojtěch "Hammer" Zatloukal
- Tony "TonyHawk" Bako
- Marek "Topal12" Lehečka
- Dr. Alex Mercer
- Inlesco
- t
- Foie
- Vojtěch "snw" Chaloupka
- Spez

The community continuation welcomes pull requests, ideas, fixes, ports, tests,
and documentation work in this repository.

## Third-party software

This project builds on open-source work. Per-component licenses are listed
in [`thirdparty/README.md`](thirdparty/README.md), and dependencies are declared in
[`vcpkg.json`](vcpkg.json). With thanks to the authors and maintainers of:

**Vendored in `thirdparty/`**

- **glad** — OpenGL function loader (David Herberth and contributors).
- **RenderDoc** in-application API header (Baldur Karlsson).
- **Khronos** `khrplatform.h` (The Khronos Group).

**Resolved via vcpkg**

- **SDL3** — windowing, input, and platform abstraction (Sam Lantinga and the SDL team).
- **OpenAL Soft** — audio backend (Chris "kcat" Robinson and contributors).
- **Opus**, **libogg**, **libvorbis** — audio codecs (the Xiph.Org Foundation).
- **FreeType** — font rasterisation (David Turner, Robert Wilhelm, Werner Lemberg, and the FreeType Project).
- **curl** — networking/transfers (Daniel Stenberg and contributors).
- **cJSON** — JSON parsing (Dave Gamble and contributors).
- **CLI11** — command-line parsing (University of Cincinnati and contributors).
- **stb** — single-file utility headers (Sean Barrett).
- **mimalloc** — allocator (Microsoft / Daan Leijen).
- **enkiTS** — task scheduler (Doug Binks).
- **Dear ImGui** — developer/debug UI (Omar Cornut and contributors).
- **spdlog** — logging (Gabi Melman and contributors).

## Embedded algorithms

- **ISAAC** pseudo-random number generator by **Bob Jenkins** (public domain),
  with the `QTIsaac` C++ template implementation by **Quinn Tyler Jackson**.
- **RANMAR** random number generator C++ port by **Phil Linttell**, kept for
  legacy network compatibility.
- **CRC-32** table generation follows the implementation described in the
  `comp.compression` FAQ.
- Various numerical and fast-math routines in `engine/Poseidon/Foundation/Math`
  adapted from publicly published references.

---

*"ARMA" is a registered trademark of BOHEMIA INTERACTIVE a.s. "OPERATION FLASHPOINT" is a registered trademark of Electronic Arts Inc.
See [`LICENSE`](LICENSE) for information concerning trademarks. This credits file is
informational and does not constitute any grant and/or waiver of rights.*
