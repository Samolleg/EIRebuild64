#include "../Game/OwnedRender/ModelPreviewVisualRole.h"
#include <iostream>
#include <memory>
#include <stdexcept>

static void require(bool valid, const char* reason) {
    if (!valid) throw std::runtime_error(reason);
}
struct Preview {
    OwnedRender::ModelPreviewVisualRole role;
    std::shared_ptr<int> assembly = std::make_shared<int>(1);
    std::shared_ptr<int> views = std::make_shared<int>(2);
    unsigned invalidations = 0;
    void Capture(bool sourceHero) {
        role.Capture(sourceHero, [&] {
            assembly.reset(); views.reset(); ++invalidations;
        });
    }
};
int main() try {
    // A normal world object follows the current party role; merely creating a
    // UI object must not classify every human/template as the main character.
    OwnedRender::ModelPreviewVisualRole world;
    require(!world.Resolve(false) && world.Resolve(true), "world role changed");
    Preview hero, companion;
    // Camp deployment temporarily publishes private IDs in the party. Capture
    // then; once original IDs are restored, IsMainHero(privateCopy) is false.
    hero.Capture(true); companion.Capture(false);
    require(hero.role.Resolve(false), "private hero lost registry heroCharacter");
    require(!companion.role.Resolve(false) && !companion.role.Resolve(true),
            "captured nonhero inherited another actor's hero role");
    require(!hero.assembly && !hero.views && hero.invalidations == 1,
            "capturing role did not invalidate existing preview resources");
    require(!world.Resolve(false), "copy contaminated source/world role");
    std::cout << "PASS world role, private hero retention and companion isolation\n";

    hero.assembly = std::make_shared<int>(3); hero.views = std::make_shared<int>(4);
    const auto oldAssembly = hero.assembly; const auto oldViews = hero.views;
    hero.Capture(true);
    require(hero.invalidations == 1 && hero.assembly == oldAssembly && hero.views == oldViews,
            "unchanged role discarded preview caches");
    hero.Capture(false);
    require(hero.invalidations == 2 && !hero.assembly && !hero.views && !hero.role.Resolve(true),
            "role change reused hero assembly/pose");
    hero.Capture(true);
    require(hero.invalidations == 3 && hero.role.Resolve(false), "role re-selection failed");
    std::cout << "PASS unchanged-role reuse and hero/nonhero cache invalidation\n";

    // Ordinary armor/appearance invalidation does not reset actor identity.
    hero.assembly.reset(); hero.views.reset();
    require(hero.role.Resolve(false), "appearance refresh forgot source role");
    hero.role = {};
    require(!hero.role.Resolve(false) && hero.role.Resolve(true), "destroy/reset retained private role");
    Preview reopened; reopened.Capture(true);
    require(reopened.role.Resolve(false) && !companion.role.Resolve(false), "camp reopen leaked roles");
    std::cout << "PASS appearance refresh, destruction and independent camp reopen\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << "FAIL " << error.what() << '\n'; return 1;
}
