#include <windows.h>
#include <stdio.h>
#include <tlhelp32.h>
#include <string.h>

#ifndef BN_CLICKED
#define BN_CLICKED 0
#endif

#ifndef WM_COMMAND
#define WM_COMMAND 0x0111
#endif


typedef struct {
    HWND hwnd;
    char text[256];
    char className[256];
    RECT windowRect;    // Position relative to screen
    RECT clientRect;    // Size of client area
    POINT screenPos;    // Top-left corner in screen coordinates
    POINT clientPos;    // Position relative to parent window
    int width;
    int height;
    BOOL isVisible;
    BOOL isEnabled;
} ELEMENT_INFO;



// Shared memory structure
typedef struct {
    int elementCount;
    DWORD lastUpdate;
    HWND targetHwnd;
    ELEMENT_INFO elements[1000];
} SHARED_ELEMENT_DATA;

FILE* logFile = NULL;
ELEMENT_INFO* g_foundElements = NULL;
int g_elementCount = 0;
const int g_maxElements = 1000;

HANDLE hMapFile = NULL;
SHARED_ELEMENT_DATA* pSharedData = NULL;
const char* SHARED_MEMORY_NAME = "ElementFinderSharedMemory";

static int g_lastElementCount = -1;
static DWORD g_lastLogTime = 0;

BOOL InitializeSharedMemory() {
    hMapFile = CreateFileMappingA(
        INVALID_HANDLE_VALUE,
        NULL,
        PAGE_READWRITE,
        0,
        sizeof(SHARED_ELEMENT_DATA),
        SHARED_MEMORY_NAME
    );
    
    if (hMapFile == NULL) {
        if (logFile) {
            fprintf(logFile, "[ERROR] Could not create file mapping: %lu\n", GetLastError());
            fflush(logFile);
        }
        return FALSE;
    }
    
    pSharedData = (SHARED_ELEMENT_DATA*)MapViewOfFile(
        hMapFile,
        FILE_MAP_ALL_ACCESS,
        0,
        0,
        sizeof(SHARED_ELEMENT_DATA)
    );
    
    if (pSharedData == NULL) {
        if (logFile) {
            fprintf(logFile, "[ERROR] Could not map view of file: %lu\n", GetLastError());
            fflush(logFile);
        }
        CloseHandle(hMapFile);
        hMapFile = NULL;
        return FALSE;
    }
    
    pSharedData->elementCount = 0;
    pSharedData->lastUpdate = GetTickCount();
    pSharedData->targetHwnd = NULL;
    
    if (logFile) {
        fprintf(logFile, "[+] Shared memory initialized successfully\n");
        fflush(logFile);
    }
    
    return TRUE;
}

void UpdateSharedMemory() {
    if (!pSharedData) return;
    
    pSharedData->elementCount = g_elementCount;
    pSharedData->lastUpdate = GetTickCount();
    
    for (int i = 0; i < g_elementCount && i < 1000; i++) {
        memcpy(&pSharedData->elements[i], &g_foundElements[i], sizeof(ELEMENT_INFO));
    }
    
    // Only log shared memory updates when there are significant changes
    static int lastSharedCount = -1;
    if (lastSharedCount != g_elementCount && logFile) {
        fprintf(logFile, "[SHARED] Memory updated: %d elements\n", g_elementCount);
        fflush(logFile);
        lastSharedCount = g_elementCount;
    }
}

void FillElementInfo(HWND hwnd, ELEMENT_INFO* element) {
    char windowText[256] = {0};
    char className[256] = {0};
    
    GetWindowTextA(hwnd, windowText, sizeof(windowText));
    GetClassNameA(hwnd, className, sizeof(className));
    
    element->hwnd = hwnd;
    strcpy(element->text, windowText);
    strcpy(element->className, className);
    element->isVisible = IsWindowVisible(hwnd);
    element->isEnabled = IsWindowEnabled(hwnd);
    
    GetWindowRect(hwnd, &element->windowRect);
    element->screenPos.x = element->windowRect.left;
    element->screenPos.y = element->windowRect.top;
    element->width = element->windowRect.right - element->windowRect.left;
    element->height = element->windowRect.bottom - element->windowRect.top;
    
    GetClientRect(hwnd, &element->clientRect);
    
    HWND parent = GetParent(hwnd);
    if (parent) {
        POINT topLeft = {element->windowRect.left, element->windowRect.top};
        ScreenToClient(parent, &topLeft);
        element->clientPos = topLeft;
    } else {
        element->clientPos = element->screenPos;
    }
}

BOOL CALLBACK EnumChildElementsProc(HWND hwnd, LPARAM lParam) {
    if (g_elementCount >= g_maxElements) {
        return FALSE;
    }
    
    FillElementInfo(hwnd, &g_foundElements[g_elementCount]);
    g_elementCount++;
    
    return TRUE;
}

BOOL CALLBACK EnumWindowsProc(HWND hwnd, LPARAM lParam) {
    DWORD* targetPid = (DWORD*)lParam;
    DWORD windowProcessId;
    
    GetWindowThreadProcessId(hwnd, &windowProcessId);
    
    if (windowProcessId == *targetPid) {
        // Add the top-level window itself
        if (g_elementCount < g_maxElements) {
            FillElementInfo(hwnd, &g_foundElements[g_elementCount]);
            g_elementCount++;
        }
        // Enumerate all child windows
        EnumChildWindows(hwnd, EnumChildElementsProc, 0);
    }
    
    return TRUE;
}

// Scan all elements in the current process
void ScanAllElements() {
    if (!g_foundElements) {
        g_foundElements = (ELEMENT_INFO*)malloc(sizeof(ELEMENT_INFO) * g_maxElements);
        if (!g_foundElements) {
            if (logFile) {
                fprintf(logFile, "[ERROR] Failed to allocate memory for elements\n");
                fflush(logFile);
            }
            return;
        }
    }
    
    g_elementCount = 0;
    DWORD currentPid = GetCurrentProcessId();
    
    // Enumerate all windows belonging to this process
    EnumWindows(EnumWindowsProc, (LPARAM)&currentPid);
    
    if (logFile) {
        DWORD currentTime = GetTickCount();
        BOOL shouldLog = FALSE;
        
        // Only log if:
        // 1. Element count changed
        // 2. It's been more than 30 seconds since last detailed log
        // 3. This is the first scan
        if (g_lastElementCount != g_elementCount) {
            shouldLog = TRUE;
            fprintf(logFile, "[SCAN] Element count changed: %d -> %d\n", g_lastElementCount, g_elementCount);
        } else if (currentTime - g_lastLogTime > 30000 || g_lastLogTime == 0) {
            shouldLog = TRUE;
            fprintf(logFile, "[SCAN] Periodic check: %d elements active\n", g_elementCount);
        }
        
        if (shouldLog) {
            // Count and log only the important buttons
            int buttonCount = 0;
            for (int i = 0; i < g_elementCount; i++) {
                ELEMENT_INFO* elem = &g_foundElements[i];
                
                if (strstr(elem->className, "Button") != NULL && 
                    (strstr(elem->text, "Enable") != NULL || 
                     strstr(elem->text, "Disable") != NULL ||
                     strstr(elem->text, "Menu") != NULL ||
                     strstr(elem->text, "Open") != NULL)) {
                    buttonCount++;
                    
                    // Only log if this is a significant change or first scan
                    if (g_lastElementCount != g_elementCount || g_lastLogTime == 0) {
                        fprintf(logFile, "  [BTN] '%s' HWND:0x%p %s\n", 
                               elem->text, elem->hwnd, elem->isEnabled ? "Ready" : "Disabled");
                    }
                }
            }
            
            if (buttonCount > 0) {
                fprintf(logFile, "[INFO] %d control buttons available\n", buttonCount);
            }
            
            g_lastLogTime = currentTime;
        }
        
        g_lastElementCount = g_elementCount;
        fflush(logFile);
    }
    
    UpdateSharedMemory();
}

BOOL PerformBypassClick(HWND hwnd, int x, int y) {
    if (!hwnd || !IsWindow(hwnd)) {
        if (logFile) {
            fprintf(logFile, "[CLICK] Error: Invalid HWND\n");
            fflush(logFile);
        }
        return FALSE;
    }
    
    if (logFile) {
        char windowText[256] = {0};
        GetWindowTextA(hwnd, windowText, sizeof(windowText));
        fprintf(logFile, "[CLICK] Attempting: '%s' HWND:0x%p\n", windowText, hwnd);
        fflush(logFile);
    }
    
    // Use BN_CLICKED notification
    HWND parent = GetParent(hwnd);
    if (parent) {
        UINT ctrlId = GetDlgCtrlID(hwnd);
        LPARAM lParam = (LPARAM)hwnd;
        WPARAM wParam = MAKEWPARAM(ctrlId, BN_CLICKED);
        BOOL success = PostMessage(parent, WM_COMMAND, wParam, lParam);
        
        if (logFile) {
            fprintf(logFile, "[CLICK] Result: %s\n", success ? "SUCCESS" : "FAILED");
            fflush(logFile);
        }
        
        return success;
    } else {
        if (logFile) {
            fprintf(logFile, "[CLICK] Error: No parent window\n");
            fflush(logFile);
        }
        return FALSE;
    }
}

// Process click commands from shared memory
void ProcessClickCommands() {
    if (!pSharedData) return;
    
    // Check if there's a target HWND to click
    HWND targetHwnd = pSharedData->targetHwnd;
    if (targetHwnd) {
        PerformBypassClick(targetHwnd, 0, 0);
        pSharedData->targetHwnd = NULL;
    }
}

// Thread function to periodically scan for elements
DWORD WINAPI ScanThread(LPVOID lpParam) {
    Sleep(1000);
    ScanAllElements();
    
    // Periodic rescans and click command processing
    while (TRUE) {
        Sleep(500);
        ProcessClickCommands();
        
        static DWORD lastScan = 0;
        if (GetTickCount() - lastScan > 2000) {
            ScanAllElements();
            lastScan = GetTickCount();
        }
    }
    return 0;
}

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved) {
    switch (fdwReason) {
    case DLL_PROCESS_ATTACH: {
        char logFilePath[MAX_PATH];
        GetModuleFileNameA(hinstDLL, logFilePath, MAX_PATH);
        char* lastSlash = strrchr(logFilePath, '\\');
        if (lastSlash) {
            strcpy(lastSlash + 1, "loader.txt");
        } else {
            strcpy(logFilePath, "loader.txt");
        }
        logFile = fopen(logFilePath, "w");
        if (logFile) {
            fprintf(logFile, "[+] Element Finder DLL Loaded in PID: %lu\n", GetCurrentProcessId());
            fflush(logFile);
        }
    
        InitializeSharedMemory();
        HANDLE hThread = CreateThread(NULL, 0, ScanThread, NULL, 0, NULL);
        if (hThread) {
            CloseHandle(hThread);
        }
        break;
    }
        
    case DLL_PROCESS_DETACH:
        if (g_foundElements) {
            free(g_foundElements);
            g_foundElements = NULL;
        }
        
        if (pSharedData) {
            UnmapViewOfFile(pSharedData);
            pSharedData = NULL;
        }
        if (hMapFile) {
            CloseHandle(hMapFile);
            hMapFile = NULL;
        }
        
        if (logFile) {
            fprintf(logFile, "[-] Element Finder DLL Unloaded\n");
            fclose(logFile);
            logFile = NULL;
        }
        break;
    }
    return TRUE;
} 