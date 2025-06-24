#Requires AutoHotkey v2.0
#SingleInstance Force
#Include Lib\sordum_ripper.ahk

if !A_IsAdmin {
    try {
        Run '*RunAs "' A_ScriptFullPath '"'
    } catch {
        MsgBox "Failed to run as administrator. Please run the script as administrator manually."
    }
    ExitApp
}

if !DllCall("AttachConsole", "Int", -1)
    DllCall("AllocConsole")

ConsolePrint(str) => FileAppend(str, "*")
ConsolePrint("Running as administrator: " (A_IsAdmin ? "YES" : "NO") . "`n")
ConsolePrint("Starting dControl.exe...`n")
try {
    Run "dControl.exe"
} catch as err {
    MsgBox "Failed to start dControl.exe: " err.Message
    ExitApp
}

ConsolePrint("Waiting for dControl.exe to be ready...`n")
timeout := 30000
startTime := A_TickCount
while !ProcessExist("dControl.exe") {
    if (A_TickCount - startTime) > timeout {
        MsgBox "dControl.exe process did not start within 30 seconds"
        ExitApp
    }
    Sleep 100
}

Sleep 2000
ConsolePrint("dControl.exe is ready, proceeding with DLL injection...`n")
ConsolePrint("Executing loader.exe for DLL injection...`n")
try {
    RunWait A_WorkingDir "\Lib\loader.exe", A_WorkingDir "\Lib"
    ConsolePrint("DLL injection completed successfully.`n")
} catch as err {
    MsgBox "Failed to execute loader.exe: " err.Message
    ExitApp
}

ConsolePrint("Proceeding with automation...`n")
try {
    rip := SR()
} catch as err {
    MsgBox "Failed to connect: " err.Message
    ExitApp
}

rip.DumpElements()

btn := rip.FindByText("Disable Windows Defender")
ConsolePrint(rip.DebugElement(btn) . "`n")
success := rip.Click(btn)

result := rip.WaitForText("Windows Defender is turned off")
if (result) {
    MsgBox "Success! Windows Defender is turned off.", "Windows Defender Status"
} else {
    MsgBox "Timeout: Could not confirm that Windows Defender was turned off.", "Windows Defender Status"
}

Sleep 300000

