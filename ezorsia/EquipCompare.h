#pragma once

// 装备悬停比较（EquipCompare）—— 移植自逆向目标端装备比较模块
// 触发：悬停装备物品时，在悬停 tooltip 旁并排画出同类别已穿戴装备的 tooltip 对比
// 参考（只读）：D:\AI\ijl15反编译（目标二进制 .data 指针表 + analysis/target/functions/*.c）
class EquipCompare
{
public:
	static void Hook();                 // 安装 hook（在 BossHP::Hook 之后调用）
	static void SetEnabled(bool b);     // [equipcompare] enabled
	static void SetGap(int n);          // [equipcompare] gap（<0 钳 0）
private:
	static void HookShow();             // 0x008F5B20 显示悬停物品 tooltip
	static void HookClear();            // 0x008E6E23 隐藏 tooltip
	static void TryShowCompare(void* pHoverTT, int x, int y, void* pItem, int d); // 目标 FUN_10039bb0
	static void HideCompare();          // 目标 FUN_100393e0
	static void EnsureWindow();         // 目标 FUN_10038f60
	static void ClearWindow();          // 目标 FUN_100397a0
	static void DrawCompare(int x, int y, void* pItem, int d); // 目标 FUN_10039ac0
	static int  GetWidth(void* pTT);    // *(int*)(pTT+0xC)
	static int  GetItemId(void* pItem); // 0x0042873D
	static int  QuerySameSlots(int itemId, int* out); // 0x004606A0
	static void* GetEquipBySlot(int negSlot);          // 0x004282F7
	static void ReleaseItem(void* pItem);              // 引用计数 -1（item+4）

	static char s_compareToolTip[0x800]; // 静态 CUIToolTip 实例
	static bool s_enabled;    // DAT_101138b8
	static bool s_created;    // DAT_101138ba
	static bool s_visible;    // DAT_101138bb
	static bool s_reentrancy; // DAT_101138bc
	static int  s_gap;        // DAT_1010fc50
	static void* s_lastThis;    // DAT_101140c0
	static int  s_lastHoverId;  // DAT_101140c4
	static int  s_lastEquipId;  // DAT_101140c8
	static int  s_lastX;        // DAT_101140cc
	static int  s_lastY;        // DAT_101140d0
};
