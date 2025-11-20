
#include <Windows.h>

#include "healthbars.h"

HealthbarRenderer renderer;

typedef void (*DrawGUI)(bool someValue);

DrawGUI realDrawGUIFunc = (DrawGUI)0x6e6e40;

//先调用自己的绘图函数，再调用游戏本身的函数
void DrawGUIHook(bool someValue) {
    if (!someValue)
        renderer.Draw();
    realDrawGUIFunc(someValue);
}

void HookFunction() {
    // Hook into the game's rendering code.
    // This replaces the call to the function that is responsible for drawing the GUI with a call to DrawGUIHook().
    //
    //------------ 劫持操作 ------------------
    DWORD previous; //旧权限 便于恢复
    void* instrAddress = (void*)0x6e75a7; 
    VirtualProtect(instrAddress, 4, PAGE_READWRITE, &previous);//转变权限为可写 

    
    void* targetFunctionAddress = (void*)DrawGUIHook;
    int diff = ((int)targetFunctionAddress - (int)instrAddress - 5);//相对偏移量 = 目标函数地址 - (CALL指令的地址 + 5)
    *(unsigned int*)((int)instrAddress + 1) = diff;//写入
    // -----------------------------------------

    VirtualProtect(instrAddress, 4, previous, &previous);
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD  ul_reason_for_call, LPVOID lpReserved) {
    switch (ul_reason_for_call) {
    case DLL_PROCESS_ATTACH:
        HookFunction();
        break;
    case DLL_THREAD_ATTACH:
    case DLL_THREAD_DETACH:
    case DLL_PROCESS_DETACH:
        break;
    }
    return TRUE;
}

