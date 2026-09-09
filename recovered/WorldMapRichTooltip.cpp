#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <algorithm>
#include <array>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <string>
#include <unordered_map>
#include <utility>
#include "WorldMapRichTooltip.hpp"
#include "WzUiBridge.hpp"
#pragma comment(lib, "user32.lib")

#if !defined(_M_IX86)
#error The recovered world-map hook requires the x86 client ABI.
#endif

namespace recovered { namespace worldmap {
namespace {
using MouseFn = int (__thiscall*)(void*, int, int);
using CloseFn = void (__cdecl*)();
using FocusFn = void (__thiscall*)(void*, void*);

MouseFn originalMouse = reinterpret_cast<MouseFn>(0x009EE2B3);
CloseFn originalClose = reinterpret_cast<CloseFn>(0x009EB94A);
FocusFn originalFocus = reinterpret_cast<FocusFn>(0x009E3264);
Settings currentSettings;
bool enabled = false;
int currentMapId = 0;
int layerWidth = 0, layerHeight = 0;
wzui::Object stringMapRoot, stringMobRoot, stringNpcRoot;
wzui::Object streetFont, mapFont, sectionFont, monsterFont, npcFont, shadowFont, layer;
std::unordered_map<int, wzui::Object> mapIcons, monsterIcons, npcIcons;
alignas(16) unsigned char nativeTooltip[1304]{};
bool nativeTooltipCreated = false;
std::string nativeTooltipText;
const std::string emptyTooltipTitle;
int nativeTooltipWidth = 220;
bool nativeRichTooltip = false;
bool nativeCustomTooltip = false;
MapDetails displayedDetails;
bool hasDisplayedDetails = false;

bool EnsureFonts();
bool DrawExactTooltip(const MapDetails& details);

constexpr std::uintptr_t TooltipCtorAddress = 0x008E49B5;
constexpr std::uintptr_t TooltipClearAddress = 0x008E6E23;
constexpr std::uintptr_t TooltipSetStringAddress = 0x008E6E7D;
constexpr std::uintptr_t TooltipSetRichAddress = 0x008E7150;
using TooltipCtorFn = void (__fastcall*)(void*, void*);
using TooltipClearFn = void (__fastcall*)(void*, void*);
using TooltipSetStringFn = void (__fastcall*)(void*, void*, int, int, const char*);
using TooltipSetRichFn = void (__fastcall*)(void*, void*, int, int, char*, char*,
                                            int, int, int, int, int, int);

void Log(const char* format, ...) {
    if (!currentSettings.debugLog || !format) return;
    wchar_t path[MAX_PATH]{};
    if (!GetModuleFileNameW(nullptr, path, _countof(path))) return;
    wchar_t* slash = wcsrchr(path, L'\\');
    if (!slash) return;
    wcscpy_s(slash + 1, _countof(path) - static_cast<std::size_t>(slash + 1 - path),
             L"worldmap.log");
    FILE* file = nullptr;
    if (_wfopen_s(&file, path, L"a") != 0 || !file) return;
    SYSTEMTIME now{};
    GetLocalTime(&now);
    std::fprintf(file, "[%02u:%02u:%02u.%03u] ", now.wHour, now.wMinute,
                 now.wSecond, now.wMilliseconds);
    va_list arguments;
    va_start(arguments, format);
    std::vfprintf(file, format, arguments);
    va_end(arguments);
    std::fputc('\n', file);
    std::fclose(file);
}

constexpr std::array<const wchar_t*, 14> Regions = {
    L"maple", L"victoria", L"ossyria", L"elin", L"weddingGL", L"MasteriaGL",
    L"HalloweenGL", L"jp", L"etc", L"singapore", L"event", L"Episode1GL",
    L"china", L"thai"
};

bool ValidMapId(int mapId) { return mapId >= 100000 && mapId < 1000100000; }

bool EnsureRoots() {
    if (!stringMapRoot) stringMapRoot = wzui::LoadObject(L"String/Map.img");
    if (!stringMobRoot) stringMobRoot = wzui::LoadObject(L"String/Mob.img");
    if (!stringNpcRoot) stringNpcRoot = wzui::LoadObject(L"String/Npc.img");
    return static_cast<bool>(stringMapRoot);
}

std::wstring NumberKey(int value) {
    wchar_t text[24]{};
    _snwprintf_s(text, _countof(text), _TRUNCATE, L"%d", value);
    return text;
}

std::wstring ReadName(void* root, int id) {
    if (!root) return {};
    const auto key = NumberKey(id);
    auto entry = wzui::GetChild(root, key.c_str());
    std::wstring name;
    if (entry) wzui::GetString(entry.Get(), L"name", name);
    return name;
}

void LoadMapNames(int mapId, MapDetails& details) {
    if (!EnsureRoots()) return;
    const auto key = NumberKey(mapId);
    for (const auto* region : Regions) {
        auto group = wzui::GetChild(stringMapRoot.Get(), region);
        if (!group) continue;
        auto entry = wzui::GetChild(group.Get(), key.c_str());
        if (!entry) continue;
        wzui::GetString(entry.Get(), L"streetName", details.streetName);
        wzui::GetString(entry.Get(), L"mapName", details.mapName);
        return;
    }
}

void LoadLife(int mapId, MapDetails& details) {
    wchar_t path[96]{};
    const int group = mapId / 100000000;
    _snwprintf_s(path, _countof(path), _TRUNCATE, L"Map/Map/Map%d/%09d.img", group, mapId);
    auto map = wzui::LoadObject(path);
    auto life = map ? wzui::GetChild(map.Get(), L"life") : wzui::Object{};
    int count = 0;
    if (!life || !wzui::GetCount(life.Get(), count)) return;
    count = std::min(count, 4096);

    std::unordered_map<int, std::size_t> monsterIndex;
    std::unordered_map<int, std::size_t> npcIndex;
    for (int index = 0; index < count; ++index) {
        const auto key = NumberKey(index);
        auto entry = wzui::GetChild(life.Get(), key.c_str());
        if (!entry) continue;
        std::wstring type;
        int id = 0;
        if (!wzui::GetString(entry.Get(), L"type", type) ||
            !wzui::GetInteger(entry.Get(), L"id", id) || id < 1) continue;
        if (type == L"m") {
            const auto found = monsterIndex.find(id);
            if (found != monsterIndex.end()) {
                ++details.monsters[found->second].count;
                continue;
            }
            Monster monster; monster.id = id; monster.count = 1;
            monster.name = ReadName(stringMobRoot.Get(), id);
            wchar_t mobPath[64]{};
            _snwprintf_s(mobPath, _countof(mobPath), _TRUNCATE, L"Mob/%07d.img", id);
            auto mob = wzui::LoadObject(mobPath);
            auto info = mob ? wzui::GetChild(mob.Get(), L"info") : wzui::Object{};
            if (info) wzui::GetInteger(info.Get(), L"level", monster.level);
            monsterIndex[id] = details.monsters.size();
            details.monsters.push_back(std::move(monster));
        } else if (type == L"n" && npcIndex.find(id) == npcIndex.end()) {
            Npc npc; npc.id = id; npc.name = ReadName(stringNpcRoot.Get(), id);
            npcIndex[id] = details.npcs.size();
            details.npcs.push_back(std::move(npc));
        }
    }
    std::sort(details.monsters.begin(), details.monsters.end(),
        [](const Monster& a, const Monster& b) {
            if (a.level != b.level) return a.level < b.level;
            return a.id < b.id;
        });
    std::sort(details.npcs.begin(), details.npcs.end(),
        [](const Npc& a, const Npc& b) { return a.id < b.id; });
}

MapDetails LoadDetails(int mapId) {
    MapDetails details; details.mapId = mapId;
    LoadMapNames(mapId, details);
    LoadLife(mapId, details);
    return details;
}

int TextWidth(const std::wstring& text) {
    int width = 0;
    for (wchar_t c : text) width += c < 0x80 ? 7 : 14;
    return width;
}

std::string WideToAnsi(const std::wstring& text) {
    if (text.empty()) return {};
    const int length = WideCharToMultiByte(CP_ACP, 0, text.data(),
                                           static_cast<int>(text.size()),
                                           nullptr, 0, nullptr, nullptr);
    if (length < 1) return {};
    std::string result(static_cast<std::size_t>(length), '\0');
    if (WideCharToMultiByte(CP_ACP, 0, text.data(), static_cast<int>(text.size()),
                            result.data(), length, nullptr, nullptr) != length) return {};
    return result;
}

bool EnsureNativeTooltip() {
    if (nativeTooltipCreated) return true;
    __try {
        reinterpret_cast<TooltipCtorFn>(TooltipCtorAddress)(nativeTooltip, nullptr);
        nativeTooltipCreated = true;
        Log("native tooltip created at %p", nativeTooltip);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        Log("native tooltip constructor raised exception 0x%08lx", GetExceptionCode());
        return false;
    }
}

char* AllocateTooltipString(const std::string& text) {
    const std::size_t length = text.size();
    const std::size_t capacity = std::max<std::size_t>(141, length);
    if (capacity > 0x7FFFFFF0u) return nullptr;
    auto* block = static_cast<unsigned char*>(
        HeapAlloc(GetProcessHeap(), 0, capacity + 17));
    if (!block) return nullptr;
    auto* header = reinterpret_cast<std::uint32_t*>(block);
    header[0] = static_cast<std::uint32_t>(capacity + 13);
    header[1] = 1;
    header[2] = static_cast<std::uint32_t>(capacity);
    header[3] = static_cast<std::uint32_t>(length);
    char* data = reinterpret_cast<char*>(block + 16);
    if (length) std::memcpy(data, text.data(), length);
    data[length] = '\0';
    return data;
}

void FreeTooltipString(char* text) {
    if (text) HeapFree(GetProcessHeap(), 0, text - 16);
}

bool ShowRichTooltipSurface(int x, int y, const std::string& bodyText, int width) {
    if (!nativeTooltipCreated) return false;
    __try {
        reinterpret_cast<TooltipClearFn>(TooltipClearAddress)(nativeTooltip, nullptr);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        Log("rich tooltip pre-clear raised exception 0x%08lx", GetExceptionCode());
        return false;
    }
    char* title = AllocateTooltipString(emptyTooltipTitle);
    char* body = AllocateTooltipString(bodyText);
    if (!title || !body) {
        FreeTooltipString(title);
        FreeTooltipString(body);
        Log("rich tooltip string allocation failed");
        return false;
    }
    __try {
        // The client consumes both reference-counted strings, matching the original DLL.
        reinterpret_cast<TooltipSetRichFn>(TooltipSetRichAddress)(
            nativeTooltip, nullptr, x, y, title, body, 0, 0, 0,
            width, 1, 0);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        Log("rich tooltip draw raised exception 0x%08lx", GetExceptionCode());
        return false;
    }
}

bool ReadTooltipSurface(void*& canvas, int& width, int& height) {
    canvas = nullptr; width = 0; height = 0;
    __try {
        height = *reinterpret_cast<int*>(nativeTooltip + 0x08);
        width = *reinterpret_cast<int*>(nativeTooltip + 0x0C);
        canvas = *reinterpret_cast<void**>(nativeTooltip + 0x10);
        return canvas && width > 0 && width <= 1024 && height > 0 && height <= 2048;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        canvas = nullptr; width = 0; height = 0; return false;
    }
}

bool PositionRichTooltip(int x, int y) {
    return ShowRichTooltipSurface(x, y, nativeTooltipText, nativeTooltipWidth);
}

bool PositionNativeTooltip() {
    if (!nativeTooltipCreated) return false;
    if (nativeCustomTooltip && hasDisplayedDetails)
        return DrawExactTooltip(displayedDetails);
    if (nativeTooltipText.empty()) return false;
    POINT cursor{};
    if (!GetCursorPos(&cursor)) return false;
    HWND window = GetForegroundWindow();
    if (window) ScreenToClient(window, &cursor);
    if (nativeRichTooltip && PositionRichTooltip(cursor.x + 18, cursor.y + 18)) return true;
    nativeRichTooltip = false;
    __try {
        reinterpret_cast<TooltipSetStringFn>(TooltipSetStringAddress)(
            nativeTooltip, nullptr, cursor.x + 18, cursor.y + 18, nativeTooltipText.c_str());
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        Log("native tooltip draw raised exception 0x%08lx", GetExceptionCode());
        return false;
    }
}

bool DrawNativeTooltip(const MapDetails& details) {
    if (!EnsureNativeTooltip()) return false;
    const auto lines = BuildTooltipLines(details);
    std::wstring combined;
    for (const auto& line : lines) {
        if (!combined.empty()) combined += L"\r\n";
        combined += line.text;
    }
    nativeTooltipText = WideToAnsi(combined);
    if (nativeTooltipText.empty()) {
        Log("native tooltip text conversion failed for map %d", details.mapId);
        return false;
    }
    nativeTooltipWidth = 220;
    for (const auto& line : lines)
        nativeTooltipWidth = std::max(nativeTooltipWidth, TextWidth(line.text) + 40);
    nativeTooltipWidth = std::min(nativeTooltipWidth, 420);
    nativeRichTooltip = true;
    const bool shown = PositionNativeTooltip();
    Log("native rich tooltip map=%d lines=%zu bytes=%zu width=%d shown=%d", details.mapId,
        lines.size(), nativeTooltipText.size(), nativeTooltipWidth, shown ? 1 : 0);
    return shown;
}

bool EnsureFonts() {
    if (!streetFont) streetFont = wzui::CreateFont(L"Dotum", 12, 0xFFFFD45A);
    if (!mapFont) mapFont = wzui::CreateFont(L"Dotum", 13, 0xFF6EDB7E);
    if (!sectionFont) sectionFont = wzui::CreateFont(L"Dotum", 12, 0xFFFF6A6A);
    if (!monsterFont) monsterFont = wzui::CreateFont(L"Dotum", 12, 0xFF6FAFFF);
    if (!npcFont) npcFont = wzui::CreateFont(L"Dotum", 12, 0xFFFFFFFF);
    if (!shadowFont) shadowFont = wzui::CreateFont(L"Dotum", 12, 0xFF000000);
    return streetFont && mapFont && sectionFont && monsterFont && npcFont && shadowFont;
}

void* FontFor(LineKind kind) {
    if (kind == LineKind::Street) return streetFont.Get();
    if (kind == LineKind::Map) return mapFont.Get();
    if (kind == LineKind::Section) return sectionFont.Get();
    if (kind == LineKind::Monster) return monsterFont.Get();
    return npcFont.Get();
}

void* MapIcon(int mapId) {
    const auto found = mapIcons.find(mapId);
    if (found != mapIcons.end()) return found->second.Get();
    wchar_t mapPath[96]{};
    _snwprintf_s(mapPath, _countof(mapPath), _TRUNCATE, L"Map/Map/Map%d/%09d.img",
                 mapId / 100000000, mapId);
    auto map = wzui::LoadObject(mapPath);
    auto info = map ? wzui::GetChild(map.Get(), L"info") : wzui::Object{};
    std::wstring mark;
    if (info) wzui::GetString(info.Get(), L"mapMark", mark);
    const auto markAnsi = WideToAnsi(mark);
    char iconPath[192]{};
    if (!markAnsi.empty())
        sprintf_s(iconPath, "Map/MapHelper.img/mark/%s", markAnsi.c_str());
    auto icon = iconPath[0] ? wzui::LoadCanvas(iconPath) : wzui::Object{};
    Log("map icon map=%d mark=%s loaded=%d", mapId, markAnsi.c_str(), icon ? 1 : 0);
    auto inserted = mapIcons.emplace(mapId, std::move(icon));
    return inserted.first->second.Get();
}

void* MonsterIcon(int id) {
    const auto found = monsterIcons.find(id);
    if (found != monsterIcons.end()) return found->second.Get();
    const char* formats[] = {"Mob/%07d.img/stand/0", "Mob/%07d.img/fly/0",
                             "Mob/%08d.img/stand/0", "Mob/%08d.img/fly/0"};
    wzui::Object icon;
    char path[96]{};
    for (const char* format : formats) {
        sprintf_s(path, format, id);
        icon = wzui::LoadCanvas(path);
        if (icon) break;
    }
    Log("monster icon id=%d path=%s loaded=%d", id, path, icon ? 1 : 0);
    auto inserted = monsterIcons.emplace(id, std::move(icon));
    return inserted.first->second.Get();
}

void* NpcIcon(int id) {
    const auto found = npcIcons.find(id);
    if (found != npcIcons.end()) return found->second.Get();
    const char* formats[] = {"Npc/%07d.img/stand/0", "Npc/%08d.img/stand/0"};
    wzui::Object icon;
    char path[96]{};
    for (const char* format : formats) {
        sprintf_s(path, format, id);
        icon = wzui::LoadCanvas(path);
        if (icon) break;
    }
    Log("npc icon id=%d path=%s loaded=%d", id, path, icon ? 1 : 0);
    auto inserted = npcIcons.emplace(id, std::move(icon));
    return inserted.first->second.Get();
}

bool DrawShadowText(void* canvas, int x, int y, const std::wstring& text, void* font) {
    if (!canvas || text.empty() || !font) return false;
    wzui::DrawText(canvas, x + 1, y + 1, text.c_str(), shadowFont.Get());
    return wzui::DrawText(canvas, x, y, text.c_str(), font);
}

bool CreateExactSurface(int x, int y, int wantedWidth, int wantedHeight,
                        void*& canvas, int& actualWidth, int& actualHeight) {
    static int lineStep = 0;
    static int baseHeight = 0;
    if (lineStep < 1) {
        if (!ShowRichTooltipSurface(x, y, std::string(2, '\n'), 240) ||
            !ReadTooltipSurface(canvas, actualWidth, actualHeight)) return false;
        const int heightAtTwo = actualHeight;
        if (!ShowRichTooltipSurface(x, y, std::string(12, '\n'), 240) ||
            !ReadTooltipSurface(canvas, actualWidth, actualHeight)) return false;
        lineStep = heightAtTwo < actualHeight ? std::max(1, (actualHeight - heightAtTwo) / 10) : 14;
        baseHeight = heightAtTwo - lineStep * 2;
        Log("custom surface calibration base=%d step=%d h2=%d h12=%d",
            baseHeight, lineStep, heightAtTwo, actualHeight);
    }
    int lines = std::max(1, (wantedHeight - baseHeight + lineStep - 1) / lineStep);
    for (int retry = 0; retry < 5; ++retry, ++lines) {
        if (!ShowRichTooltipSurface(x, y, std::string(static_cast<std::size_t>(lines), '\n'),
                                    wantedWidth) ||
            !ReadTooltipSurface(canvas, actualWidth, actualHeight)) return false;
        if (actualHeight >= wantedHeight) return true;
    }
    return false;
}

bool DrawExactTooltip(const MapDetails& details) {
    if (!EnsureNativeTooltip() || !EnsureFonts()) return false;
    const std::size_t monsterCount = std::min<std::size_t>(10, details.monsters.size());
    const std::size_t npcCount = std::min<std::size_t>(10, details.npcs.size());
    int wantedWidth = 220;
    const std::wstring street = details.streetName.empty() ? L"未知区域" : details.streetName;
    const std::wstring mapName = details.mapName.empty()
        ? L"地图 " + std::to_wstring(details.mapId) : details.mapName;
    wantedWidth = std::max(wantedWidth, TextWidth(street) + 54);
    wantedWidth = std::max(wantedWidth, TextWidth(mapName) + 54);
    for (std::size_t i = 0; i < monsterCount; ++i) {
        const auto& monster = details.monsters[i];
        std::wstring name = monster.name.empty() ? std::to_wstring(monster.id) : monster.name;
        std::wstring row = L"[Lv." + std::to_wstring(monster.level) + L"] " + name;
        if (monster.count > 1) row += L" x" + std::to_wstring(monster.count);
        wantedWidth = std::max(wantedWidth, TextWidth(row) + 56);
    }
    for (std::size_t i = 0; i < npcCount; ++i) {
        const auto& npc = details.npcs[i];
        wantedWidth = std::max(wantedWidth,
            TextWidth(npc.name.empty() ? std::to_wstring(npc.id) : npc.name) + 56);
    }
    wantedWidth = std::min(wantedWidth, 420);
    int wantedHeight = monsterCount ? static_cast<int>(monsterCount) * 34 + 94 : 74;
    if (npcCount) wantedHeight += static_cast<int>(npcCount) * 34 + 20;

    POINT cursor{};
    if (!GetCursorPos(&cursor)) return false;
    HWND window = GetForegroundWindow();
    if (window) ScreenToClient(window, &cursor);
    void* tooltipCanvas = nullptr;
    int width = 0, height = 0;
    if (!CreateExactSurface(cursor.x + 18, cursor.y + 18, wantedWidth, wantedHeight,
                            tooltipCanvas, width, height)) return false;
    // Match the target DLL's vtable +0x100 surface acquisition before drawing.
    auto drawingCanvas = wzui::GetDrawingCanvas(tooltipCanvas);
    if (!drawingCanvas) {
        Log("target-style drawing surface unavailable for map %d", details.mapId);
        return false;
    }
    void* canvas = drawingCanvas.Get();
    if (!wzui::Fill(canvas, 2, 2, std::max(1, width - 4), std::max(1, height - 4),
                    0xFF141A24)) return false;
    wzui::Fill(canvas, 4, 4, std::max(1, width - 8), 20, 0xC0423010);
    DrawShadowText(canvas, 10, 9, street, streetFont.Get());
    if (void* icon = MapIcon(details.mapId)) wzui::DrawCanvas(canvas, 10, 30, icon, 28);
    DrawShadowText(canvas, 44, 36, mapName, mapFont.Get());
    wzui::Fill(canvas, 10, 66, std::max(1, width - 20), 1, 0xC0E0C070);

    int y = 70;
    if (monsterCount) {
        DrawShadowText(canvas, 10, y,
            FormatSectionHeader(L"怪物", details.monsters.size(), monsterCount), sectionFont.Get());
        y = 90;
        for (std::size_t i = 0; i < monsterCount; ++i) {
            const auto& monster = details.monsters[i];
            if (void* icon = MonsterIcon(monster.id)) wzui::DrawCanvas(canvas, 10, y, icon, 28);
            std::wstring name = monster.name.empty() ? std::to_wstring(monster.id) : monster.name;
            std::wstring row = L"[Lv." + std::to_wstring(monster.level) + L"] " + name;
            if (monster.count > 1) row += L" x" + std::to_wstring(monster.count);
            DrawShadowText(canvas, 46, y + 11, row, npcFont.Get());
            y += 34;
        }
    }
    if (npcCount) {
        DrawShadowText(canvas, 10, y,
            FormatSectionHeader(L"NPC", details.npcs.size(), npcCount), monsterFont.Get());
        y += 20;
        for (std::size_t i = 0; i < npcCount; ++i) {
            const auto& npc = details.npcs[i];
            if (void* icon = NpcIcon(npc.id)) wzui::DrawCanvas(canvas, 10, y, icon, 28);
            DrawShadowText(canvas, 46, y + 11,
                npc.name.empty() ? std::to_wstring(npc.id) : npc.name, npcFont.Get());
            y += 34;
        }
    }
    nativeCustomTooltip = true;
    Log("target-style tooltip map=%d requested=%dx%d actual=%dx%d mobs=%zu npcs=%zu",
        details.mapId, wantedWidth, wantedHeight, width, height, monsterCount, npcCount);
    return true;
}

void PositionLayer() {
    if (!layer || layerWidth < 1 || layerHeight < 1) return;
    POINT cursor{};
    GetCursorPos(&cursor);
    HWND window = GetForegroundWindow();
    if (window) ScreenToClient(window, &cursor);
    RECT client{0, 0, 640, 480};
    if (window) GetClientRect(window, &client);
    const int x = std::max(8L, std::min(cursor.x + 18L, client.right - layerWidth - 8L));
    const int y = std::max(8L, std::min(cursor.y + 18L, client.bottom - layerHeight - 8L));
    wzui::SetLayerPosition(layer.Get(), x, y);
}

bool DrawTooltip(const MapDetails& details) {
    displayedDetails = details;
    hasDisplayedDetails = true;
    nativeCustomTooltip = false;
    if (DrawExactTooltip(details)) return true;
    Log("target-style custom draw unavailable for map %d; using native rich fallback",
        details.mapId);
    if (DrawNativeTooltip(details)) return true;
    Log("native tooltip unavailable; trying Canvas/Font/Layer fallback for map %d",
        details.mapId);
    if (!EnsureFonts()) return false;
    const auto lines = BuildTooltipLines(details);
    int width = 220;
    for (const auto& line : lines) width = std::max(width, TextWidth(line.text) + 24);
    width = std::min(width, 420);
    const int height = std::max(66, 14 + static_cast<int>(lines.size()) * 18);
    auto canvas = wzui::CreateCanvas(width, height);
    if (!canvas) return false;
    wzui::Fill(canvas.Get(), 0, 0, width, height, 0xFF141A24);
    wzui::Fill(canvas.Get(), 2, 2, std::max(0, width - 4), 20, 0xC0423010);
    int y = 7;
    for (const auto& line : lines) {
        wzui::DrawText(canvas.Get(), 10, y, line.text.c_str(), FontFor(line.kind));
        y += 18;
    }
    auto replacement = wzui::CreateLayer(canvas.Get(), 8, 8, width, height, 1000);
    if (!replacement) return false;
    wzui::SetLayerVisible(replacement.Get(), true);
    layer = std::move(replacement);
    layerWidth = width; layerHeight = height; PositionLayer();
    return true;
}

bool HasTooltip() {
    return nativeCustomTooltip || (nativeTooltipCreated && !nativeTooltipText.empty()) ||
           static_cast<bool>(layer);
}

bool FindSpots(void* self, std::vector<MapSpot>& spots) {
    static constexpr int bases[] = {-4, 0, 4};
    __try {
        for (int base : bases) {
            auto* payload = *reinterpret_cast<unsigned char**>(
                static_cast<unsigned char*>(self) + base + 0x5B8);
            if (!payload) continue;
            const int count = *reinterpret_cast<int*>(payload - 4);
            if (count < 1 || count > 256) continue;
            spots.reserve(static_cast<std::size_t>(count));
            for (int i = 0; i < count; ++i) {
                auto* record = payload + i * 0x44;
                auto* mapIdPointer = *reinterpret_cast<int**>(record + 0x2C);
                if (!mapIdPointer) continue;
                const int mapId = *mapIdPointer;
                if (ValidMapId(mapId))
                    spots.push_back({*reinterpret_cast<int*>(record),
                                     *reinterpret_cast<int*>(record + 4), mapId});
            }
            if (!spots.empty()) return true;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) { spots.clear(); }
    return false;
}

int __fastcall MouseHook(void* self, void*, int mouseX, int mouseY) {
    const int result = originalMouse(self, mouseX, mouseY);
    static unsigned int mouseCalls = 0;
    ++mouseCalls;
    if (mouseCalls <= 8)
        Log("mouse hook call=%u this=%p x=%d y=%d", mouseCalls, self, mouseX, mouseY);
    if (!enabled || !self) { Hide(); return result; }
    std::vector<MapSpot> spots;
    MapSpot hit;
    if (!FindSpots(self, spots)) {
        if (mouseCalls <= 8) Log("no hotspot array found at this+{-4,0,4}+0x5B8");
        Hide(); return result;
    }
    if (!FindNearestSpot(spots.data(), spots.size(), mouseX, mouseY, currentSettings, hit)) {
        if (mouseCalls <= 8) Log("hotspot array count=%zu; cursor did not hit radius=%d",
                                 spots.size(), currentSettings.hitRadius);
        Hide(); return result;
    }
    if (hit.mapId != currentMapId || !HasTooltip()) {
        currentMapId = hit.mapId;
        Log("hotspot hit map=%d position=(%d,%d) count=%zu", hit.mapId, hit.x, hit.y,
            spots.size());
        if (currentSettings.richTooltip && !DrawTooltip(LoadDetails(hit.mapId)))
            Log("all tooltip render paths failed for map %d", hit.mapId);
    } else if (!PositionNativeTooltip()) {
        PositionLayer();
    }
    return result;
}

void __cdecl CloseHook() {
    Log("world map close hook");
    Hide();
    originalClose();
    currentMapId = 0;
}

void __fastcall FocusHook(void* self, void*, void* focus) {
    originalFocus(self, focus);
    if (!focus) Hide();
}
}

bool FindNearestSpot(const MapSpot* spots, std::size_t count, int mouseX, int mouseY,
                     const Settings& settings, MapSpot& result) {
    if (!spots || !count) return false;
    const int radius = settings.hitRadius >= 1 && settings.hitRadius <= 64
        ? settings.hitRadius : 12;
    const long long maximum = static_cast<long long>(radius) * radius;
    long long nearest = maximum + 1;
    bool found = false;
    for (std::size_t i = 0; i < count; ++i) {
        if (!ValidMapId(spots[i].mapId)) continue;
        const long long dx = spots[i].x - (mouseX - settings.originX);
        const long long dy = spots[i].y - (mouseY - settings.originY);
        const long long distance = dx * dx + dy * dy;
        if (distance <= maximum && distance < nearest) {
            nearest = distance; result = spots[i]; found = true;
        }
    }
    return found;
}

std::wstring FormatSectionHeader(const wchar_t* label, std::size_t total, std::size_t shown) {
    std::wstring result = label ? label : L"";
    result += L" (" + std::to_wstring(total) + L")";
    if (shown < total) result += L"，仅列出前 " + std::to_wstring(shown) + L" 个";
    result += L"：";
    return result;
}

std::vector<TooltipLine> BuildTooltipLines(const MapDetails& details,
                                           std::size_t maximumPerSection) {
    std::vector<TooltipLine> lines;
    if (!details.streetName.empty()) lines.push_back({LineKind::Street, details.streetName});
    if (!details.mapName.empty()) lines.push_back({LineKind::Map, details.mapName});
    if (lines.empty())
        lines.push_back({LineKind::Map, L"未知地图 (" + std::to_wstring(details.mapId) + L")"});
    const auto monsterCount = std::min(maximumPerSection, details.monsters.size());
    if (!details.monsters.empty()) {
        lines.push_back({LineKind::Section,
            FormatSectionHeader(L"怪物", details.monsters.size(), monsterCount)});
        for (std::size_t i = 0; i < monsterCount; ++i) {
            const auto& monster = details.monsters[i];
            std::wstring name = monster.name.empty() ? std::to_wstring(monster.id) : monster.name;
            lines.push_back({LineKind::Monster, L"  [Lv." + std::to_wstring(monster.level) +
                L"] " + name + L" x" + std::to_wstring(monster.count)});
        }
    }
    const auto npcCount = std::min(maximumPerSection, details.npcs.size());
    if (!details.npcs.empty()) {
        lines.push_back({LineKind::Section,
            FormatSectionHeader(L"NPC", details.npcs.size(), npcCount)});
        for (std::size_t i = 0; i < npcCount; ++i) {
            const auto& npc = details.npcs[i];
            lines.push_back({LineKind::Npc, L"  " +
                (npc.name.empty() ? std::to_wstring(npc.id) : npc.name)});
        }
    }
    return lines;
}

bool Install(const Settings& settings, SetHookFn setHook) {
    currentSettings = settings;
    if (currentSettings.hitRadius < 1 || currentSettings.hitRadius > 64)
        currentSettings.hitRadius = 12;
    enabled = currentSettings.richTooltip;
    Log("install begin enabled=%d origin=(%d,%d) radius=%d", enabled ? 1 : 0,
        currentSettings.originX, currentSettings.originY, currentSettings.hitRadius);
    if (!enabled) { Log("install skipped because richTooltip=false"); return true; }
    if (!setHook) { Log("install failed: SetHook is null"); return false; }
    const bool mouse = setHook(true, reinterpret_cast<void**>(&originalMouse),
                               reinterpret_cast<void*>(MouseHook));
    const bool close = setHook(true, reinterpret_cast<void**>(&originalClose),
                               reinterpret_cast<void*>(CloseHook));
    const bool focus = setHook(true, reinterpret_cast<void**>(&originalFocus),
                               reinterpret_cast<void*>(FocusHook));
    Log("install hooks mouse=%d close=%d focus=%d", mouse ? 1 : 0, close ? 1 : 0,
        focus ? 1 : 0);
    return mouse && close && focus;
}

void Hide() {
    if (nativeTooltipCreated) {
        __try {
            reinterpret_cast<TooltipClearFn>(TooltipClearAddress)(nativeTooltip, nullptr);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            Log("native tooltip clear raised exception 0x%08lx", GetExceptionCode());
        }
        nativeTooltipText.clear();
    }
    nativeRichTooltip = false;
    nativeCustomTooltip = false;
    hasDisplayedDetails = false;
    if (layer) wzui::SetLayerVisible(layer.Get(), false);
}

void Shutdown() {
    layer.Reset(); streetFont.Reset(); mapFont.Reset(); sectionFont.Reset();
    monsterFont.Reset(); npcFont.Reset(); shadowFont.Reset();
    mapIcons.clear(); monsterIcons.clear(); npcIcons.clear();
    stringMapRoot.Reset(); stringMobRoot.Reset(); stringNpcRoot.Reset();
    nativeTooltipText.clear();
    nativeTooltipCreated = false; nativeRichTooltip = false;
    nativeCustomTooltip = false; hasDisplayedDetails = false;
    enabled = false; currentMapId = 0; layerWidth = 0; layerHeight = 0;
}

} }
