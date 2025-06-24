#Requires AutoHotkey v2.0
; ------------------------------------------------------------
; Sordum Ripper Library (64-bit AutoHotkey v2)
; ------------------------------------------------------------
; Provides a thin OO wrapper around the shared memory exposed
; by hook.dll (ElementFinderSharedMemory).  Typical workflow:
;
;   rip := SR()                           ; singleton instance
;   snap := rip.ReadSnapshot()            ; {elements, lastUpdate}
;   elem := rip.FindByText("Disable")     ; first match
;   rip.Click(elem)                       ; send BN_CLICKED bypass
;
; Each element is returned as a Map with the following keys:
;   hwnd, text, className, screenX, screenY,
;   width, height, centerX, centerY, isVisible, isEnabled
; ------------------------------------------------------------

class SordumRipper {
    static FILE_MAP_WRITE := 0x0002
    static SHARED_NAME   := "ElementFinderSharedMemory"
    static STRUCT_SIZE   := 580   ; sizeof(ELEMENT_INFO)
    static HEADER_SIZE   := 12    ; elementCount + lastUpdate + targetHwnd

    __New() {
        this.hMap  := DllCall("OpenFileMappingA", "UInt", SordumRipper.FILE_MAP_WRITE,
                                               "Int", 0,
                                               "AStr", SordumRipper.SHARED_NAME,
                                               "Ptr")
        if !this.hMap
            throw Error("SordumRipper: unable to open shared memory (" A_LastError ")")

        this.pData := DllCall("MapViewOfFile", "Ptr",  this.hMap,
                                                "UInt", SordumRipper.FILE_MAP_WRITE,
                                                "UInt", 0, "UInt", 0,
                                                "UPtr", 0, "Ptr")
        if !this.pData
            throw Error("SordumRipper: unable to map view (" A_LastError ")")

        ; Cache DPI scaling factor
        this.dpi := DllCall("GetDpiForSystem")
        this.dpiScale := this.dpi / 96.0
    }

    ; Returns an object { elements: [], lastUpdate: ms }
    ReadSnapshot() {
        elementCount := NumGet(this.pData + 0, "UInt")
        lastUpdate   := NumGet(this.pData + 4, "UInt")

        elems := []
        offset := SordumRipper.HEADER_SIZE
        Loop elementCount {
            elem := Map()
            elem.hwnd := NumGet(this.pData + offset + 0, "UInt")
            elem.text := StrGet(this.pData + offset + 4, 256, "CP0")
            elem.className := StrGet(this.pData + offset + 260, 256, "CP0")
            
            ; Get window coordinates
            rect_left   := NumGet(this.pData + offset + 516, "Int")
            rect_top    := NumGet(this.pData + offset + 520, "Int")
            rect_right  := NumGet(this.pData + offset + 524, "Int")
            rect_bottom := NumGet(this.pData + offset + 528, "Int")
            
            ; Apply DPI scaling to coordinates
            elem.screenX := Round(rect_left * this.dpiScale)
            elem.screenY := Round(rect_top * this.dpiScale)
            elem.width   := Round((rect_right - rect_left) * this.dpiScale)
            elem.height  := Round((rect_bottom - rect_top) * this.dpiScale)
            elem.centerX := elem.screenX + elem.width  // 2
            elem.centerY := elem.screenY + elem.height // 2
            
            ; Status flags
            elem.isVisible := NumGet(this.pData + offset + 572, "Int")
            elem.isEnabled := NumGet(this.pData + offset + 576, "Int")
            elems.Push(elem)
            offset += SordumRipper.STRUCT_SIZE
        }
        return {elements: elems, lastUpdate: lastUpdate}
    }

    ; First element containing `needle` inside its text; returns Map or blank
    FindByText(needle, caseSensitive:=false) {
        snap := this.ReadSnapshot()
        needle := caseSensitive ? needle : StrLower(needle)
        for elem in snap.elements {
            hay := caseSensitive ? elem.text : StrLower(elem.text)
            if InStr(hay, needle)
                return elem
        }
        return
    }

    Click(elem) {
        if !IsObject(elem)
            return false
            
        ; Simply write the target HWND to shared memory for the DLL to process
        NumPut("UInt", elem.hwnd, this.pData + 8)  ; targetHwnd at offset 8
        
        ; Wait briefly for the DLL to process it
        Sleep(100)
        
        ; Check if it was processed (HWND gets cleared by DLL)
        result := NumGet(this.pData + 8, "UInt") == 0
        
        return result
    }

    ; Get DPI information
    GetDPI() {
        return {dpi: this.dpi, scale: this.dpiScale}
    }
    
    ; Debug method to show element info
    DebugElement(elem) {
        if !IsObject(elem)
            return ""
        
        info := this.GetDPI()
        return Format("Element: '{}' ({})`n", elem.text, elem.className) .
               Format("  Coords: ({},{}) {}x{} center=({},{})`n", elem.screenX, elem.screenY, elem.width, elem.height, elem.centerX, elem.centerY) .
               Format("  DPI: {} (scale: {:.2f}) HWND: 0x{:X}", info.dpi, info.scale, elem.hwnd)
    }

    ; Print all elements to console (max=0 means all)
    DumpElements(max:=0) {
        snap := this.ReadSnapshot()
        total := snap.elements.Length
        info := this.GetDPI()
        FileAppend(Format("Snapshot: {} elements (age {} ms) DPI: {} (scale: {:.2f})`n", total, A_TickCount - snap.lastUpdate, info.dpi, info.scale), "*")
        count := (max>0 && max<total) ? max : total
        Loop count {
            elem := snap.elements[A_Index]
            FileAppend(Format("#{:02d}: '{}' ({})`n", A_Index, elem.text, elem.className), "*")
        }
        FileAppend("-----------------------------`n", "*")
    }

    ; Convenience: find by text and click
    ClickText(str) {
        elem := this.FindByText(str)
        if (elem)
            return this.Click(elem)
        return false
    }

    ; Wait for specific text to appear with timeout
    WaitForText(searchText, timeoutMs := 30000, checkIntervalMs := 500) {
        startTime := A_TickCount
        
        while (A_TickCount - startTime) < timeoutMs {
            elem := this.FindByText(searchText)
            if (elem) {
                return elem
            }
            Sleep(checkIntervalMs)
        }
        
        return false
    }

    __Delete() {
        if this.pData
            DllCall("UnmapViewOfFile", "Ptr", this.pData)
        if this.hMap
            DllCall("CloseHandle", "Ptr", this.hMap)
    }
}

; Singleton helper – call SR() to reuse single instance
SR() {
    static _inst := SordumRipper()
    return _inst
} 