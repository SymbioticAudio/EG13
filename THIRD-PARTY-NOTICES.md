# Third-party notices

EG13 is free software under the GNU General Public License, version 3 (see `LICENSE`). The shipped plugins (`EG13.component`, `EG13.vst3` and `EG13.clap`) are built from EG13's own code and from the components below. Every one of them is under a licence compatible with the GPL-3.0, so the plugins as a whole are distributed under the GPL-3.0. Each section gives the notices its licence asks to be kept with copies of the software.

The source code of EG13 and of every component is in the EG13 repository at the release tag (`SOURCE.txt` in the zip gives the address): the components in `third_party/` are git submodules pinned to the versions below, and the build fetches the VST3 and AudioUnit SDKs at the tags listed.

## Components in the plugins

| Component | Version | What EG13 uses it for | Licence | In | GPL-3.0 compatible |
| --- | --- | --- | --- | --- | --- |
| [Eurorack firmware](https://github.com/pichenettes/eurorack) by Emilie Gillet (DSP code of Plaits, Braids, Elements, Rings, Warps, Stages, Tides 2, Marbles, Peaks, Clouds, Beads and stmlib) | commit f4cfa76 | the engines | MIT | all | yes |
| [AudibleInstruments](https://github.com/VCVRack/AudibleInstruments) (the VCV models of Ripples and Shelves) | commit a1cd335 | Analog Filter, EQ | GPL-3.0-or-later | all | yes (same licence) |
| VCV Rack SDK parts (EG13's `core/engine/rack_shim`, written after [VCV Rack](https://github.com/VCVRack/Rack) v2) | Rack commit 061ccf6 | lets the AudibleInstruments models build without Rack | GPL-3.0-or-later | all | yes (same licence) |
| [sst-filters](https://github.com/surge-synthesizer/sst-filters) and [sst-basic-blocks](https://github.com/surge-synthesizer/sst-basic-blocks) (Surge Synth Team) | tag `last_cpp_17` | Classic Filters | GPL-3.0-or-later | all | yes (same licence) |
| [SIMD Everywhere (simde)](https://github.com/simd-everywhere/simde) | 0.8.2 | SSE intrinsics on Apple silicon, for sst and the VCV models | MIT (a few files CC0-1.0) | all | yes |
| [r8brain-free-src](https://github.com/avaneev/r8brain-free-src) | 7.6 | sample-rate conversion for the engines that run at their own rate | MIT | all | yes |
| [JSON for Modern C++ (nlohmann/json)](https://github.com/nlohmann/json) | 3.12.0 | saved state and presets | MIT | all | yes |
| [CLAP](https://github.com/free-audio/clap) | 1.2.10 | the plugin API | MIT | all | yes |
| [clap-wrapper](https://github.com/free-audio/clap-wrapper) | 0.17.0 | the AU and VST3 formats | MIT | AU, VST3 | yes |
| [{fmt}](https://github.com/fmtlib/fmt) (bundled with clap-wrapper) | 11.1.4 | text formatting in the VST3 wrapper | MIT | VST3 | yes |
| PreSonus Plug-In Extensions (bundled with clap-wrapper) | — | gain-reduction reporting in the VST3 wrapper | public domain | VST3 | yes |
| [VST 3 SDK](https://github.com/steinbergmedia/vst3sdk) (Steinberg Media Technologies) | 3.8.0 build 66 | the VST3 format | MIT | VST3 | yes |
| [AudioUnitSDK](https://github.com/apple/AudioUnitSDK) (Apple) | 1.1.0 | the AU format | Apache-2.0 | AU | yes (Apache-2.0 is compatible with GPL-3.0) |
| [symbiotic-design](https://github.com/SymbioticAudio/symbiotic-design): SymbioticTokens and SymbioticIcons (the standard icons) | 0.1.0 | the editor's colours, type styles and icons | MIT | all | yes |

## Not in the plugins

- **SymbioticBrand** (the SymbioticAudio glyph artwork in symbiotic-design) is all rights reserved, not open source. Release builds leave it out (`EG13_NO_BRAND`), and the release packaging (`scripts/package-release.sh`) fails if any of its symbols is in a plugin. The editor then uses the MIT standard icons.
- **Catch2** (BSL-1.0) builds the test suite only.
- **RtAudio and RtMidi**, which clap-wrapper fetches for its Standalone app, are only in `EG13.app`, which is not shipped.
- **AudioKit** is not used.
- **Fonts:** none are bundled; the editor uses the system fonts of macOS.
- **The Swift runtime, SwiftUI, AppKit and the other Apple frameworks** are linked dynamically from macOS itself (`/usr/lib/swift`, `/System/Library`); they are system libraries in the sense of the GPL-3.0 and are not part of the plugins.
- **Factory presets** are EG13's own work under the GPL-3.0-or-later unless a bank says otherwise; a preset bank by another author is credited, with its licence, in the manual's presets page and in this file when it ships.

## Bank 2 factory presets (CC BY 4.0)

The instrument's Bank 2 presets (*catnap* and its variations, sources in `presets/bank2-catnap/`) are a rebuild, in EG13's engines, of the VCV Rack patch "~zero-g:catnap : vcv rack jam" by **plantay**, published at <https://patchstorage.com/zero-gcatnap-vcv-rack-jam/> under the Creative Commons Attribution 4.0 International licence (CC BY 4.0, <https://creativecommons.org/licenses/by/4.0/>). The presets are adapted from the patch: its module settings were translated onto EG13's slots, parts of its routing are approximated and some of its modules are left out (each preset's note says what changed). The patch file itself is not included. Each Bank 2 preset carries the credit "~zero-g:catnap by plantay (patchstorage.com/zero-gcatnap-vcv-rack-jam)" and the licence "CC BY 4.0", both stored in the preset as it ships inside the plugin. The patch's author does not endorse EG13. No warranties are given; the licence may not give all the permissions needed for an intended use.

## GPL-3.0-or-later components

The Ripples and Shelves models (AudibleInstruments `src/Ripples`, `src/Shelves`): Copyright (C) 2020 Tyler Coy. AudibleInstruments: copyright © 2016-2020 Andrew Belt and Audible Instruments contributors. VCV Rack: copyright © Andrew Belt (VCV) and the VCV Rack contributors. sst-filters and sst-basic-blocks: copyright various authors (Surge Synth Team), as described in their git history.

These are under the GNU General Public License, version 3 or (at your option) any later version; EG13 uses them under version 3, its own licence, whose full text is in `LICENSE`.

## MIT-licensed components

Each of these components is distributed under the MIT licence, whose text follows the list, with the copyright notices below.

- **Eurorack firmware (DSP code and stmlib):** Copyright 2012-2022 Emilie Gillet. EG13 ships one modified file, stmlib's `utils/random` (its generator state is per thread), in `third_party/eurorack-patches`.
- **SIMD Everywhere:** Copyright (c) 2017 Evan Nemerson and the SIMD Everywhere contributors.
- **r8brain-free-src:** Copyright (c) 2013-2026 Aleksey Vaneev.
- **JSON for Modern C++:** Copyright (c) 2013-2025 Niels Lohmann. It contains code copyright (c) 2008-2009 Björn Hoehrmann, (c) 2009 Florian Loitsch, (c) 2016-2021 Evan Nemerson and (c) 2018 The Abseil Authors, all under the MIT licence.
- **CLAP:** Copyright (c) 2021 Alexandre BIQUE.
- **clap-wrapper:** Copyright (c) 2022 defiantnerd.
- **{fmt}:** Copyright (c) 2012 - present, Victor Zverovich.
- **VST 3 SDK:** Copyright (c) 2025, Steinberg Media Technologies GmbH. VST is a registered trademark of Steinberg Media Technologies GmbH.
- **symbiotic-design (SymbioticTokens, SymbioticIcons):** Copyright (c) 2026 Westlee Latta.

```
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

## Public-domain components

- **PreSonus Plug-In Extensions:** written and placed in the public domain by PreSonus Software Ltd.
- **SIMD Everywhere:** a few files are dedicated to the public domain under CC0-1.0.

## AudioUnitSDK (Apache License 2.0)

AudioUnitSDK: © 2000-2021 Apple Inc. EG13 uses it unmodified, through clap-wrapper. It has no NOTICE file. Its licence:

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
