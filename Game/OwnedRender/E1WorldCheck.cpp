#include "E1World.h"
#include "embedded_sky.h"
#include <cstdio>
#include <limits>
#include <stdexcept>

int main() {
    int checks = 0;
    auto check = [&](bool value) { ++checks; if (!value) throw checks; };
    try {
        using namespace owned;
        using namespace OwnedRender;
        int owner = 0, foreign = 0;
        WorldEnvironment environment;
        double time = -1;
        check(!bridge_read_world_environment(environment));
        check(!bridge_current_world_time(time) && time == -1);
        check(bridge_current_world_epoch() == 0);
        check(!bridge_world_draw_completion_snapshot().valid);
        E1WorldFrame published;
        check(!E1ReadWorldFrame(published) && !published.camera.valid);
        E1WorldFrame input;
        input.environment.valid = true;
        input.environment.game_hours = 49.5;
        input.environment.hour = 1.5f;
        input.environment.world_time = 345.25;
        input.environment.region = 2;
        input.environment.weather_current = 1;
        input.environment.weather_next = 2;
        input.environment.rain_particles = .25f;
        input.lightingValid = true;
        input.lighting.ambient = {.1f, .2f, .3f, 1.f};
        input.lighting.sunlight_diffuse = {.8f, .6f, .4f, 1.f};
        input.lighting.sunlight_direction = {-.3f, .4f, .5f, 0.f};
        NativeWorldLighting lighting;
        check(!E1ReadWorldLighting(lighting));
        input.camera.valid = true;
        input.camera.world_to_clip = {1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16};
        input.fog.valid = input.fog.enabled = true;
        input.fog.failure = nullptr;
        input.fog.fog = {50,100,5.1f,2,3,4};
        E1BeginFrame();
        std::uint64_t epoch = 0, serial = 0;
        {
            E1WorldDraw draw(&owner, 5, input);
            epoch = bridge_current_world_epoch();
            check(epoch != 0);
            check(E1ReadWorldFrame(published) && published.camera.valid &&
                published.camera.world_to_clip == input.camera.world_to_clip &&
                published.fog.valid && published.fog.fog.projection_34 == 3 &&
                published.environment.world_time == input.environment.world_time &&
                published.lightingValid && published.lighting.sunlight_diffuse == input.lighting.sunlight_diffuse);
            check(E1ReadWorldLighting(lighting) && lighting.ambient == input.lighting.ambient &&
                lighting.sunlight_diffuse == input.lighting.sunlight_diffuse &&
                lighting.sunlight_direction == input.lighting.sunlight_direction);
            check(bridge_read_world_environment(environment));
            check(environment.game_hours == 49.5 && environment.hour == 1.5f &&
                environment.region == 2 && environment.weather_current == 1 && environment.weather_next == 2);
            check(bridge_current_world_time(time) && time == 345.25);
            auto camera = bridge_scene_world_to_clip_snapshot();
            serial = camera.scene_frame_serial;
            check(camera.valid && serial != 0 && camera.world_to_clip == input.camera.world_to_clip);
            auto fog = bridge_world_fog_snapshot();
            check(fog.valid && fog.enabled && fog.scene_frame_serial == serial && fog.environment.game_hours == 49.5);
            check(fog.fog.projection_33 == 2 && fog.fog.projection_34 == 3 && fog.fog.projection_43 == 4);
            check(!bridge_world_draw_completion_snapshot().valid && bridge_world_draw_completion_counter() == 0);
            E1RetireWorld(&foreign);
            check(bridge_current_world_epoch() == epoch && bridge_scene_world_to_clip_snapshot().valid);
            draw.Complete();
            check(!E1ReadWorldFrame(published) && !published.camera.valid);
            draw.Complete();
            check(bridge_world_draw_completion_counter() == 1);
        }
        auto complete = bridge_world_draw_completion_snapshot();
        check(complete.valid && complete.bridge_frame == serial && complete.completion_serial == 1);
        check(bridge_read_world_environment(environment)); // UI consumes the just-completed world.
        check(E1ReadWorldLighting(lighting));
        E1BeginFrame(); // A menu frame with no world must not consume the old seam.
        check(!E1ReadWorldLighting(lighting));
        check(!bridge_read_world_environment(environment));
        check(!bridge_scene_world_to_clip_snapshot().valid && !bridge_world_fog_snapshot().valid);
        check(!bridge_world_draw_completion_snapshot().valid && bridge_world_draw_completion_counter() == 1);
        try {
            E1WorldDraw draw(&owner, 5, input);
            check(bridge_current_world_epoch() == epoch);
            throw std::runtime_error("interrupted world draw");
        } catch (const std::runtime_error&) {}
        check(!E1ReadWorldLighting(lighting));
        check(!bridge_read_world_environment(environment));
        check(!bridge_world_draw_completion_snapshot().valid && bridge_world_draw_completion_counter() == 1);
        {
            E1WorldDraw draw(&owner, 6, input); // New zone / load of an earlier save.
            check(bridge_current_world_epoch() > epoch);
            epoch = bridge_current_world_epoch();
            E1RetireWorld(&owner);
            draw.Complete();
        }
        check(bridge_current_world_epoch() == 0 && !bridge_read_world_environment(environment));
        check(!bridge_world_draw_completion_snapshot().valid && bridge_world_draw_completion_counter() == 1);
        {
            E1WorldDraw draw(&owner, 5, input); // Reused source generation cannot reuse the retired epoch.
            check(bridge_current_world_epoch() > epoch);
            draw.Complete();
        }
        check(bridge_world_draw_completion_counter() == 2);
        E1BeginFrame();
        input.camera.world_to_clip[6] = std::numeric_limits<float>::quiet_NaN();
        input.environment.world_time = std::numeric_limits<double>::infinity();
        {
            E1WorldDraw draw(&owner, 5, input);
            check(!bridge_read_world_environment(environment));
            check(!bridge_scene_world_to_clip_snapshot().valid);
            check(!bridge_world_fog_snapshot().valid);
        }
        input.environment.world_time = 345.25;
        input.camera.world_to_clip[6] = 7;
        input.fog.fog.end = input.fog.fog.start;
        {
            E1WorldDraw draw(&owner, 5, input);
            check(!bridge_world_fog_snapshot().valid && bridge_world_fog_snapshot().failure != nullptr);
            check(bridge_read_world_environment(environment) && bridge_scene_world_to_clip_snapshot().valid);
        }
        input.fog.fog.end = 100;
        input.fog.fog.projection_34 = std::numeric_limits<float>::infinity();
        {
            E1WorldDraw draw(&owner, 5, input);
            check(!bridge_world_fog_snapshot().valid);
        }
        E1RetireWorld(&owner);
        check(!E1ReadWorldLighting(lighting));
        // Missing/invalid lighting may not silently use NativeWorldLighting defaults.
        const auto validLight = input.lighting;
        input.lightingValid = false;
        {
            E1WorldDraw draw(&owner, 7, input);
            check(!E1ReadWorldLighting(lighting));
        }
        input.lightingValid = true;
        for (int field = 0; field != 12; ++field) {
            input.lighting = validLight;
            auto& row = field < 4 ? input.lighting.ambient : field < 8 ?
                input.lighting.sunlight_diffuse : input.lighting.sunlight_direction;
            row[field % 4] = std::numeric_limits<float>::quiet_NaN();
            E1WorldDraw draw(&owner, 7, input);
            check(!E1ReadWorldLighting(lighting));
        }
        for (float value : {0.f, 1.e-8f, std::numeric_limits<float>::max()}) {
            input.lighting = validLight;
            input.lighting.sunlight_direction = {value, value, value, 0.f};
            E1WorldDraw draw(&owner, 7, input);
            check(!E1ReadWorldLighting(lighting));
        }
        input.lighting = validLight;
        {
            E1WorldDraw draw(&owner, 7, input);
            check(E1ReadWorldLighting(lighting));
            const auto held = lighting;
            input.lighting.ambient[0] = .9f;
            check(E1ReadWorldLighting(lighting) && lighting.ambient == held.ambient);
        }
        {
            E1WorldDraw draw(&owner, 8, input);
            check(E1ReadWorldLighting(lighting) && lighting.ambient[0] == .9f);
        }
        input.camera.valid = false;
        {
            E1WorldDraw draw(&owner, 8, input);
            check(!E1ReadWorldLighting(lighting));
        }
        input.camera.valid = true;
        // Real world adapter + embedded device lease. No window/device is opened.
        int renderer = 0, otherRenderer = 0;
        auto& sky = embedded_sky();
        check(!bridge_owned_sky_installed() && !E1ClaimSky(true));
        bridge_set_owned_sky(true); // Config alone must not suppress the stock draw.
        check(!bridge_owned_sky_installed() && !E1ClaimSky(true));
        check(!sky.attach(nullptr));
        check(sky.attach(&renderer));
        check(bridge_owned_sky_installed());
        check(!sky.available_to(&otherRenderer) && !sky.attach(&otherRenderer));
        sky.detach(&otherRenderer);
        check(bridge_owned_sky_installed());
        check(!E1ClaimSky(true)); // No current world.
        input.fog.fog.projection_34 = 3;
        E1BeginFrame();
        {
            E1WorldDraw draw(&owner, 8, input);
            check(!bridge_owned_sky_requested());
            check(!E1ClaimSky(false) && !bridge_owned_sky_requested());
            check(E1ClaimSky(true) && bridge_owned_sky_requested());
            const auto revision = sky.revision();
            check(sky.attach(&renderer) && sky.revision() == revision);
            bridge_set_owned_sky(true);
            check(bridge_owned_sky_requested()); // Idempotent frame settings.
            bridge_set_owned_sky(false);
            check(!bridge_owned_sky_requested() && !E1ClaimSky(true));
            bridge_set_owned_sky(true);
            check(!bridge_owned_sky_requested()); // No revival of an old request.
            check(E1ClaimSky(true));
            sky.detach(&renderer);
            check(!bridge_owned_sky_installed() && !bridge_owned_sky_requested());
            check(sky.attach(&renderer));
            bridge_set_owned_sky(true);
            check(!bridge_owned_sky_requested()); // Same address, new device lease.
            check(E1ClaimSky(true));
            draw.Complete();
        }
        check(bridge_owned_sky_requested()); // Composition after world completion.
        E1BeginFrame();
        check(!bridge_owned_sky_requested());
        {
            E1WorldDraw draw(&owner, 8, input);
            check(E1ClaimSky(true));
        } // Aborted world cannot leave an owned sky request.
        check(!bridge_owned_sky_requested());
        {
            E1WorldDraw draw(&owner, 9, input);
            check(E1ClaimSky(true));
            E1RetireWorld(&owner);
            check(!bridge_owned_sky_requested());
            draw.Complete();
        }
        input.camera.valid = false;
        {
            E1WorldDraw draw(&owner, 10, input);
            check(!E1ClaimSky(true));
        }
        sky.detach(&renderer);
        check(!bridge_owned_sky_installed() && !bridge_owned_sky_requested());
        std::printf("E1 world frame: %d checks PASS\n", checks);
        return 0;
    } catch (int failed) { std::printf("E1 world frame: check %d FAILED\n", failed); return 1; }
}
