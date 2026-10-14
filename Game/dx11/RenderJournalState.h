#pragma once
#include "../GameJournal.h"
#include "../OwnedRender/ei_renderer_api.h"
#include <cstring>

// Journal only final source-host states, on the game/renderer owner thread.
// This retains copied diagnostics, never game objects or GPU resources.
class RenderJournalState {
    EIRenderStatusV2 previous = {};
    bool known = false;
public:
    static bool Valid(const EIRenderStatusV2& status) noexcept {
        if (status.size != sizeof(status) || status.version != EI_RENDERER_STATUS_VERSION_V2 ||
            status.ground_cover_drawn > 1 || status.feature_count > EI_RENDERER_STATUS_ROWS_V2 ||
            !std::memchr(status.ground_cover_reason, 0, sizeof(status.ground_cover_reason))) return false;
        for (uint32_t i = 0; i < status.feature_count; ++i) {
            const auto& row = status.features[i];
            if (row.enabled > 1 || row.active > 1 || !std::memchr(row.name, 0, sizeof(row.name)) ||
                !std::memchr(row.reason, 0, sizeof(row.reason)) ||
                !std::memchr(row.param, 0, sizeof(row.param)) || !std::memchr(row.value, 0, sizeof(row.value))) return false;
        }
        return true;
    }
    void Publish(const EIRenderStatusV2& status) noexcept {
        if (!known || previous.ground_cover_drawn != status.ground_cover_drawn ||
            std::strcmp(previous.ground_cover_reason, status.ground_cover_reason) ||
            previous.grid_x != status.grid_x || previous.grid_y != status.grid_y ||
            previous.atlas_ready != status.atlas_ready || previous.atlas_total != status.atlas_total) {
            GameJournal::Event("RENDER", "ground_cover", "state=%s reason=%s grid=%ux%u atlas=%u/%u",
                status.ground_cover_drawn ? "drawn" : "skipped", status.ground_cover_reason,
                status.grid_x, status.grid_y, status.atlas_ready, status.atlas_total);
        }
        for (uint32_t i = 0; i < status.feature_count; ++i) {
            const auto& row = status.features[i];
            if (!known || i >= previous.feature_count || std::strcmp(row.name, previous.features[i].name) ||
                row.enabled != previous.features[i].enabled || row.active != previous.features[i].active ||
                std::strcmp(row.reason, previous.features[i].reason) ||
                std::strcmp(row.param, previous.features[i].param) || std::strcmp(row.value, previous.features[i].value)) {
                if (row.param[0])
                    GameJournal::Event("RENDER", "feature", "name=%s param=%s value=%s",
                        row.name, row.param, row.value);
                else
                    GameJournal::Event("RENDER", "feature", "name=%s enabled=%u active=%u reason=%s",
                        row.name, row.enabled, row.active, row.reason);
            }
        }
        previous = status;
        known = true;
    }
};
