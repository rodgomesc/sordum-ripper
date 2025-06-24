#include <windows.h>
#include <stdio.h>
#include <tlhelp32.h>
#include <string.h>

const char* TARGET_PROCESS_NAME = "dControl.exe";

// Element info structure (must match hook.c)
typedef struct {
    HWND hwnd;
    char text[256];
    char className[256];
    RECT windowRect;
    RECT clientRect;
    POINT screenPos;
    POINT clientPos;
    int width;
    int height;
    BOOL isVisible;
    BOOL isEnabled;
} ELEMENT_INFO;

// Shared memory structure (must match hook.c)
typedef struct {
    int elementCount;
    DWORD lastUpdate;
    HWND targetHwnd;
    ELEMENT_INFO elements[1000];
} SHARED_ELEMENT_DATA;

const char* SHARED_MEMORY_NAME = "ElementFinderSharedMemory";

DWORD findProcessId(const char* processName) {
    PROCESSENTRY32 processEntry;
    processEntry.dwSize = sizeof(PROCESSENTRY32);
    HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);

    if (hSnapshot == INVALID_HANDLE_VALUE) return 0;

    if (Process32First(hSnapshot, &processEntry)) {
        do {
            if (_stricmp(processEntry.szExeFile, processName) == 0) {
                CloseHandle(hSnapshot);
                return processEntry.th32ProcessID;
            }
        } while (Process32Next(hSnapshot, &processEntry));
    }

    CloseHandle(hSnapshot);
    return 0;
}

void DisplayElement(ELEMENT_INFO* elem, int index) {
    printf("[%d] '%s' (Class: %s) HWND: 0x%p\n", 
           index, elem->text, elem->className, elem->hwnd);
    printf("    Screen: (%ld, %ld) Size: %dx%d\n", 
           elem->screenPos.x, elem->screenPos.y, elem->width, elem->height);
    printf("    Status: %s%s\n", 
           elem->isVisible ? "Visible " : "Hidden ",
           elem->isEnabled ? "Enabled" : "Disabled");
    
    // Show center coordinates for clickable elements
    if (strstr(elem->className, "Button") != NULL || 
        strstr(elem->text, "Enable") != NULL || 
        strstr(elem->text, "Disable") != NULL ||
        strstr(elem->text, "Menu") != NULL ||
        strstr(elem->text, "Open") != NULL) {
        
        int centerX = elem->screenPos.x + elem->width / 2;
        int centerY = elem->screenPos.y + elem->height / 2;
        printf("    >>> CLICK CENTER: (%d, %d)\n", centerX, centerY);
    }
    printf("\n");
}

void TestElementFinderWithSharedMemory() {
    printf("\n=== ELEMENT FINDER RESULTS ===\n");
    
    HANDLE hMapFile = OpenFileMappingA(FILE_MAP_READ, FALSE, SHARED_MEMORY_NAME);
    if (hMapFile == NULL) {
        printf("Could not access shared memory: %lu\n", GetLastError());
        printf("Make sure the DLL is loaded in the target process.\n");
        return;
    }
    
    SHARED_ELEMENT_DATA* pSharedData = (SHARED_ELEMENT_DATA*)MapViewOfFile(
        hMapFile, FILE_MAP_READ, 0, 0, sizeof(SHARED_ELEMENT_DATA));
    
    if (pSharedData == NULL) {
        printf("Could not map shared memory: %lu\n", GetLastError());
        CloseHandle(hMapFile);
        return;
        }
        
    printf("Connected to shared memory successfully!\n");
    printf("Data age: %lu ms\n", GetTickCount() - pSharedData->lastUpdate);
    
    int totalElements = pSharedData->elementCount;
    printf("Total elements found: %d\n\n", totalElements);
    
    if (totalElements == 0) {
        printf("No elements found yet. Waiting 3 seconds...\n");
        Sleep(3000);
        totalElements = pSharedData->elementCount;
        printf("After waiting: %d elements\n\n", totalElements);
}

    // Show all buttons
    printf("=== BUTTONS AND CLICKABLE ELEMENTS ===\n");
    int buttonCount = 0;
    for (int i = 0; i < totalElements; i++) {
        ELEMENT_INFO* elem = &pSharedData->elements[i];
        BOOL isButton = (strstr(elem->className, "Button") != NULL) ||
                       (strstr(elem->text, "Enable") != NULL) ||
                       (strstr(elem->text, "Disable") != NULL) ||
                       (strstr(elem->text, "Menu") != NULL) ||
                       (strstr(elem->text, "Open") != NULL);
                
        if (isButton) {
            DisplayElement(elem, buttonCount);
            buttonCount++;
        }
    }
    printf("Found %d clickable elements\n\n", buttonCount);
    
    // Show Windows Defender specific buttons
    printf("=== WINDOWS DEFENDER CONTROLS ===\n");
    int defenderCount = 0;
    for (int i = 0; i < totalElements; i++) {
        ELEMENT_INFO* elem = &pSharedData->elements[i];
        BOOL isDefenderButton = (strstr(elem->className, "Button") != NULL) &&
                               ((strstr(elem->text, "Enable") != NULL) ||
                                (strstr(elem->text, "Disable") != NULL));
        
        if (isDefenderButton) {
            DisplayElement(elem, defenderCount);
            defenderCount++;
        }
    }
    printf("Found %d Windows Defender buttons\n", defenderCount);
    
    UnmapViewOfFile(pSharedData);
    CloseHandle(hMapFile);
}

BOOL EnableDebugPrivilege() {
    HANDLE hToken;
    TOKEN_PRIVILEGES tkp;
    
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &hToken)) {
        return FALSE;
    }
    
    if (!LookupPrivilegeValue(NULL, SE_DEBUG_NAME, &tkp.Privileges[0].Luid)) {
        CloseHandle(hToken);
        return FALSE;
    }
    
    tkp.PrivilegeCount = 1;
    tkp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
    
    BOOL result = AdjustTokenPrivileges(hToken, FALSE, &tkp, 0, NULL, 0);
    CloseHandle(hToken);
    
    return result && GetLastError() == ERROR_SUCCESS;
}

int main() {
    printf("=== dControl Element Finder ===\n");
    
    // Try to enable debug privileges
    if (EnableDebugPrivilege()) {
        printf("Debug privileges enabled successfully.\n");
    } else {
        printf("Warning: Could not enable debug privileges (Error: %lu)\n", GetLastError());
        printf("This may limit injection capabilities.\n");
    }
    
    printf("Looking for process: %s\n", TARGET_PROCESS_NAME);
    
    DWORD pid = 0;
    while (pid == 0) {
        pid = findProcessId(TARGET_PROCESS_NAME);
        if (pid == 0) {
            printf("Process not found. Retrying in 5 seconds...\n");
            Sleep(5000);
        }
    }
    printf("Found %s with PID: %lu\n", TARGET_PROCESS_NAME, pid);

    // Get the path of the current executable and construct DLL path
    char exePath[MAX_PATH];
    char dllPath[MAX_PATH];
    GetModuleFileNameA(NULL, exePath, MAX_PATH);
    
    // Find the last backslash to get the directory
    char* lastSlash = strrchr(exePath, '\\');
    if (lastSlash) {
        *lastSlash = '\0';  // Terminate at the last backslash
        snprintf(dllPath, MAX_PATH, "%s\\hook.dll", exePath);
    } else {
        strcpy(dllPath, "hook.dll");  // Fallback to current directory
    }
    
    printf("Looking for DLL at: %s\n", dllPath);
    if (GetFileAttributesA(dllPath) == INVALID_FILE_ATTRIBUTES) {
        printf("Error: DLL not found at '%s'.\n", dllPath);
        return 1;
    }

    // Inject the DLL - try different access levels
    HANDLE hProcess = OpenProcess(PROCESS_ALL_ACCESS, FALSE, pid);
    if (hProcess == NULL) {
        printf("Failed with PROCESS_ALL_ACCESS, trying reduced privileges...\n");
        // Try with minimal required permissions
        hProcess = OpenProcess(PROCESS_CREATE_THREAD | PROCESS_VM_OPERATION | PROCESS_VM_WRITE | PROCESS_QUERY_INFORMATION, FALSE, pid);
        if (hProcess == NULL) {
            printf("Error: Could not open process with any access level. Error code: %lu\n", GetLastError());
            printf("This may indicate process protection or security software blocking access.\n");
            printf("Try running dControl.exe with compatibility mode or disable antivirus temporarily.\n");
            return 1;
        }
        printf("Successfully opened process with reduced privileges.\n");
    } else {
        printf("Successfully opened process with full access.\n");
    }

    LPVOID pRemoteMem = VirtualAllocEx(hProcess, NULL, strlen(dllPath) + 1, MEM_COMMIT, PAGE_READWRITE);
    if (pRemoteMem == NULL) {
        printf("Error: Could not allocate memory. Error code: %lu\n", GetLastError());
        CloseHandle(hProcess);
        return 1;
    }

    if (!WriteProcessMemory(hProcess, pRemoteMem, dllPath, strlen(dllPath) + 1, NULL)) {
        printf("Error: Could not write to process memory. Error code: %lu\n", GetLastError());
        VirtualFreeEx(hProcess, pRemoteMem, 0, MEM_RELEASE);
        CloseHandle(hProcess);
        return 1;
    }

    HANDLE hKernel32 = GetModuleHandleA("kernel32.dll");
    LPVOID pLoadLibraryA = (LPVOID)GetProcAddress(hKernel32, "LoadLibraryA");

    HANDLE hThread = CreateRemoteThread(hProcess, NULL, 0, (LPTHREAD_START_ROUTINE)pLoadLibraryA, pRemoteMem, 0, NULL);
    if (hThread == NULL) {
        printf("Error: Could not create remote thread. Error code: %lu\n", GetLastError());
        VirtualFreeEx(hProcess, pRemoteMem, 0, MEM_RELEASE);
        CloseHandle(hProcess);
        return 1;
    }

    printf("Success! DLL injected into %s\n", TARGET_PROCESS_NAME);
    printf("Waiting for element scanning to begin...\n");

    WaitForSingleObject(hThread, INFINITE);
    Sleep(3000);

    TestElementFinderWithSharedMemory();

    CloseHandle(hThread);
    VirtualFreeEx(hProcess, pRemoteMem, 0, MEM_RELEASE);
    CloseHandle(hProcess);

    printf("\nDLL continues running in target process.\n");
    printf("Run this program again to get updated element data.\n");
    printf("Press any key to exit...\n");
    getchar();

    return 0;
} 