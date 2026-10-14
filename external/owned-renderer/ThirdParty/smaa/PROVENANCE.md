SMAA by Jorge Jimenez, Jose I. Echevarria, Belen Masia, Fernando Navarro,
and Diego Gutierrez. Used under the MIT licence in LICENSE.txt.

Upstream: https://github.com/iryoku/smaa
The director supplied SMAA.hlsl and the generated area/search C headers from
upstream on 2026-09-29. The upstream commit was not supplied.

SHA-256 of the supplied files:
- SMAA.hlsl: 4db53d92ef0b45661f8450303589ffabce93c352cae4282a6f32dd2b80efbeb9
- Textures/AreaTex.h: 1933bf43ce86ba71f7ed5f0fa94b148610c634dc3395b9bc67a817d51ee73af2
- Textures/SearchTex.h: 1fac315ab5f87b60083c9b8387af17f056204d3d1bf21500f2acc5b3e8c1a37a

The upstream files are unmodified. Tools/embed-smaa.py generates the embedded
shader string in Source/smaa_embedded.h (the original text uses Latin-1).
The renderer supplies explicit sampler registers and a fullscreen wrapper at
compilation, selects SMAA HIGH/luma/1x, and clamps the final scene output.
The area and search headers are embedded directly as their generated constants.
