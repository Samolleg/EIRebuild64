#pragma once

// The caller owns the distinction: both paths deserialize a world save.
enum class WorldLoadOrigin { SavedGame, ZoneEntry };
