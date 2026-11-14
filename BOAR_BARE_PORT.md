# Author bare material port
Source: Ilufus/evil-islands-owned-renderer commit 35ba33dd56440e605f124ab7f24fd15947b97839, found in claude/cand-1114.
Ported only material_library.h, renderer.h, renderer_materials.cpp, renderer_fur.cpp. Preserved local synchronous material profile path and embedded x64 declarations; did not import gem/scenery/asynchronous changes from author context.
Both D3D11 and D3D12 receive bare class masks and class textures. Body/head/tail/limb coat triangles touching bare classes skip shells, fins and strand generation. Hair and armour keep their own paths. Requires material_quality > 0, matching this author revision.
Installed boar maps checked using exact author 0.2 inward vertex sampling: all 192 tusk triangles rejected for both skins, all other visible parts retain coat. No gameplay test performed.
Engine64 bone class also requires bare=true; data prepared separately in BOARBARE02.
