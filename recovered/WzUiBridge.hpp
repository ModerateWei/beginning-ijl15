#pragma once
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <oleauto.h>
#include <cstdint>
#include <string>

#if !defined(_M_IX86)
#error The recovered WZ UI bridge requires the x86 client ABI.
#endif

namespace recovered { namespace wzui {

using FactoryFn = HRESULT (__cdecl*)(const wchar_t* className, const GUID* iid,
                                      void** object, IUnknown* outer);
struct Binding {
    FactoryFn* factorySlot = reinterpret_cast<FactoryFn*>(0x00BF0CC0);
    void** resourceManagerSlot = reinterpret_cast<void**>(0x00BF14E8);
    void** graphicsSlot = reinterpret_cast<void**>(0x00BF14EC);
};
void SetBinding(Binding binding);

class Object {
public:
    Object() = default;
    explicit Object(void* value) : value_(value) {}
    ~Object();
    Object(const Object&) = delete;
    Object& operator=(const Object&) = delete;
    Object(Object&& other) noexcept;
    Object& operator=(Object&& other) noexcept;
    void* Get() const { return value_; }
    void Reset(void* value = nullptr);
    void** Put();
    explicit operator bool() const { return value_ != nullptr; }
private:
    void* value_ = nullptr;
};

Object CreateCanvas(int width, int height);
Object CreateFont(const wchar_t* face, int height, std::uint32_t color);
Object LoadObject(const wchar_t* path);
Object LoadCanvas(const char* path);
Object GetChild(void* property, const wchar_t* name);
bool GetString(void* property, const wchar_t* name, std::wstring& value);
bool GetInteger(void* property, const wchar_t* name, int& value);
bool GetCount(void* property, int& value);
Object CreateLayer(void* canvas, int x, int y, int width, int height, int z);
bool Fill(void* canvas, int x, int y, int width, int height, std::uint32_t color);
Object GetDrawingCanvas(void* canvas);
bool GetCanvasSize(void* canvas, int& width, int& height);
bool DrawCanvas(void* canvas, int x, int y, void* image, int boxSize);
bool DrawText(void* canvas, int x, int y, const wchar_t* text, void* font,
              std::uint32_t opacity = 0xB4);
bool SetLayerPosition(void* layer, int x, int y);
bool SetLayerSize(void* layer, int width, int height);
bool SetLayerColor(void* layer, std::uint32_t color);
bool SetLayerVisible(void* layer, bool visible);

} }
