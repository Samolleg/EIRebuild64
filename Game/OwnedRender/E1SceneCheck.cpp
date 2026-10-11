#include "E1Scene.h"
#include <cstdio>
#include <cstdlib>

namespace { unsigned checks = 0;
void check(bool value, const char* label) {
    ++checks;
    if (!value) { std::fprintf(stderr, "FAIL: %s\n", label); std::exit(1); }
}
}
namespace owned {
// Console diagnostic sink; no game, device, or bridge implementation is mocked.
void bridge_append_probe_event(const char*, const char*) noexcept {}
}
int main() {
    check(&OwnedRender::E1Scene().Scene() == &owned::retained_visual_scene(),
        "host and resource runtime use the same production scene owner");
    owned::RetainedStaticScene scene;
    OwnedRender::E1SceneMembership host(scene);
    int worldA = 0, worldB = 0;
    auto count = [&] { return host.Scene().visual_frame(0).diagnostic.live_members; };
    check(!host.Added(&worldA, 1, 7), "unbound source rejected");
    host.Begin(&worldA, 1);
    check(host.Added(&worldA, 1, 7), "successful insertion");
    const auto first = host.Identity(7);
    check(first.id == 7 && first.generation && first.positive_world_epoch, "renderer identity issued");
    check(count() == 1, "real retained scene sees membership without mesh or visibility");
    check(host.Scene().visual_frame(0).entities.empty(), "membership alone invents no geometry");
    host.Begin(&worldA, 1);
    check(host.Added(&worldA, 1, 7) && host.Identity(7).generation == first.generation,
        "duplicate and same-world begin preserve incarnation");
    check(count() == 1, "duplicate does not duplicate live membership");
    host.Added(&worldA, 1, 8);
    host.Added(&worldA, 1, 0); host.Added(&worldA, 1, -7);
    check(count() == 2 && !host.Identity(-7).generation && !host.Identity(0).generation,
        "nonpositive IDs stay outside c56 world membership domain");
    host.Removed(&worldB, 1, 7); host.Removed(&worldA, 2, 7);
    check(count() == 2, "foreign and stale removal ignored");
    host.Removed(&worldA, 1, 999);
    check(count() == 2, "unsuccessful removal ignored");
    host.Removed(&worldA, 1, 7);
    check(count() == 1 && !host.Identity(7).generation, "successful removal reaches real scene");
    host.Added(&worldA, 1, 7);
    check(count() == 2 && host.Identity(7).generation > first.generation, "ID reuse gets fresh incarnation");
    const auto unaffected = host.Identity(8);
    check(unaffected.generation != host.Identity(7).generation, "independent member unchanged");
    host.ResetPositive(&worldB, 1);
    check(count() == 2, "foreign reset ignored");
    host.ResetPositive(&worldA, 1);
    check(count() == 0 && !host.Identity(8).generation, "bulk positive reset reaches renderer");
    host.Added(&worldA, 1, 8);
    check(host.Identity(8).positive_world_epoch > unaffected.positive_world_epoch &&
        host.Identity(8).generation > unaffected.generation, "bulk reset changes both identity domains");
    const auto oldFrame = host.Scene().visual_frame_value(0);
    const auto oldResource = host.Scene().visual_begin_resource(8);
    check(oldResource.serial != 0, "resource publication has separate lifetime");
    host.Begin(&worldA, 2);
    check(count() == 0 && !host.Scene().visual_resource(8).serial, "zone change retires membership and resource");
    check(!host.Scene().visual_finish_resource(oldResource, true), "late resource completion cannot revive old world");
    check(oldFrame->diagnostic.live_members == 1, "previous immutable frame survives retirement");
    check(!host.Added(&worldA, 1, 9), "old generation cannot repopulate new scene");
    host.Added(&worldA, 2, 9);
    host.Retire(&worldA, 1);
    check(count() == 1, "stale destructor cannot retire new zone");
    host.Begin(&worldB, 1);
    check(count() == 0, "owner replacement resets scene");
    host.Added(&worldB, 1, 9);
    host.Retire(&worldA, 2);
    check(count() == 1, "old owner cannot retire replacement");
    host.Retire(&worldB, 1);
    check(count() == 0 && !host.Added(&worldB, 1, 9), "retired scene rejects publication");
    host.Begin(&worldA, 3);
    int objectA = 0, objectB = 0;
    check(!host.BeginResource(&worldB, &objectA, 21).serial,
        "server or foreign world cannot publish a client resource");
    check(!host.BeginResource(&worldA, &objectA, -21).serial,
        "UI resource stays outside positive world domain");
    owned::VisualResourceIncarnation createdResource{};
    {
        OwnedRender::E1ResourceCreation creation(host, &worldA, &objectA, 21);
        createdResource = host.Resource(&objectA);
        check(createdResource.serial && count() == 0,
            "resource begins before membership without fabricating membership");
        creation.Complete(true);
    }
    check(host.Resource(&objectA).serial == createdResource.serial,
        "successful virtual creation survives scope exit");
    host.Added(&worldA, 3, 21);
    host.Removed(&worldA, 3, 21);
    check(host.Resource(&objectA).serial == createdResource.serial,
        "streamed membership removal does not destroy resource");
    host.Added(&worldA, 3, 21);
    host.DestroyResource(&objectA);
    check(!scene.visual_resource(21).serial && count() == 1,
        "resource destruction does not remove independent membership");
    host.DestroyResource(&objectA);
    check(count() == 1, "repeated destruction is harmless");
    {
        OwnedRender::E1ResourceCreation creation(host, &worldA, &objectA, 22);
        creation.Complete(false);
    }
    check(!host.Resource(&objectA).serial && !scene.visual_resource(22).serial,
        "failed virtual Create retires construction");
    try {
        OwnedRender::E1ResourceCreation creation(host, &worldA, &objectA, 23);
        throw 1;
    } catch (int) {}
    check(!host.Resource(&objectA).serial && !scene.visual_resource(23).serial,
        "unwound virtual Create retires construction");
    const auto staleResource = host.BeginResource(&worldA, &objectA, 24);
    const auto replacement = host.BeginResource(&worldA, &objectB, 24);
    check(replacement.serial != staleResource.serial && !host.Resource(&objectA).serial,
        "resource ID reuse rejects old owner lookup");
    host.DestroyResource(&objectA);
    check(host.Resource(&objectB).serial == replacement.serial,
        "late old-object destructor preserves replacement ID");
    check(!host.FinishResource(staleResource, false) && host.Resource(&objectB).serial,
        "late failed completion preserves replacement");
    host.ResetPositive(&worldA, 3);
    check(host.Resource(&objectB).serial == replacement.serial,
        "positive membership reset preserves resources");
    {
        OwnedRender::E1ResourceCreation late(host, &worldA, &objectA, 25);
        host.Begin(&worldA, 4);
        const auto next = host.BeginResource(&worldA, &objectB, 25);
        late.Complete(true);
        check(host.Resource(&objectB).serial == next.serial,
            "old-world completion cannot finish or replace new-world resource");
    }
    host.Retire(&worldA, 4);
    check(!host.Resource(&objectB).serial && !scene.visual_resource(25).serial,
        "world retirement clears resource owners and scene");
    std::printf("E1SceneCheck: %u checks PASS\n", checks);
}
