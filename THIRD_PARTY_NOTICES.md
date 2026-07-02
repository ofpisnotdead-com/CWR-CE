# Third-Party Notices

The source code of this project is licensed under the **GNU General Public License
v3.0 or later** (see [`LICENSE`](LICENSE)). This file reproduces all third-party components used by the project.

There are three groups:

1. **Vendored components** — code that is actually included in this repository, under
   [`thirdparty/`](thirdparty). Their full license texts are reproduced below and are
   the legally operative notices for this distribution.
2. **Dependencies resolved via vcpkg** — declared in [`vcpkg.json`](vcpkg.json) and
   downloaded/built at configure time. They are **not** distributed in this
   repository; vcpkg delivers each package's authoritative `LICENSE`/`COPYING` file
   into your `vcpkg_installed/` tree at build time. They are listed here, with their
   license texts, for completeness and convenience.
3. **Rust dependencies (Cargo)** — the crates used by the project's Rust components
   (the master server under [`mserver/`](mserver) and the [`Trident`](engine/Trident)
   test orchestrator), declared in each crate's `Cargo.toml` and pinned in its
   `Cargo.lock`. Like the vcpkg dependencies, they are **not** distributed in this
   repository — Cargo fetches and builds them — and they are standalone programs not
   linked into the GPL-licensed C++ engine. They are summarized, not reproduced, in
   [§3](#3-rust-dependencies-cargo).

The `thirdparty/` directory and all components listed here are **excluded from the
project's GPL license**; each is governed by its own terms below.

---

# 1. Vendored components (in `thirdparty/`)

## 1.1 glad — OpenGL loader

`thirdparty/glad/` (`include/glad/gl.h`, `include/KHR/khrplatform.h`, `src/gl.c`)

The glad sources carry the SPDX identifier:

```
SPDX-License-Identifier: (WTFPL OR CC0-1.0) AND Apache-2.0
```

glad is a code generator; the generated loader code is dedicated to the public
domain and may be used under **CC0-1.0** (or, at your option, the WTFPL), while the
OpenGL/Khronos API definitions it embeds are made available under the **Apache
License 2.0**. To satisfy the `(WTFPL OR CC0-1.0)` term this project relies on
**CC0-1.0**; the Apache-2.0 portion is mandatory and its full text is reproduced in
[§1.4](#14-apache-license-20) below.

### CC0 1.0 Universal (Public Domain Dedication) — summary

> The person who associated a work with this deed has dedicated the work to the
> public domain by waiving all of their rights to the work worldwide under copyright
> law, including all related and neighboring rights, to the extent allowed by law.
> You can copy, modify, distribute and perform the work, even for commercial
> purposes, all without asking permission.
>
> THE WORK IS PROVIDED "AS IS" AND THE AUTHOR MAKES NO WARRANTIES OF ANY KIND.

Full CC0 1.0 legal code: <https://creativecommons.org/publicdomain/zero/1.0/legalcode>

The Khronos header `KHR/khrplatform.h` is additionally distributed under the
Khronos/MIT-style license reproduced in [§1.3](#13-khronos-group-khrplatformh).

## 1.2 RenderDoc — in-application API header

`thirdparty/renderdoc/renderdoc_app.h`

```
The MIT License (MIT)

Copyright (c) 2015-2026 Baldur Karlsson

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
```

## 1.3 Khronos Group — `khrplatform.h`

`thirdparty/glad/include/KHR/khrplatform.h`

```
Copyright (c) 2008-2018 The Khronos Group Inc.

Permission is hereby granted, free of charge, to any person obtaining a
copy of this software and/or associated documentation files (the
"Materials"), to deal in the Materials without restriction, including
without limitation the rights to use, copy, modify, merge, publish,
distribute, sublicense, and/or sell copies of the Materials, and to
permit persons to whom the Materials are furnished to do so, subject to
the following conditions:

The above copyright notice and this permission notice shall be included
in all copies or substantial portions of the Materials.

THE MATERIALS ARE PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
MATERIALS OR THE USE OR OTHER DEALINGS IN THE MATERIALS.
```

## 1.4 Apache License 2.0

Applies to the Khronos-derived API definitions in glad (see [§1.1](#11-glad--opengl-loader)).

```
                                 Apache License
                           Version 2.0, January 2004
                        http://www.apache.org/licenses/

   TERMS AND CONDITIONS FOR USE, REPRODUCTION, AND DISTRIBUTION

   1. Definitions.

      "License" shall mean the terms and conditions for use, reproduction,
      and distribution as defined by Sections 1 through 9 of this document.

      "Licensor" shall mean the copyright owner or entity authorized by
      the copyright owner that is granting the License.

      "Legal Entity" shall mean the union of the acting entity and all
      other entities that control, are controlled by, or are under common
      control with that entity. For the purposes of this definition,
      "control" means (i) the power, direct or indirect, to cause the
      direction or management of such entity, whether by contract or
      otherwise, or (ii) ownership of fifty percent (50%) or more of the
      outstanding shares, or (iii) beneficial ownership of such entity.

      "You" (or "Your") shall mean an individual or Legal Entity
      exercising permissions granted by this License.

      "Source" form shall mean the preferred form for making modifications,
      including but not limited to software source code, documentation
      source, and configuration files.

      "Object" form shall mean any form resulting from mechanical
      transformation or translation of a Source form, including but
      not limited to compiled object code, generated documentation,
      and conversions to other media types.

      "Work" shall mean the work of authorship, whether in Source or
      Object form, made available under the License, as indicated by a
      copyright notice that is included in or attached to the work
      (an example is provided in the Appendix below).

      "Derivative Works" shall mean any work, whether in Source or Object
      form, that is based on (or derived from) the Work and for which the
      editorial revisions, annotations, elaborations, or other modifications
      represent, as a whole, an original work of authorship. For the purposes
      of this License, Derivative Works shall not include works that remain
      separable from, or merely link (or bind by name) to the interfaces of,
      the Work and Derivative Works thereof.

      "Contribution" shall mean any work of authorship, including
      the original version of the Work and any modifications or additions
      to that Work or Derivative Works thereof, that is intentionally
      submitted to Licensor for inclusion in the Work by the copyright owner
      or by an individual or Legal Entity authorized to submit on behalf of
      the copyright owner. For the purposes of this definition, "submitted"
      means any form of electronic, verbal, or written communication sent
      to the Licensor or its representatives, including but not limited to
      communication on electronic mailing lists, source code control systems,
      and issue tracking systems that are managed by, or on behalf of, the
      Licensor for the purpose of discussing and improving the Work, but
      excluding communication that is conspicuously marked or otherwise
      designated in writing by the copyright owner as "Not a Contribution."

      "Contributor" shall mean Licensor and any individual or Legal Entity
      on behalf of whom a Contribution has been received by Licensor and
      subsequently incorporated within the Work.

   2. Grant of Copyright License. Subject to the terms and conditions of
      this License, each Contributor hereby grants to You a perpetual,
      worldwide, non-exclusive, no-charge, royalty-free, irrevocable
      copyright license to reproduce, prepare Derivative Works of,
      publicly display, publicly perform, sublicense, and distribute the
      Work and such Derivative Works in Source or Object form.

   3. Grant of Patent License. Subject to the terms and conditions of
      this License, each Contributor hereby grants to You a perpetual,
      worldwide, non-exclusive, no-charge, royalty-free, irrevocable
      (except as stated in this section) patent license to make, have made,
      use, offer to sell, sell, import, and otherwise transfer the Work,
      where such license applies only to those patent claims licensable
      by such Contributor that are necessarily infringed by their
      Contribution(s) alone or by combination of their Contribution(s)
      with the Work to which such Contribution(s) was submitted. If You
      institute patent litigation against any entity (including a
      cross-claim or counterclaim in a lawsuit) alleging that the Work
      or a Contribution incorporated within the Work constitutes direct
      or contributory patent infringement, then any patent licenses
      granted to You under this License for that Work shall terminate
      as of the date such litigation is filed.

   4. Redistribution. You may reproduce and distribute copies of the
      Work or Derivative Works thereof in any medium, with or without
      modifications, and in Source or Object form, provided that You
      meet the following conditions:

      (a) You must give any other recipients of the Work or
          Derivative Works a copy of this License; and

      (b) You must cause any modified files to carry prominent notices
          stating that You changed the files; and

      (c) You must retain, in the Source form of any Derivative Works
          that You distribute, all copyright, patent, trademark, and
          attribution notices from the Source form of the Work,
          excluding those notices that do not pertain to any part of
          the Derivative Works; and

      (d) If the Work includes a "NOTICE" text file as part of its
          distribution, then any Derivative Works that You distribute must
          include a readable copy of the attribution notices contained
          within such NOTICE file, excluding those notices that do not
          pertain to any part of the Derivative Works, in at least one
          of the following places: within a NOTICE text file distributed
          as part of the Derivative Works; within the Source form or
          documentation, if provided along with the Derivative Works; or,
          within a display generated by the Derivative Works, if and
          wherever such third-party notices normally appear. The contents
          of the NOTICE file are for informational purposes only and
          do not modify the License. You may add Your own attribution
          notices within Derivative Works that You distribute, alongside
          or as an addendum to the NOTICE text from the Work, provided
          that such additional attribution notices cannot be construed
          as modifying the License.

      You may add Your own copyright statement to Your modifications and
      may provide additional or different license terms and conditions
      for use, reproduction, or distribution of Your modifications, or
      for any such Derivative Works as a whole, provided Your use,
      reproduction, and distribution of the Work otherwise complies with
      the conditions stated in this License.

   5. Submission of Contributions. Unless You explicitly state otherwise,
      any Contribution intentionally submitted for inclusion in the Work
      by You to the Licensor shall be under the terms and conditions of
      this License, without any additional terms or conditions.
      Notwithstanding the above, nothing herein shall supersede or modify
      the terms of any separate license agreement you may have executed
      with Licensor regarding such Contributions.

   6. Trademarks. This License does not grant permission to use the trade
      names, trademarks, service marks, or product names of the Licensor,
      except as required for reasonable and customary use in describing the
      origin of the Work and reproducing the content of the NOTICE file.

   7. Disclaimer of Warranty. Unless required by applicable law or
      agreed to in writing, Licensor provides the Work (and each
      Contributor provides its Contributions) on an "AS IS" BASIS,
      WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or
      implied, including, without limitation, any warranties or conditions
      of TITLE, NON-INFRINGEMENT, MERCHANTABILITY, or FITNESS FOR A
      PARTICULAR PURPOSE. You are solely responsible for determining the
      appropriateness of using or redistributing the Work and assume any
      risks associated with Your exercise of permissions under this License.

   8. Limitation of Liability. In no event and under no legal theory,
      whether in tort (including negligence), contract, or otherwise,
      unless required by applicable law (such as deliberate and grossly
      negligent acts) or agreed to in writing, shall any Contributor be
      liable to You for damages, including any direct, indirect, special,
      incidental, or consequential damages of any character arising as a
      result of this License or out of the use or inability to use the
      Work (including but not limited to damages for loss of goodwill,
      work stoppage, computer failure or malfunction, or any and all
      other commercial damages or losses), even if such Contributor
      has been advised of the possibility of such damages.

   9. Accepting Warranty or Additional Liability. While redistributing
      the Work or Derivative Works thereof, You may choose to offer,
      and charge a fee for, acceptance of support, warranty, indemnity,
      or other liability obligations and/or rights consistent with this
      License. However, in accepting such obligations, You may act only
      on Your own behalf and on Your sole responsibility, not on behalf
      of any other Contributor, and only if You agree to indemnify,
      defend, and hold each Contributor harmless for any liability
      incurred by, or claims asserted against, such Contributor by reason
      of your accepting any such warranty or additional liability.

   END OF TERMS AND CONDITIONS

   APPENDIX: How to apply the Apache License to your work.

      To apply the Apache License to your work, attach the following
      boilerplate notice, with the fields enclosed by brackets "[]"
      replaced with your own identifying information. (Don't include
      the brackets!)  The text should be enclosed in the appropriate
      comment syntax for the file format. We also recommend that a
      file or class name and description of purpose be included on the
      same "printed page" as the copyright notice for easier
      identification within third-party archives.

   Copyright [yyyy] [name of copyright owner]

   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at

       http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License.
```

---

# 2. Dependencies resolved via vcpkg

Declared in [`vcpkg.json`](vcpkg.json). Not distributed in this repository — vcpkg
downloads and builds these, and places each package's authoritative license file
under `vcpkg_installed/.../share/<package>/copyright` at build time. Versions are
noted where pinned by an `overrides` entry in the manifest.

| Component | License (SPDX) | Copyright (summary) | Upstream |
|-----------|----------------|---------------------|----------|
| cJSON | MIT | © 2009–2017 Dave Gamble and cJSON contributors | <https://github.com/DaveGamble/cJSON> |
| curl | curl (MIT/X-style) | © 1996–2024 Daniel Stenberg and many contributors | <https://curl.se> |
| CLI11 *(2.4.0)* | BSD-3-Clause | © 2017–2024 University of Cincinnati | <https://github.com/CLIUtils/CLI11> |
| stb | MIT **OR** Public Domain (Unlicense) | © Sean Barrett | <https://github.com/nothings/stb> |
| mimalloc *(2.2.4)* | MIT | © 2018–2021 Microsoft Corporation, Daan Leijen | <https://github.com/microsoft/mimalloc> |
| SDL3 | Zlib | © 1997–2025 Sam Lantinga | <https://libsdl.org> |
| OpenAL Soft | LGPL-2.1-or-later | © the OpenAL Soft authors (Chris Robinson et al.) | <https://openal-soft.org> |
| Opus | BSD-3-Clause | © 2001–2023 Xiph.Org, Skype Limited, et al. | <https://opus-codec.org> |
| libogg | BSD-3-Clause | © 2002 Xiph.Org Foundation | <https://xiph.org/ogg> |
| libvorbis | BSD-3-Clause | © 2002–2020 Xiph.Org Foundation | <https://xiph.org/vorbis> |
| enkiTS | Zlib | © 2013 Doug Binks | <https://github.com/dougbinks/enkiTS> |
| Dear ImGui | MIT | © 2014–2025 Omar Cornut | <https://github.com/ocornut/imgui> |
| spdlog *(1.15.1)* | MIT (bundles fmt, MIT) | © 2016 Gabi Melman | <https://github.com/gabime/spdlog> |
| FreeType | FTL **OR** GPL-2.0 (dual) | © 1996–2024 David Turner, Robert Wilhelm, Werner Lemberg & the FreeType Project | <https://freetype.org> |
| zstd | BSD-3-Clause | © Meta Platforms, Inc. and affiliates | <https://github.com/facebook/zstd> |
| libzip | BSD-3-Clause | © 1999–2024 Dieter Baron and Thomas Klausner | <https://libzip.org> |

## 2.1 MIT License

Applies to **cJSON, mimalloc, Dear ImGui, spdlog** (and, at your option, **stb**),
each with its own copyright holder as listed in the table above.

```
MIT License

Copyright (c) <year> <copyright holders, per the table above>

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

## 2.2 BSD 3-Clause License

Applies to **CLI11, Opus, libogg, libvorbis, zstd, libzip**, each with its own copyright holder as
listed in the table above.

```
Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice, this
   list of conditions and the following disclaimer.

2. Redistributions in binary form must reproduce the above copyright notice,
   this list of conditions and the following disclaimer in the documentation
   and/or other materials provided with the distribution.

3. Neither the name of the copyright holder nor the names of its contributors
   may be used to endorse or promote products derived from this software
   without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
```

## 2.3 zlib License

Applies to **SDL3** and **enkiTS**, each with its own copyright holder as listed in
the table above.

```
This software is provided 'as-is', without any express or implied warranty. In
no event will the authors be held liable for any damages arising from the use
of this software.

Permission is granted to anyone to use this software for any purpose, including
commercial applications, and to alter it and redistribute it freely, subject to
the following restrictions:

1. The origin of this software must not be misrepresented; you must not claim
   that you wrote the original software. If you use this software in a product,
   an acknowledgment in the product documentation would be appreciated but is
   not required.

2. Altered source versions must be plainly marked as such, and must not be
   misrepresented as being the original software.

3. This notice may not be removed or altered from any source distribution.
```

## 2.4 curl License

Applies to **curl**.

```
COPYRIGHT AND PERMISSION NOTICE

Copyright (c) 1996 - 2024, Daniel Stenberg, <daniel@haxx.se>, and many
contributors, see the THANKS file.

All rights reserved.

Permission to use, copy, modify, and distribute this software for any purpose
with or without fee is hereby granted, provided that the above copyright notice
and this permission notice appear in all copies.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT OF THIRD PARTY RIGHTS. IN
NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE
OR OTHER DEALINGS IN THE SOFTWARE.

Except as contained in this notice, the name of a copyright holder shall not be
used in advertising or otherwise to promote the sale, use or other dealings in
this Software without prior written authorization of the copyright holder.
```

## 2.5 GNU LGPL v2.1 (OpenAL Soft)

OpenAL Soft is licensed under the **GNU Lesser General Public License, version 2.1
or later**, which is compatible with this project's GPL-3.0-or-later license. The
standard library notice is:

```
OpenAL Soft is free software; you can redistribute it and/or modify it under the
terms of the GNU Lesser General Public License as published by the Free Software
Foundation; either version 2.1 of the License, or (at your option) any later
version.

OpenAL Soft is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
PARTICULAR PURPOSE. See the GNU Lesser General Public License for more details.
```

Full license text: <https://www.gnu.org/licenses/old-licenses/lgpl-2.1.html>

**Local modifications.** OpenAL Soft is built from a locally patched copy via the vcpkg
overlay port in [`cmake/vcpkg-overlay-ports/openal-soft/`](cmake/vcpkg-overlay-ports/openal-soft).
The changes are:

- `fix-mixer-uaf-on-source-free.patch` — fixes a use-after-free in the mixer when a source is freed.
- `devendor-fmt.diff` — builds against the external fmt library instead of the vendored copy.
- `pkgconfig-cxx.diff` — corrects C++ linkage flags in the generated pkg-config file.

These patches are distributed under OpenAL Soft's own GNU LGPL-2.1-or-later. OpenAL Soft is
dynamically linked; as required by the LGPL, you may relink the program against your own
build of the library.

## 2.6 FreeType License (FTL) / GPLv2 (FreeType)

FreeType is dual-licensed: you may use it under the **FreeType License (FTL)**, a
BSD-style license with a credit clause, **or** under the **GNU General Public
License v2**. The FTL is compatible with GPL-3.0-or-later (it is *not* compatible
with GPLv2-only — which is why this project uses GPL "v3.0 *or later*"). FreeType
asks that distributions display the following credit:

```
Portions of this software are copyright © 1996-2024 The FreeType Project
(www.freetype.org). All rights reserved.
```

Full FTL text: <https://gitlab.freedesktop.org/freetype/freetype/-/raw/master/docs/FTL.TXT>
GPLv2 option: <https://gitlab.freedesktop.org/freetype/freetype/-/raw/master/docs/GPLv2.TXT>

## 2.7 The Unlicense (stb — public-domain option)

**stb** is offered under MIT (see [§2.1](#21-mit-license)) **or** the public-domain
Unlicense, at your option.

```
This is free and unencumbered software released into the public domain.

Anyone is free to copy, modify, publish, use, compile, sell, or distribute this
software, either in source code form or as a compiled binary, for any purpose,
commercial or non-commercial, and by any means.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS BE
LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF
CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

For more information, please refer to <https://unlicense.org>
```

# 3. Rust dependencies (Cargo)

The Rust components — the master server under `mserver/` (`Archive`, `CLI`, `Client`,
`MasterService`) and the `Trident` test orchestrator under `engine/Trident/` — depend on
third-party crates declared in each crate's `Cargo.toml` and pinned in its `Cargo.lock`.
Like the vcpkg dependencies, these crates are **fetched and built by Cargo** and are **not
distributed in this repository**.

These are standalone Rust programs; they are not linked into the GPL-licensed C++ engine.
Their crates are overwhelmingly permissive (MIT, Apache-2.0, `MIT OR Apache-2.0`, BSD, ISC).

If you distribute a compiled Rust binary (e.g. the master service), ship a complete
per-crate license report generated from the lockfile, and verify license policy:

    cargo install cargo-about cargo-deny
    cargo about generate about.hbs > THIRD_PARTY_RUST.html   # per-crate notices
    cargo deny check licenses                                # flag any disallowed license

The authoritative license text for each crate is delivered into the Cargo registry cache
at build time.
---

## Notes

- The copyright lines for the vcpkg dependencies in §2 are summarised for reference.
  The authoritative, version-exact copyright and license files are produced by vcpkg
  under `vcpkg_installed/` when you build, and should be the ones shipped with any
  binary redistribution you make.
- Standard license bodies (MIT, BSD-3-Clause, zlib) are reproduced once and shared by
  several components; the specific copyright holder for each is given in the table in
  §2. Verbatim per-project headers remain in each upstream package.
- This file documents third-party components only. The project's own code remains
  under GPL-3.0-or-later with the Section 7 additional terms in [`LICENSE`](LICENSE).
