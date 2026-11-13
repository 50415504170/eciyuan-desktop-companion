using namespace Gdiplus;

static ULONG_PTR graphicsToken;
static HWND advancedButton;
static bool advancedOpen, reducedMotion;
static WNDPROC buttonProc;
static HWND hoveredButton;
static float spring, velocity;
static POINT buttonPointer;
static DWORD expandStarted;
static float expandFrom = 566, expandTo = 566;
static DWORD introStarted, textStarted;
static float introOpacity = 1, statusOpacity = 1, fadeFrom = 1;
static int textPhase;
static std::wstring nextStatus;
static bool introPlayed, titleHovered, titleReplay;

static float Progress(DWORD started, float duration) {
    return (GetTickCount() - started) / duration;
}

static int Pixels(float value) { return (int)std::round(value * uiScale); }

static void SetStatus(const wchar_t* text) {
    if (reducedMotion) {
        SetWindowTextW(statusText, text);
        return;
    }
    nextStatus = text;
    fadeFrom = statusOpacity;
    textPhase = 1;
    textStarted = GetTickCount();
    SetTimer(GetParent(statusText), 4, 16, nullptr);
}

static void InitializeVisuals() {
    typedef BOOL(WINAPI* SetDpiAware)();
    SetDpiAware aware = (SetDpiAware)GetProcAddress(GetModuleHandleW(L"user32.dll"), "SetProcessDPIAware");
    if (aware) aware();
    HDC screen = GetDC(nullptr);
    uiScale = GetDeviceCaps(screen, LOGPIXELSX) / 96.0f;
    ReleaseDC(nullptr, screen);
    BOOL animations = TRUE;
    SystemParametersInfoW(0x1042, 0, &animations, 0);
    reducedMotion = !animations;
    GdiplusStartupInput input;
    GdiplusStartup(&graphicsToken, &input, nullptr);
}

static void RoundPath(GraphicsPath& path, float x, float y, float width, float height, float radius) {
    float diameter = radius * 2;
    path.AddArc(x, y, diameter, diameter, 180, 90);
    path.AddArc(x + width - diameter, y, diameter, diameter, 270, 90);
    path.AddArc(x + width - diameter, y + height - diameter, diameter, diameter, 0, 90);
    path.AddArc(x, y + height - diameter, diameter, diameter, 90, 90);
    path.CloseFigure();
}

static void Label(Graphics& graphics, const wchar_t* text, float x, float y, float width, float height,
                  float size, Color color, bool bold = false, bool centered = false) {
    FontFamily family(L"Microsoft YaHei UI");
    Font font(&family, size, bold ? FontStyleBold : FontStyleRegular, UnitPixel);
    SolidBrush brush(color);
    StringFormat format;
    format.SetLineAlignment(StringAlignmentCenter);
    if (centered) format.SetAlignment(StringAlignmentCenter);
    graphics.DrawString(text, -1, &font, RectF(x, y, width, height), &format, &brush);
}

static void Glass(Graphics& graphics, float x, float y, float width, float height, float radius) {
    GraphicsPath path;
    RoundPath(path, x, y, width, height, radius);
    LinearGradientBrush fill(PointF(x, y), PointF(x + width, y + height),
        Color(255, 29, 44, 57), Color(255, 17, 25, 35));
    graphics.FillPath(&fill, &path);
    LinearGradientBrush edge(PointF(x, y), PointF(x + width, y + height),
        Color(130, 159, 204, 222), Color(25, 159, 204, 222));
    Pen border(&edge, 1);
    graphics.DrawPath(&border, &path);
}

static LRESULT CALLBACK ButtonProc(HWND control, UINT message, WPARAM word, LPARAM parameter) {
    if (message == WM_MOUSEMOVE) {
        buttonPointer.x = (short)LOWORD(parameter);
        buttonPointer.y = (short)HIWORD(parameter);
        if (!reducedMotion && IsWindowEnabled(control)) InvalidateRect(control, nullptr, FALSE);
    }
    if (message == WM_MOUSEMOVE && hoveredButton != control) {
        HWND previous = hoveredButton;
        hoveredButton = control;
        TRACKMOUSEEVENT tracking = {sizeof(tracking), TME_LEAVE, control, 0};
        TrackMouseEvent(&tracking);
        if (previous) InvalidateRect(previous, nullptr, FALSE);
        SetTimer(GetParent(control), 2, 16, nullptr);
        InvalidateRect(control, nullptr, FALSE);
    }
    if (message == WM_MOUSELEAVE && hoveredButton == control) {
        hoveredButton = nullptr;
        SetTimer(GetParent(control), 2, 16, nullptr);
        InvalidateRect(control, nullptr, FALSE);
    }
    if (message == WM_SETCURSOR && IsWindowEnabled(control)) {
        SetCursor(LoadCursorW(nullptr, IDC_HAND));
        return TRUE;
    }
    if ((message == WM_LBUTTONDOWN || message == WM_LBUTTONUP ||
        ((message == WM_KEYDOWN || message == WM_KEYUP) && word == VK_SPACE)) &&
        IsWindowEnabled(control) && !reducedMotion) {
        if (control == enableButton) {
            spring = -2.6f;
            velocity = 0;
        }
        SetTimer(GetParent(control), 2, 16, nullptr);
    }
    return CallWindowProcW(buttonProc, control, message, word, parameter);
}

static HWND Control(HWND parent, const wchar_t* type, const wchar_t* text, DWORD style,
                    int x, int y, int width, int height, int identifier = 0) {
    HWND control = CreateWindowExW(0, type, text, WS_CHILD | WS_VISIBLE | style,
        Pixels(x), Pixels(y), Pixels(width), Pixels(height), parent, (HMENU)(intptr_t)identifier, nullptr, nullptr);
    SendMessageW(control, WM_SETFONT, (WPARAM)bodyFont, TRUE);
    if (!wcscmp(type, L"BUTTON")) {
        WNDPROC previous = (WNDPROC)SetWindowLongPtrW(control, GWLP_WNDPROC, (LONG_PTR)ButtonProc);
        if (!buttonProc) buttonProc = previous;
    }
    return control;
}

static void Paint(HWND window) {
    PAINTSTRUCT paint;
    HDC dc = BeginPaint(window, &paint);
    RECT client;
    GetClientRect(window, &client);
    Bitmap buffer(client.right, client.bottom, PixelFormat32bppPARGB);
    Graphics graphics(&buffer);
    graphics.SetSmoothingMode(SmoothingModeAntiAlias);
    graphics.ScaleTransform(uiScale, uiScale);
    float height = client.bottom / uiScale;
    LinearGradientBrush backdrop(PointF(0, 0), PointF(620, height),
        Color(255, 20, 32, 44), Color(255, 9, 15, 23));
    graphics.FillRectangle(&backdrop, 0.0f, 0.0f, 620.0f, height);
    Pen rim(Color(100, 131, 167, 185), 1);
    GraphicsPath outline;
    RoundPath(outline, 0.5f, 0.5f, 619, height - 1, 20);
    graphics.DrawPath(&rim, &outline);
    Glass(graphics, 32, 30, 34, 34, 11);
    Pen icon(Color(255, 129, 231, 235), 1.5f);
    graphics.DrawRectangle(&icon, 41, 39, 16, 11);
    graphics.DrawLine(&icon, 49, 50, 49, 55);
    graphics.DrawLine(&icon, 45, 55, 53, 55);
    BYTE titleAlpha = (BYTE)(255 * introOpacity);
    BYTE brandAlpha = titleReplay ? 255 : titleAlpha;
    Label(graphics, L"全屏陪玩", 78, 26, 220, 27, 17, Color(brandAlpha, 235, 242, 247), true);
    Label(graphics, L"鹅次元桌面扩展", 78, 53, 240, 18, 11, Color(brandAlpha, 145, 163, 178));
    Label(graphics, L"让陪伴，延伸到每一屏。", 32, 101, 550, 45, 29, Color(titleAlpha, 239, 245, 250), true);
    Label(graphics, L"游戏、浏览器、整个桌面。", 33, 151, 530, 26, 14, Color(titleAlpha, 163, 181, 195));
    Label(graphics, L"鹅次元主程序", 33, 198, 270, 24, 12, Color(255, 169, 185, 198));
    Glass(graphics, 32, 232, 556, 54, 15);
    Glass(graphics, 32, 305, 556, 73, 17);
    SolidBrush dot(operation ? Color(255, 239, 190, 110) : Color(255, 129, 231, 235));
    graphics.FillEllipse(&dot, 49.0f, 323.0f, 6.0f, 6.0f);
    Label(graphics, operation ? L"正在处理" : L"操作状态", 62, 315, 400, 23, 11, Color(255, 156, 177, 192));
    Label(graphics, L"开启 / 还原将重启鹅次元，当前通话会中断。", 33, 472, 555, 24, 11, Color(255, 178, 184, 190));
    Label(graphics, L"共享内容可能上传至鹅次元云端", 33, 497, 555, 22, 11, Color(255, 139, 156, 170));
    Label(graphics, L"0.1.0  实验版", 459, 530, 128, 24, 11, Color(255, 139, 156, 170), false, true);
    Graphics output(dc);
    output.DrawImage(&buffer, 0, 0);
    EndPaint(window, &paint);
}

static void PaintButton(DRAWITEMSTRUCT* item) {
    int width = item->rcItem.right, height = item->rcItem.bottom;
    Bitmap buffer(width, height, PixelFormat32bppPARGB);
    Graphics graphics(&buffer);
    graphics.Clear(Color(255, 14, 23, 32));
    graphics.SetSmoothingMode(SmoothingModeAntiAlias);
    graphics.ScaleTransform(uiScale, uiScale);
    float logicalWidth = width / uiScale, logicalHeight = height / uiScale;
    bool primary = item->CtlID == 12;
    bool hover = hoveredButton == item->hwndItem;
    bool disabled = (item->itemState & ODS_DISABLED) != 0;
    bool pressed = (item->itemState & ODS_SELECTED) != 0;
    float inset = primary ? (pressed ? 8.0f : 3.0f - spring * 0.8f) : 1;
    float verticalInset = primary ? (pressed ? 6.0f : 3.0f - spring * 1.6f) : 1;
    if (verticalInset < 0.7f) verticalInset = 0.7f;
    GraphicsPath path;
    RoundPath(path, inset, verticalInset, logicalWidth - 2 * inset,
        logicalHeight - 2 * verticalInset, primary ? 18 : 10);
    if (primary) {
        LinearGradientBrush fill(PointF(0, 0), PointF(logicalWidth, logicalHeight),
            disabled ? Color(255, 61, 86, 93) : Color(255, 165, 239, 238),
            disabled ? Color(255, 36, 59, 71) : Color(255, 85, 183, 210));
        graphics.FillPath(&fill, &path);
        Pen shine(Color(180, 218, 255, 255), 1);
        graphics.DrawPath(&shine, &path);
    } else if (hover || (item->itemState & ODS_FOCUS)) {
        SolidBrush fill(Color(255, 31, 48, 62));
        graphics.FillPath(&fill, &path);
    }
    if (!disabled && !reducedMotion && hover) {
        GraphicsState saved = graphics.Save();
        graphics.SetClip(&path);
        float pointerX = buttonPointer.x / uiScale, pointerY = buttonPointer.y / uiScale;
        if (hover) {
            GraphicsPath halo;
            halo.AddEllipse(pointerX - 110, pointerY - 80, 220.0f, 160.0f);
            PathGradientBrush light(&halo);
            light.SetCenterPoint(PointF(pointerX, pointerY));
            light.SetCenterColor(Color(primary ? 80 : 35, 224, 255, 255));
            Color transparent(0, 224, 255, 255);
            INT count = 1;
            light.SetSurroundColors(&transparent, &count);
            graphics.FillPath(&light, &halo);
        }
        graphics.Restore(saved);
    }
    wchar_t text[128];
    GetWindowTextW(item->hwndItem, text, 128);
    Color textColor = disabled ? Color(255, 125, 143, 156) :
        primary ? Color(255, 9, 38, 47) : item->CtlID == 14 ? Color(255, 232, 159, 151) : Color(255, 183, 204, 218);
    Label(graphics, text, 0, pressed ? 1 : 0, logicalWidth, logicalHeight, primary ? 17 : 12, textColor, primary, true);
    if (item->itemState & ODS_FOCUS) {
        Pen focus(Color(255, 227, 252, 255), 1);
        graphics.DrawPath(&focus, &path);
    }
    Graphics output(item->hDC);
    output.DrawImage(&buffer, 0, 0);
}

static LRESULT CALLBACK LauncherProc(HWND window, UINT message, WPARAM word, LPARAM parameter) {
    if (message == WM_CREATE) {
        bodyFont = CreateFontW(-Pixels(12), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei UI");
        fieldBrush = CreateSolidBrush(RGB(24, 36, 48));
        pathEdit = Control(window, L"EDIT", L"", ES_AUTOHSCROLL | WS_TABSTOP, 49, 250, 417, 22, 20);
        browseButton = Control(window, L"BUTTON", L"浏览…", BS_OWNERDRAW | WS_TABSTOP, 484, 242, 91, 34, 10);
        inspectButton = Control(window, L"BUTTON", L"检测兼容性", BS_OWNERDRAW | WS_TABSTOP, 478, 195, 110, 30, 11);
        statusText = Control(window, L"EDIT", L"先检测兼容性，再开启共享。", ES_READONLY | ES_MULTILINE | ES_AUTOVSCROLL | WS_TABSTOP,
            49, 340, 520, 32, 21);
        enableButton = Control(window, L"BUTTON", L"开启陪玩  →", BS_OWNERDRAW | WS_TABSTOP, 32, 401, 556, 58, 12);
        advancedButton = Control(window, L"BUTTON", L"高级操作  ⌄", BS_OWNERDRAW | WS_TABSTOP, 32, 527, 118, 30, 15);
        pauseButton = Control(window, L"BUTTON", L"暂停画面共享", BS_OWNERDRAW | WS_TABSTOP, 32, 574, 260, 38, 13);
        restoreButton = Control(window, L"BUTTON", L"还原原版", BS_OWNERDRAW | WS_TABSTOP, 308, 574, 280, 38, 14);
        Control(window, L"BUTTON", L"−", BS_OWNERDRAW | WS_TABSTOP, 516, 26, 32, 30, 17);
        Control(window, L"BUTTON", L"×", BS_OWNERDRAW | WS_TABSTOP, 559, 26, 32, 30, 18);
        ShowWindow(pauseButton, SW_HIDE);
        ShowWindow(restoreButton, SW_HIDE);
        wchar_t candidate[MAX_PATH];
        for (wchar_t drive = L'C'; drive <= L'Z'; ++drive) {
            swprintf(candidate, MAX_PATH, L"%c:\\Eciyuan\\0.1.15.624\\Eciyuan.exe", drive);
            if (GetFileAttributesW(candidate) != INVALID_FILE_ATTRIBUTES) {
                SetWindowTextW(pathEdit, candidate);
                break;
            }
        }
        HMODULE dwm = LoadLibraryW(L"dwmapi.dll");
        if (dwm) {
            typedef HRESULT(WINAPI* SetAttribute)(HWND, DWORD, LPCVOID, DWORD);
            SetAttribute set = (SetAttribute)GetProcAddress(dwm, "DwmSetWindowAttribute");
            DWORD corner = 2;
            if (set) set(window, 33, &corner, sizeof(corner));
            FreeLibrary(dwm);
        }
        SetTimer(window, 1, 500, nullptr);
        if (!reducedMotion) introOpacity = 0;
        return 0;
    }
    if (message == WM_ACTIVATE && LOWORD(word) != WA_INACTIVE && !introPlayed && !reducedMotion) {
        introPlayed = true;
        introStarted = GetTickCount();
        SetTimer(window, 5, 16, nullptr);
    }
    if (message == WM_MOUSEMOVE) {
        int pointerX = (short)LOWORD(parameter), pointerY = (short)HIWORD(parameter);
        bool overTitle = pointerX >= Pixels(32) && pointerX <= Pixels(588) &&
            pointerY >= Pixels(100) && pointerY <= Pixels(180);
        if (overTitle && !titleHovered && introOpacity >= 1 && !reducedMotion) {
            titleReplay = true;
            introStarted = GetTickCount();
            SetTimer(window, 5, 16, nullptr);
        }
        titleHovered = overTitle;
        TRACKMOUSEEVENT tracking = {sizeof(tracking), TME_LEAVE, window, 0};
        TrackMouseEvent(&tracking);
    }
    if (message == WM_MOUSELEAVE) titleHovered = false;
    if (message == WM_COMMAND) {
        switch (LOWORD(word)) {
        case 10: {
            wchar_t file[MAX_PATH] = L"";
            OPENFILENAMEW picker = {sizeof(picker)};
            picker.hwndOwner = window;
            picker.lpstrFilter = L"鹅次元主程序\0Eciyuan.exe\0";
            picker.lpstrFile = file;
            picker.nMaxFile = MAX_PATH;
            picker.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
            if (GetOpenFileNameW(&picker)) SetWindowTextW(pathEdit, file);
            break;
        }
        case 11: StartOperation(window, L"Inspect"); break;
        case 12: StartOperation(window, L"Enable"); break;
        case 13: {
            if (operation) break;
            HWND anchor = FindWindowW(L"EciyuanDesktopCompanionAnchor", nullptr);
            if (anchor) PostMessageW(anchor, WM_APP + 1, 0, 0);
            SetStatus(anchor ? L"已请求暂停画面共享，保留语音入口。" : L"未发现共享窗口。");
            break;
        }
        case 14: StartOperation(window, L"Restore"); break;
        case 15:
            advancedOpen = !advancedOpen;
            ShowWindow(pauseButton, advancedOpen ? SW_SHOW : SW_HIDE);
            ShowWindow(restoreButton, advancedOpen ? SW_SHOW : SW_HIDE);
            SetWindowTextW(advancedButton, advancedOpen ? L"收起操作  ⌃" : L"高级操作  ⌄");
            {
                RECT bounds;
                GetWindowRect(window, &bounds);
                expandFrom = (bounds.bottom - bounds.top) / uiScale;
                expandTo = advancedOpen ? 632 : 566;
                expandStarted = GetTickCount();
                if (reducedMotion) SetWindowPos(window, nullptr, 0, 0, Pixels(620), Pixels(expandTo), SWP_NOMOVE | SWP_NOZORDER);
                else SetTimer(window, 3, 16, nullptr);
            }
            InvalidateRect(window, nullptr, FALSE);
            break;
        case 17: ShowWindow(window, SW_MINIMIZE); break;
        case 18: SendMessageW(window, WM_CLOSE, 0, 0); break;
        }
        return 0;
    }
    if (message == WM_TIMER && word == 2) {
        float target = hoveredButton == enableButton ? 1.0f : 0.0f;
        velocity = (velocity + (target - spring) * 0.18f) * 0.78f;
        spring = reducedMotion ? target : spring + velocity;
        InvalidateRect(enableButton, nullptr, FALSE);
        if (std::fabs(target - spring) < 0.005f && std::fabs(velocity) < 0.005f) {
            spring = target;
            KillTimer(window, 2);
        }
        return 0;
    }
    if (message == WM_TIMER && word == 3) {
        float progress = Progress(expandStarted, 200);
        if (progress > 1) progress = 1;
        float easing = 1 - std::pow(1 - progress, 3);
        SetWindowPos(window, nullptr, 0, 0, Pixels(620), Pixels(expandFrom + (expandTo - expandFrom) * easing),
            SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
        InvalidateRect(window, nullptr, FALSE);
        if (progress >= 1) KillTimer(window, 3);
        return 0;
    }
    if (message == WM_TIMER && word == 4) {
        float progress = Progress(textStarted, textPhase == 1 ? 250.0f : 450.0f);
        if (progress > 1) progress = 1;
        float easing = progress * progress * (3 - 2 * progress);
        statusOpacity = textPhase == 1 ? fadeFrom * (1 - easing) : easing;
        if (progress >= 1) {
            if (textPhase == 1) {
                SetWindowTextW(statusText, nextStatus.c_str());
                textPhase = 2;
                textStarted = GetTickCount();
            } else {
                textPhase = 0;
                KillTimer(window, 4);
            }
        }
        InvalidateRect(statusText, nullptr, FALSE);
        return 0;
    }
    if (message == WM_TIMER && word == 5) {
        float progress = Progress(introStarted, titleReplay ? 1000.0f : 1100.0f);
        if (progress > 1) progress = 1;
        if (titleReplay) {
            float phase = progress < 0.5f ? progress * 2 : (1 - progress) * 2;
            introOpacity = 1 - 0.88f * phase * phase * (3 - 2 * phase);
        } else {
            introOpacity = progress * progress * (3 - 2 * progress);
        }
        RECT heading = {Pixels(30), Pixels(24), Pixels(585), Pixels(180)};
        InvalidateRect(window, &heading, FALSE);
        if (progress >= 1) {
            introOpacity = 1;
            titleReplay = false;
            KillTimer(window, 5);
        }
        return 0;
    }
    if (message == WM_TIMER && word == 1 && operation && WaitForSingleObject(operation, 0) == WAIT_OBJECT_0) {
        CloseHandle(operation);
        operation = nullptr;
        for (HWND control : {enableButton, inspectButton, restoreButton, pauseButton, browseButton, pathEdit}) EnableWindow(control, TRUE);
        SetStatus(ReadReport().c_str());
        InvalidateRect(window, nullptr, FALSE);
        return 0;
    }
    if (message == WM_PAINT) { Paint(window); return 0; }
    if (message == WM_ERASEBKGND) return 1;
    if (message == WM_DRAWITEM) { PaintButton((DRAWITEMSTRUCT*)parameter); return TRUE; }
    if (message == WM_CTLCOLOREDIT || message == WM_CTLCOLORSTATIC) {
        float opacity = (HWND)parameter == statusText ? statusOpacity : 1;
        SetTextColor((HDC)word, RGB((int)(24 + 181 * opacity), (int)(36 + 187 * opacity), (int)(48 + 185 * opacity)));
        SetBkColor((HDC)word, RGB(24, 36, 48));
        return (LRESULT)fieldBrush;
    }
    if (message == WM_LBUTTONDOWN && HIWORD(parameter) < Pixels(92)) {
        ReleaseCapture();
        SendMessageW(window, WM_NCLBUTTONDOWN, HTCAPTION, 0);
        return 0;
    }
    if (message == WM_DESTROY) {
        if (operation) CloseHandle(operation);
        DeleteObject(bodyFont);
        DeleteObject(fieldBrush);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(window, message, word, parameter);
}
