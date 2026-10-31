# EID3D11 renderer - notices

The EID3D11 renderer source in this repository is licensed under the
PolyForm Noncommercial License 1.0.0 (see `LICENSE.md`,
<https://polyformproject.org/licenses/noncommercial/1.0.0>), except for the
third-party components listed below, which keep their own licences.

In short: you may use, study, modify and redistribute it for any
noncommercial purpose (game mods, community projects, personal use).
Commercial use of any kind, including selling it or anything built on it,
is not licensed. Anyone you pass a copy to, modified or not, must also get
the licence terms and every line below that starts with `Required Notice:`.

Required Notice: Copyright (c) 2026 Ilufus. EID3D11 renderer for Evil Islands (https://github.com/Ilufus/evil-islands-owned-renderer).
Required Notice: Licensed under the PolyForm Noncommercial License 1.0.0 (https://polyformproject.org/licenses/noncommercial/1.0.0). Commercial use is not permitted.
Required Notice: Projects that include or are based on this renderer must credit "EID3D11 renderer by Ilufus" in their README or credits.

## Modified copies

If you distribute a modified version, please mark it as modified and do not
present it as an upstream EID3D11 build (for example, do not report an
upstream build commit for modified code), so that problems in your version
are not reported against this project. There is no obligation on the
licensor to provide builds, ports, platform versions or support.

## Research and references

This renderer is independently written. Its design was informed by studying
other renderer projects, such as GD3D11, as references for techniques; no
code from them is included.

## Third-party components (their own licences apply)

| Component | Location | Licence |
|---|---|---|
| Dear ImGui | `ThirdParty/imgui` | MIT, see `ThirdParty/imgui/LICENSE.txt` |
| meshoptimizer | `ThirdParty/meshoptimizer` | MIT, see `ThirdParty/meshoptimizer/LICENSE.md` |
| SMAA | `ThirdParty/smaa` | see `ThirdParty/smaa/LICENSE.txt` |
| AMD FidelityFX Super Resolution 2 | `ThirdParty/fsr2` | MIT, see `ThirdParty/fsr2/LICENSE.txt` |
| NVIDIA DLSS (nvngx_dlss.dll, not in this repository) | shipped with builds | NVIDIA RTX SDKs licence |

The camp UI art in `Assets/camp` was generated for this project; see
`Assets/camp/PROVENANCE.txt`.
