#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <oleauto.h>
#include <cstring>
#include "WzUiBridge.hpp"
#pragma comment(lib, "oleaut32.lib")

namespace recovered { namespace wzui {
namespace {
// Raw target bytes: 6c dc 00 76 28 93 ff 4b 96 24 5b 0f 5c 01 17 9e.
const GUID IID_WzCanvas = {0x7600DC6C,0x9328,0x4BFF,{0x96,0x24,0x5B,0x0F,0x5C,0x01,0x17,0x9E}};
// Raw target bytes: 6d 04 ef 2b d6 cc 5a 44 88 c4 92 9f c3 5d 30 ac.
// GUID fields are little-endian in memory, so Data3 is 0x445A.
const GUID IID_WzFont = {0x2BEF046D,0xCCD6,0x445A,{0x88,0xC4,0x92,0x9F,0xC3,0x5D,0x30,0xAC}};
Binding& CurrentBinding() { static Binding binding; return binding; }
template<class Fn> Fn Slot(void* object, unsigned index) {
    return reinterpret_cast<Fn>((*reinterpret_cast<void***>(object))[index]);
}
using RefFn = ULONG (__stdcall*)(void*);
void Release(void* object) {
    if (!object) return;
    __try { Slot<RefFn>(object, 2)(object); }
    __except (EXCEPTION_EXECUTE_HANDLER) {}
}
VARIANT Missing() { VARIANT v; VariantInit(&v); v.vt = VT_ERROR; v.scode = DISP_E_PARAMNOTFOUND; return v; }
VARIANT Integer(long value) { VARIANT v; VariantInit(&v); v.vt = VT_I4; v.lVal = value; return v; }
bool ReadSlot(void** slot, void*& value) {
    if (!slot) return false;
    __try { value = *slot; return value != nullptr; }
    __except (EXCEPTION_EXECUTE_HANDLER) { value = nullptr; return false; }
}
FactoryFn Factory() {
    auto* slot = CurrentBinding().factorySlot;
    if (!slot) return nullptr;
    __try { return *slot; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
}
bool FactoryCreate(FactoryFn factory, const wchar_t* className, const GUID& iid, void** value) {
    __try { return SUCCEEDED(factory(className, &iid, value, nullptr)) && *value != nullptr; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool ResourceCreate(void* manager, BSTR className, void** value) {
    if (!manager || !className || !value) return false;
    __try {
        using Fn = HRESULT (__stdcall*)(void*, BSTR, void**);
        return SUCCEEDED(Slot<Fn>(manager, 6)(manager, className, value)) && *value != nullptr;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool CanvasCreate(void* canvas, int width, int height) {
    __try {
        using Fn = HRESULT (__stdcall*)(void*, int, int, VARIANT, VARIANT);
        return SUCCEEDED(Slot<Fn>(canvas, 11)(canvas, width, height, Missing(), Missing()));
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool FontCreate(void* font, BSTR face, int height, std::uint32_t color) {
    BSTR style = SysAllocString(L"B");
    if (!style) return false;
    VARIANT bold; VariantInit(&bold); bold.vt = VT_BSTR; bold.bstrVal = style;
    HRESULT result = E_FAIL;
    __try {
        using Fn = HRESULT (__stdcall*)(void*, BSTR, int, std::uint32_t, VARIANT);
        result = Slot<Fn>(font, 3)(font, face, height, color, bold);
    } __except (EXCEPTION_EXECUTE_HANDLER) { result = E_FAIL; }
    SysFreeString(style);
    return SUCCEEDED(result);
}
bool ResourceGetObject(void* manager, BSTR path, VARIANT* value) {
    __try {
        using Fn = HRESULT (__stdcall*)(void*, BSTR, VARIANT, VARIANT, VARIANT*);
        return SUCCEEDED(Slot<Fn>(manager, 7)(manager, path, Missing(), Missing(), value));
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool PropertyGetItem(void* property, BSTR name, VARIANT* value) {
    __try {
        using Fn = HRESULT (__stdcall*)(void*, BSTR, VARIANT*);
        return SUCCEEDED(Slot<Fn>(property, 5)(property, name, value));
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
IUnknown* UnknownFromVariant(VARIANT& value) {
    if (value.vt == VT_UNKNOWN) return value.punkVal;
    if (value.vt == VT_DISPATCH) return value.pdispVal;
    if (value.vt == (VT_UNKNOWN | VT_BYREF) && value.ppunkVal) return *value.ppunkVal;
    if (value.vt == (VT_DISPATCH | VT_BYREF) && value.ppdispVal) return *value.ppdispVal;
    return nullptr;
}
bool TryAddRef(IUnknown* unknown) {
    __try { unknown->AddRef(); return true; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
Object ObjectFromVariant(VARIANT& value) {
    Object result;
    IUnknown* unknown = UnknownFromVariant(value);
    if (!unknown) return result;
    if (TryAddRef(unknown)) result.Reset(unknown);
    return result;
}
bool QueryCanvas(IUnknown* unknown, void** canvas) {
    __try { return unknown && SUCCEEDED(unknown->QueryInterface(IID_WzCanvas, canvas)) && *canvas != nullptr; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool CanvasDimensions(void* canvas, int& width, int& height) {
    width = 0; height = 0;
    if (!canvas) return false;
    __try {
        using Fn = HRESULT (__stdcall*)(void*, int*);
        return SUCCEEDED(Slot<Fn>(canvas,16)(canvas,&width)) &&
               SUCCEEDED(Slot<Fn>(canvas,18)(canvas,&height)) && width > 0 && height > 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) { width = 0; height = 0; return false; }
}
bool CanvasProperty(void* canvas, void** property) {
    if (!canvas || !property) return false;
    __try {
        using Fn = HRESULT (__stdcall*)(void*, void**);
        return SUCCEEDED(Slot<Fn>(canvas,26)(canvas,property)) && *property != nullptr;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool QueryFont(IUnknown* unknown, void** font) {
    __try { return unknown && SUCCEEDED(unknown->QueryInterface(IID_WzFont, font)) && *font != nullptr; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool CanvasGetDrawingSurface(void* canvas, void** surface) {
    if (!canvas || !surface) return false;
    __try {
        using Fn = HRESULT (__stdcall*)(void*, VARIANT, void**);
        return SUCCEEDED(Slot<Fn>(canvas,64)(canvas,Integer(0),surface)) && *surface != nullptr;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool GraphicsCreateLayer(void* graphics, void* canvas, int x, int y, int width, int height, int z, void** layer) {
    VARIANT image; VariantInit(&image); image.vt = VT_UNKNOWN; image.punkVal = static_cast<IUnknown*>(canvas);
    __try {
        using Fn = HRESULT (__stdcall*)(void*, int, int, int, int, int, VARIANT, VARIANT, void**);
        return SUCCEEDED(Slot<Fn>(graphics, 25)(graphics, x, y, width, height, z, image, Missing(), layer)) && *layer != nullptr;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
}

void SetBinding(Binding binding) { CurrentBinding() = binding; }
Object::~Object() { Reset(); }
Object::Object(Object&& other) noexcept : value_(other.value_) { other.value_ = nullptr; }
Object& Object::operator=(Object&& other) noexcept {
    if (this != &other) { Reset(); value_ = other.value_; other.value_ = nullptr; }
    return *this;
}
void Object::Reset(void* value) { if (value_ != value) { Release(value_); value_ = value; } }
void** Object::Put() { Reset(); return &value_; }

Object CreateCanvas(int width, int height) {
    Object result; auto factory = Factory();
    if (!factory || width < 1 || height < 1) return result;
    if (!FactoryCreate(factory, L"Canvas", IID_WzCanvas, result.Put()) ||
        !CanvasCreate(result.Get(), width, height)) return {};
    return result;
}

Object CreateFont(const wchar_t* face, int height, std::uint32_t color) {
    Object result;
    if (!face || !*face || height < 1) return result;
    Object unknown;
    void* manager = nullptr;
    BSTR className = SysAllocString(L"Canvas#Font");
    if (className && ReadSlot(CurrentBinding().resourceManagerSlot, manager))
        ResourceCreate(manager, className, unknown.Put());
    if (className) SysFreeString(className);
    if (unknown) {
        void* font = nullptr;
        if (QueryFont(static_cast<IUnknown*>(unknown.Get()), &font)) result.Reset(font);
    }
    if (!result) {
        auto factory = Factory();
        if (!factory || !FactoryCreate(factory, L"Canvas#Font", IID_WzFont, result.Put())) return {};
    }
    BSTR name = SysAllocString(face); if (!name) return {};
    const bool created = FontCreate(result.Get(), name, height, color);
    SysFreeString(name);
    if (!created) return {};
    return result;
}

Object LoadObject(const wchar_t* path) {
    Object result; void* manager = nullptr;
    if (!path || !*path || !ReadSlot(CurrentBinding().resourceManagerSlot, manager)) return result;
    BSTR bstr = SysAllocString(path); if (!bstr) return result;
    VARIANT value; VariantInit(&value);
    const bool loaded = ResourceGetObject(manager, bstr, &value);
    SysFreeString(bstr);
    if (loaded) result = ObjectFromVariant(value);
    VariantClear(&value);
    return result;
}

Object LoadCanvas(const char* path) {
    Object canvas; void* manager = nullptr;
    if (!path || !*path || !ReadSlot(CurrentBinding().resourceManagerSlot, manager)) return canvas;
    const int n = MultiByteToWideChar(CP_ACP, 0, path, -1, nullptr, 0);
    if (n < 1) return canvas;
    std::wstring current(static_cast<std::size_t>(n), L'\0');
    MultiByteToWideChar(CP_ACP, 0, path, -1, &current[0], n);
    current.resize(static_cast<std::size_t>(n - 1));
    for (int depth = 0; depth < 8; ++depth) {
        BSTR bstr = SysAllocStringLen(current.data(), static_cast<UINT>(current.size()));
        if (!bstr) return {};
        VARIANT value; VariantInit(&value);
        const bool loaded = ResourceGetObject(manager, bstr, &value);
        SysFreeString(bstr);
        if (!loaded) { VariantClear(&value); return {}; }
        IUnknown* unknown = UnknownFromVariant(value);
        canvas.Reset();
        if (unknown) QueryCanvas(unknown, canvas.Put());
        VariantClear(&value);
        if (!canvas) return {};
        int width = 0, height = 0;
        if (CanvasDimensions(canvas.Get(), width, height)) return canvas;

        Object property;
        if (!CanvasProperty(canvas.Get(), property.Put())) return {};
        std::wstring link;
        if (GetString(property.Get(), L"_outlink", link) && !link.empty()) {
            current = link;
            continue;
        }
        if (GetString(property.Get(), L"_inlink", link) && !link.empty()) {
            const auto marker = current.find(L".img/");
            if (marker == std::wstring::npos) return {};
            current = current.substr(0, marker + 5) + link;
            continue;
        }
        return {};
    }
    return {};
}

Object GetChild(void* property, const wchar_t* name) {
    Object result;
    if (!property || !name || !*name) return result;
    BSTR key = SysAllocString(name); if (!key) return result;
    VARIANT value; VariantInit(&value);
    const bool loaded = PropertyGetItem(property, key, &value);
    SysFreeString(key);
    if (loaded) result = ObjectFromVariant(value);
    VariantClear(&value);
    return result;
}

bool GetString(void* property, const wchar_t* name, std::wstring& value) {
    value.clear();
    if (!property || !name || !*name) return false;
    BSTR key = SysAllocString(name); if (!key) return false;
    VARIANT raw; VariantInit(&raw);
    const bool loaded = PropertyGetItem(property, key, &raw);
    SysFreeString(key);
    if (!loaded) { VariantClear(&raw); return false; }
    VARIANT text; VariantInit(&text);
    const HRESULT hr = VariantChangeType(&text, &raw, 0, VT_BSTR);
    if (SUCCEEDED(hr) && text.bstrVal) value.assign(text.bstrVal, SysStringLen(text.bstrVal));
    VariantClear(&text); VariantClear(&raw);
    return SUCCEEDED(hr);
}

bool GetInteger(void* property, const wchar_t* name, int& value) {
    value = 0;
    if (!property || !name || !*name) return false;
    BSTR key = SysAllocString(name); if (!key) return false;
    VARIANT raw; VariantInit(&raw);
    const bool loaded = PropertyGetItem(property, key, &raw);
    SysFreeString(key);
    if (!loaded) { VariantClear(&raw); return false; }
    VARIANT number; VariantInit(&number);
    const HRESULT hr = VariantChangeType(&number, &raw, 0, VT_I4);
    if (SUCCEEDED(hr)) value = number.lVal;
    VariantClear(&number); VariantClear(&raw);
    return SUCCEEDED(hr);
}

bool GetCount(void* property, int& value) {
    value = 0;
    if (!property) return false;
    __try {
        using Fn = HRESULT (__stdcall*)(void*, int*);
        return SUCCEEDED(Slot<Fn>(property, 8)(property, &value)) && value >= 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) { value = 0; return false; }
}

Object CreateLayer(void* canvas, int x, int y, int width, int height, int z) {
    Object layer; void* graphics = nullptr;
    if (!canvas || !ReadSlot(CurrentBinding().graphicsSlot, graphics)) return layer;
    if (!GraphicsCreateLayer(graphics, canvas, x, y, width, height, z, layer.Put())) return {};
    if (!SetLayerSize(layer.Get(), width, height) ||
        !SetLayerColor(layer.Get(), 0xFFFFFFFF) || !SetLayerPosition(layer.Get(), x, y)) return {};
    return layer;
}

bool Fill(void* canvas, int x, int y, int width, int height, std::uint32_t color) {
    if (!canvas || width < 1 || height < 1) return false;
    __try { using Fn = HRESULT (__stdcall*)(void*,int,int,int,int,std::uint32_t);
        return SUCCEEDED(Slot<Fn>(canvas,35)(canvas,x,y,width,height,color)); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
Object GetDrawingCanvas(void* canvas) {
    Object result;
    if (!canvas) return result;
    if (!CanvasGetDrawingSurface(canvas, result.Put())) result.Reset();
    return result;
}
bool GetCanvasSize(void* canvas, int& width, int& height) {
    return CanvasDimensions(canvas, width, height);
}
bool DrawCanvas(void* canvas, int x, int y, void* image, int boxSize) {
    if (!canvas || !image || boxSize < 1) return false;
    int sourceWidth = 0, sourceHeight = 0;
    if (!GetCanvasSize(image, sourceWidth, sourceHeight)) return false;
    int width = sourceWidth, height = sourceHeight;
    if (width > boxSize || height > boxSize) {
        if (width < height) { width = width * boxSize / height; height = boxSize; }
        else { height = height * boxSize / width; width = boxSize; }
    }
    if (width < 1 || height < 1) return false;
    const int drawX = x + (boxSize - width) / 2;
    const int drawY = y + (boxSize - height) / 2;
    __try {
        using Fn = HRESULT (__stdcall*)(void*,int,int,void*,int,int,int,int,int,int,int,VARIANT);
        return SUCCEEDED(Slot<Fn>(canvas,33)(canvas,drawX,drawY,image,0xFF,width,height,
                                             0,0,sourceWidth,sourceHeight,Missing()));
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool DrawText(void* canvas, int x, int y, const wchar_t* text, void* font, std::uint32_t opacity) {
    if (!canvas || !font || !text) return false;
    BSTR value = SysAllocString(text); if (!value) return false;
    HRESULT hr = E_FAIL; void* ignored = nullptr;
    __try { using Fn = HRESULT (__stdcall*)(void*,int,int,BSTR,void*,VARIANT,VARIANT,void**);
        hr = Slot<Fn>(canvas,38)(canvas,x,y,value,font,Missing(),Missing(),&ignored); }
    __except (EXCEPTION_EXECUTE_HANDLER) { hr = E_FAIL; }
    SysFreeString(value); return SUCCEEDED(hr);
}
bool SetLayerPosition(void* layer, int x, int y) {
    if (!layer) return false;
    __try { using Fn = HRESULT (__stdcall*)(void*,int,int,VARIANT,VARIANT);
        return SUCCEEDED(Slot<Fn>(layer,36)(layer,x,y,Missing(),Missing())); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool SetLayerSize(void* layer, int width, int height) {
    if (!layer) return false;
    __try { using Fn = HRESULT (__stdcall*)(void*,int);
        return SUCCEEDED(Slot<Fn>(layer,47)(layer,width)) && SUCCEEDED(Slot<Fn>(layer,49)(layer,height)); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool SetLayerColor(void* layer, std::uint32_t color) {
    if (!layer) return false;
    __try { using Fn = HRESULT (__stdcall*)(void*,std::uint32_t);
        return SUCCEEDED(Slot<Fn>(layer,56)(layer,color)); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool SetLayerVisible(void* layer, bool visible) {
    if (!layer) return false;
    __try { using Fn = HRESULT (__stdcall*)(void*,int);
        return SUCCEEDED(Slot<Fn>(layer,71)(layer,visible ? 1 : 0)); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

} }
