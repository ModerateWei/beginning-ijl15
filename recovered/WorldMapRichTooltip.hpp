#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace recovered { namespace worldmap {

using SetHookFn = bool (*)(bool, void**, void*);

struct Settings {
    int originX = 3;
    int originY = 30;
    int hitRadius = 12;
    bool richTooltip = true;
    bool debugLog = false;
};

struct MapSpot {
    int x = 0;
    int y = 0;
    int mapId = 0;
};

struct Monster {
    int id = 0;
    int level = 0;
    int count = 0;
    std::wstring name;
};

struct Npc {
    int id = 0;
    std::wstring name;
};

struct MapDetails {
    int mapId = 0;
    std::wstring streetName;
    std::wstring mapName;
    std::vector<Monster> monsters;
    std::vector<Npc> npcs;
};

enum class LineKind { Street, Map, Section, Monster, Npc };
struct TooltipLine {
    LineKind kind = LineKind::Map;
    std::wstring text;
};

bool FindNearestSpot(const MapSpot* spots, std::size_t count, int mouseX, int mouseY,
                     const Settings& settings, MapSpot& result);
std::wstring FormatSectionHeader(const wchar_t* label, std::size_t total, std::size_t shown);
std::vector<TooltipLine> BuildTooltipLines(const MapDetails& details,
                                           std::size_t maximumPerSection = 10);

bool Install(const Settings& settings, SetHookFn setHook);
void Hide();
void Shutdown();

} }
