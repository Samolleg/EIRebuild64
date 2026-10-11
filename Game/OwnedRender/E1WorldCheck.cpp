#include "E1World.h"
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
        E1WorldFrame input;
        input.environment.valid = true;
        input.environment.game_hours = 49.5;
        input.environment.hour = 1.5f;
        input.environment.world_time = 345.25;
        input.environment.region = 2;
        input.environment.weather_current = 1;
        input.environment.weather_next = 2;
        input.environment.rain_particles = .25f;
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
            draw.Complete();
            check(bridge_world_draw_completion_counter() == 1);
        }
        auto complete = bridge_world_draw_completion_snapshot();
        check(complete.valid && complete.bridge_frame == serial && complete.completion_serial == 1);
        check(bridge_read_world_environment(environment)); // UI consumes the just-completed world.
        E1BeginFrame(); // A menu frame with no world must not consume the old seam.
        check(!bridge_read_world_environment(environment));
        check(!bridge_scene_world_to_clip_snapshot().valid && !bridge_world_fog_snapshot().valid);
        check(!bridge_world_draw_completion_snapshot().valid && bridge_world_draw_completion_counter() == 1);
        try {
            E1WorldDraw draw(&owner, 5, input);
            check(bridge_current_world_epoch() == epoch);
            throw std::runtime_error("interrupted world draw");
        } catch (const std::runtime_error&) {}
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
        E1RetireWorld(&owner);
        std::printf("E1 world frame: %d checks PASS\n", checks);
        return 0;
    } catch (int failed) { std::printf("E1 world frame: check %d FAILED\n", failed); return 1; }
}
