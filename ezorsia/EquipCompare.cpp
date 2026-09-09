#include "stdafx.h"
#include "EquipCompare.h"
#include <cstdio>
#include <cstring>

// 目标端装备比较 hook 的客户端地址（同基线客户端直接适用；来源 = 目标二进制 .data 指针表）
const DWORD dwCUIToolTip__ShowItem = 0x008F5B20; // __thiscall(this; 8 栈参: p1=x,p2=y,p3=item,p4..p7, p8=d)
const DWORD dwCUIToolTip__Clear    = 0x008E6E23; // __thiscall(this) CUIToolTip::ClearToolTip
const DWORD dwCUIToolTip__Ctor     = 0x008E49B5; // __thiscall(this) CUIToolTip ctor
const DWORD dwEquip_GetItemId      = 0x0042873D; // __thiscall(item+0xC)->int（取物品 id，客户端 0x008F5B20 008F5C26 证实）
const DWORD dwEquip_QuerySameSlots = 0x004606A0; // cdecl(itemId,2,out[8],1)->count(0..8)
const DWORD dwEquip_GetBySlot      = 0x004282F7; // __thiscall(装备栏实例, out8, tab, slot)

typedef void(__fastcall* ShowToolTip_t)(void* pThis, void* edx, int p1, int p2, int p3, int p4, int p5, int p6, int p7, int p8);
typedef void(__fastcall* CtorToolTip_t)(void* pThis, void* edx);
typedef void(__fastcall* ClearToolTip_t)(void* pThis, void* edx);
typedef int(__fastcall* GetItemId_t)(void* pThis, void* edx);
typedef int(__cdecl* QuerySameSlots_t)(int itemId, int flag, int* out, int n);
typedef void*(__fastcall* GetBySlot_t)(void* pThis, void* edx, void* out, int tab, int slot);

static ShowToolTip_t _Show = reinterpret_cast<ShowToolTip_t>(dwCUIToolTip__ShowItem);
static CtorToolTip_t _Create = reinterpret_cast<CtorToolTip_t>(dwCUIToolTip__Ctor);
static ClearToolTip_t _Clear = reinterpret_cast<ClearToolTip_t>(dwCUIToolTip__Clear);
static GetItemId_t _GetItemId = reinterpret_cast<GetItemId_t>(dwEquip_GetItemId);
static QuerySameSlots_t _QuerySame = reinterpret_cast<QuerySameSlots_t>(dwEquip_QuerySameSlots);
static GetBySlot_t _GetBySlot = reinterpret_cast<GetBySlot_t>(dwEquip_GetBySlot);

char EquipCompare::s_compareToolTip[0x800];
bool EquipCompare::s_enabled = true;
bool EquipCompare::s_created = false;
bool EquipCompare::s_visible = false;
bool EquipCompare::s_reentrancy = false;
int  EquipCompare::s_gap = 2;
void* EquipCompare::s_lastThis = nullptr;
int  EquipCompare::s_lastHoverId = 0;
int  EquipCompare::s_lastEquipId = 0;
int  EquipCompare::s_lastX = 0;
int  EquipCompare::s_lastY = 0;

void EquipCompare::Hook() {
	HookShow();
	HookClear();
}

// hook#1 0x008F5B20：显示悬停物品 tooltip；enabled 时在旁边补画同类别已穿戴装备对比
void EquipCompare::HookShow() {
	ShowToolTip_t Hook = [](void* t, void* edx, int p1, int p2, int p3, int p4, int p5, int p6, int p7, int p8) {
		const bool reentrant = s_reentrancy || (s_created && t == s_compareToolTip);
		if (!reentrant) {
			_Show(t, edx, p1, p2, p3, p4, p5, p6, p7, p8);
			if (s_enabled) {
				if (p3) {
					TryShowCompare(t, p1, p2, reinterpret_cast<void*>(p3), p8);
				}
				else {
					HideCompare();
				}
			}
			return;
		}
		_Show(t, edx, p1, p2, p3, p4, p5, p6, p7, p8);
	};
	Memory::SetHook(true, reinterpret_cast<void**>(&_Show), Hook);
}

// hook#2 0x008E6E23：隐藏 tooltip；悬停窗被清时一并清掉比较窗
void EquipCompare::HookClear() {
	ClearToolTip_t Hook = [](void* t, void* edx) {
		_Clear(t, edx);
		if (s_enabled && !s_reentrancy && (!s_created || t != s_compareToolTip)
			&& s_visible && t == s_lastThis) {
			HideCompare();
		}
	};
	Memory::SetHook(true, reinterpret_cast<void**>(&_Clear), Hook);
}

// 目标 FUN_10039bb0：比较显示主逻辑（含去重、右缘钳制）；整体 SEH 兜底防客户端调用异常闪退
void EquipCompare::TryShowCompare(void* pHoverTT, int x, int y, void* pItem, int d) {
	__try {
	const int hoveredId = GetItemId(pItem);
	if (hoveredId < 1000000 || hoveredId >= 2000000) { HideCompare(); return; }

	int slots[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
	const int count = QuerySameSlots(hoveredId, slots);
	if (count > 0) {
		for (int i = 0; i < count; ++i) {
			const int slot = slots[i];
			if (slot <= 0) continue;

			int negSlot = -slot;
			if (slot >= 100) negSlot = -slot - 100; // 现金装备槽启发式（v1 探针点，目标端走 COM "cash"）

			void* eq = GetEquipBySlot(negSlot);
			if (!eq) continue;
			if (eq == pItem) { ReleaseItem(eq); continue; }

			const int equipId = GetItemId(eq);
			const bool same = s_visible && s_lastThis == pHoverTT
				&& hoveredId == s_lastHoverId && equipId == s_lastEquipId
				&& x == s_lastX && y == s_lastY;
			if (!same) {
				EnsureWindow();
				if (s_created) {
					const int hoverW = GetWidth(pHoverTT);
					const int newX = s_gap + hoverW + x;

					s_reentrancy = true;
					DrawCompare(newX, y, eq, d);
					s_reentrancy = false;

					const int cmpW = GetWidth(s_compareToolTip);
					if (cmpW > 0 && Client::m_nGameWidth > 0 && Client::m_nGameWidth < cmpW + newX) {
						const int raw = x - s_gap - cmpW;
						const int clampX = raw > 0 ? raw : 0;
						if (clampX != newX) {
							s_reentrancy = true;
							ClearWindow();
							DrawCompare(clampX, y, eq, d);
							s_reentrancy = false;
						}
					}

					s_visible = true;
					s_lastThis = pHoverTT;
					s_lastHoverId = hoveredId;
					s_lastEquipId = equipId;
					s_lastX = x;
					s_lastY = y;
					ReleaseItem(eq);
					return;
				}
			}
			ReleaseItem(eq);
			return;
		}
	}
	HideCompare();
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		HideCompare();
	}
}

// 目标 FUN_100393e0
void EquipCompare::HideCompare() {
	if (s_created && s_visible) ClearWindow();
	s_visible = false;
	s_lastThis = nullptr;
	s_lastHoverId = 0;
	s_lastEquipId = 0;
	s_lastX = 0;
	s_lastY = 0;
}

// 目标 FUN_10038f60：惰性创建比较窗（CUIToolTip 实例）
void EquipCompare::EnsureWindow() {
	if (!s_created) {
		memset(s_compareToolTip, 0, sizeof(s_compareToolTip));
		_Create(s_compareToolTip, 0);
		s_created = true;
	}
}

// 目标 FUN_100397a0：经原函数清空比较窗（绕过自身 hook）
void EquipCompare::ClearWindow() {
	_Clear(s_compareToolTip, 0);
}

// 目标 FUN_10039ac0：用原函数以已穿戴物品再画一个 tooltip（ECX=比较窗；栈 8 参：x,y,item,0,0,0,0,d）
void EquipCompare::DrawCompare(int x, int y, void* pItem, int d) {
	_Show(s_compareToolTip, 0, x, y, reinterpret_cast<int>(pItem), 0, 0, 0, 0, d);
}

int EquipCompare::GetWidth(void* pTT) {
	if (!pTT) return 0;
	return *reinterpret_cast<int*>(reinterpret_cast<char*>(pTT) + 0xC);
}

int EquipCompare::GetItemId(void* pItem) {
	if (!pItem) return 0;
	// 客户端 0x008F5B20(008F5C26):lea ebx,[item+0Ch]; mov ecx,ebx; call 0042873D → this = item+0xC
	return _GetItemId(reinterpret_cast<char*>(pItem) + 0xC, 0);
}

int EquipCompare::QuerySameSlots(int itemId, int* out) {
	int n = _QuerySame(itemId, 2, out, 1);
	if (n < 0 || n > 8) n = 0;
	return n;
}

void* EquipCompare::GetEquipBySlot(int negSlot) {
	// 目标端 FUN_10039a40:instance = *(*(0x00BE7918)+0x20B8);0x004282F7 是 __thiscall(装备栏实例, out8, tab, slot)
	void* pObj = *reinterpret_cast<void**>(0x00BE7918);
	if (!pObj) return nullptr;
	void* inst = *reinterpret_cast<void**>(reinterpret_cast<char*>(pObj) + 0x20B8);
	if (!inst) return nullptr;
	void* out[2] = { nullptr, nullptr }; // 客户端把找到的物品指针写进 out+4
	_GetBySlot(inst, 0, out, 1, negSlot);
	return out[1];
}

void EquipCompare::ReleaseItem(void* pItem) {
	if (!pItem) return;
	InterlockedDecrement(reinterpret_cast<volatile long*>(reinterpret_cast<char*>(pItem) + 4));
}

void EquipCompare::SetEnabled(bool b) { s_enabled = b; if (!b) HideCompare(); }
void EquipCompare::SetGap(int n) { s_gap = n < 0 ? 0 : n; }
