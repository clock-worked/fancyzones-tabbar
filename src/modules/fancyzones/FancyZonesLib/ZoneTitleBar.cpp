#include "pch.h"
#include "ZoneTitleBar.h"
#include "Drawing.h"
#include "CompositionDrawing.h"
#include <FancyZonesLib/WindowUtils.h>
#include <common/Themes/windows_colors.h>
#include <set>
#include <unordered_map>
#include <windowsx.h>

// Legacy FancyZones title bar code relies on several event callback signatures
// with intentionally unused parameters and some signed/unsigned math.
#pragma warning(push)
#pragma warning(disable : 4100 4189 4245 4389 26493)

/* void Cls_OnDwmNcRenderingChanged(HWND hwnd, BOOL fEnabled) */
#define HANDLE_WM_DWMNCRENDERINGCHANGED(hwnd, wParam, lParam, fn) \
    ((fn)((hwnd), (BOOL)(wParam)), 0L)

/* void Cls_OnDwmColorizationColorChanged(HWND hwnd, DWORD dwNewColorizationColor, BOOL fIsBlendedWithOpacity) */
#define HANDLE_WM_DWMCOLORIZATIONCOLORCHANGED(hwnd, wParam, lParam, fn) \
    ((fn)((hwnd), (DWORD)(wParam), (BOOL)(lParam)), 0L)

/* void Cls_OnMouseLeave(HWND hwnd) */
#define HANDLE_WM_MOUSELEAVE(hwnd, wParam, lParam, fn) \
    ((fn)((hwnd)), 0L)


using namespace FancyZonesUtils;

static D2D1_COLOR_F ParseHexColor(std::wstring_view hexColor)
{
    if (hexColor.size() != 7 || hexColor[0] != L'#')
    {
        return D2D1::ColorF(0x0078D7u);
    }

    auto parseByte = [](std::wstring_view value) {
        return static_cast<unsigned char>(std::stoi(std::wstring(value), nullptr, 16));
    };

    const auto red = parseByte(hexColor.substr(1, 2));
    const auto green = parseByte(hexColor.substr(3, 2));
    const auto blue = parseByte(hexColor.substr(5, 2));

    return D2D1::ColorF(static_cast<float>(red) / 255.f, static_cast<float>(green) / 255.f, static_cast<float>(blue) / 255.f, 1.f);
}

static std::unordered_map<HWND, std::wstring> tabTitleOverrides;

class ZoneTitleBarColors
{
public:
    ZoneTitleBarColors() :
        ZoneTitleBarColors(WindowsColors::is_dark_mode())
    {
    }

    ZoneTitleBarColors(bool isDarkMode)
    {
        backColor = Drawing::ConvertColor(WindowsColors::get_background_color());

        frameColor = Drawing::ConvertColor(isDarkMode ? WindowsColors::get_gray_text_color() : WindowsColors::get_button_face_color());
        textColor = Drawing::ConvertColor(WindowsColors::get_button_text_color());

        highlightFrameColor = Drawing::ConvertColor(WindowsColors::get_accent_color());
        highlightTextColor = Drawing::ConvertColor(WindowsColors::get_highlight_text_color());
    }

    D2D1_COLOR_F backColor;
    D2D1_COLOR_F frameColor;
    D2D1_COLOR_F textColor;
    D2D1_COLOR_F highlightFrameColor;
    D2D1_COLOR_F highlightTextColor;
};

class HiddenWindow : public Window
{
public:
    HiddenWindow(HINSTANCE hinstance)
    {
        Init(hinstance, nullptr, 0, WS_EX_TOOLWINDOW, Rect(0, 0, 0, 0), 0, 0, 0, SW_HIDE);
    }

    void HideWindowFromTaskbar(HWND window)
    {
        HWND val = *this;
        SetWindowLongPtr(window, GWLP_HWNDPARENT, (LONG_PTR)val);
    }
};

static HWND GetWindowAboveAllOthers(const std::vector<HWND>& windows)
{
    if (windows.empty())
    {
        return NULL;
    }

    std::set<HWND> windowsSet(windows.begin(), windows.end());

    // Get the window above all others
    HWND max = NULL;
    for (HWND current = windows.front(); !windows.empty() && current != NULL; current = GetWindow(current, GW_HWNDPREV))
    {
        auto i = windowsSet.find(current);
        if (i != windowsSet.end())
        {
            max = current;
            windowsSet.erase(i);
        }
    }

    return max;
}

static void SwitchToWindowByIndex(const std::vector<HWND>& windows, int i)
{
    if (i >= windows.size())
    {
        FancyZonesWindowUtils::SwitchToWindow(windows.back());
    }
    else if (i <= 0)
    {
        FancyZonesWindowUtils::SwitchToWindow(windows.front());
    }
    else
    {
        FancyZonesWindowUtils::SwitchToWindow(windows[i]);
    }
}

static void DrawWindowIcon(Drawing& drawing, const D2D1_RECT_F& rect, HWND window, float opacity = 1.f)
{
    HICON icon = nullptr;
    if (!SendMessageTimeout(window, WM_GETICON, ICON_BIG, 0, 0, 100, (PDWORD_PTR)&icon) || icon == nullptr)
    {
        icon = (HICON)GetClassLongPtrW(window, GCLP_HICON);
    }

    if (icon != nullptr)
    {
        auto bitmap = drawing.CreateIcon(icon);
        if (bitmap)
        {
            const auto bitmapSize = bitmap->GetSize();
            const auto width = rect.right - rect.left;
            const auto height = rect.bottom - rect.top;
            const auto scale = min(width / bitmapSize.width, height / bitmapSize.height);
            const auto iconWidth = bitmapSize.width * scale;
            const auto iconHeight = bitmapSize.height * scale;
            const auto iconRect = D2D1::RectF(
                rect.left + (width - iconWidth) / 2,
                rect.top + (height - iconHeight) / 2,
                rect.left + (width + iconWidth) / 2,
                rect.top + (height + iconHeight) / 2);
            drawing.DrawBitmap(iconRect, bitmap.get(), opacity);
        }
    }
}

class NoZoneTitleBar : public IZoneTitleBar
{
public:
    NoZoneTitleBar(Rect zone) noexcept :
        m_zone(zone)
    {
    }

    void Show(bool show) override {}

    void UpdateZoneWindows(std::vector<HWND> zoneWindows) override {}

    void ReadjustPos() override {}

    Rect GetZoneRect() const override { return m_zone; }

    UINT GetDpi() const override { return 96; }

    Rect GetInlineFrame() const override { return m_zone; }

private:
    Rect m_zone;
};

class VisibleZoneTitleBar : public IZoneTitleBar
{
protected:
    VisibleZoneTitleBar(bool isAboveZone, Rect zone, UINT dpi, std::function<void(HWND)> removeWindowFromZoneCallback = {}) :
        m_isAboveZone(isAboveZone),
        m_zone(zone),
        m_dpi(dpi),
        m_zoneCurrentWindow(NULL),
        m_removeWindowFromZoneCallback(std::move(removeWindowFromZoneCallback))
    {
    }

    void Init(HINSTANCE hinstance, Rect rect, DWORD style, DWORD extendedStyle)
    {
        auto proc = [this](HWND window, UINT message, WPARAM wParam, LPARAM lParam) { return WndProc(window, message, wParam, lParam); };

        m_window.Init(hinstance, proc, style, extendedStyle, rect);
    }

    HWND GetDestinedWindowBeforeTheZoneTitleBar()
    {
        if (m_isAboveZone)
        {
            // Put the zone title bar just above the zone current window
            {
                // Get the window above the zone current window
                HWND windowAboveZoneCurrentWindow = GetWindow(m_zoneCurrentWindow, GW_HWNDPREV);

                // Put the zone title bar just below the windowAboveZoneCurrentWindow
                return windowAboveZoneCurrentWindow ? windowAboveZoneCurrentWindow : HWND_TOP;
            }
        }
        else
        {
            // Put the zone title bar just below the zoneCurrentWindow
            return m_zoneCurrentWindow;
        }
    }

    void ReadjustPos() override
    {
        m_zoneCurrentWindow = GetWindowAboveAllOthers(m_zoneWindows);

        HWND windowBeforeTheZoneTitleBar = m_zoneCurrentWindow != NULL ? GetDestinedWindowBeforeTheZoneTitleBar() : HWND_TOP;
        SetWindowPos(m_window, windowBeforeTheZoneTitleBar, 0, 0, 0, 0, SWP_NOACTIVATE | SWP_NOMOVE | SWP_NOOWNERZORDER | SWP_NOSIZE);

        OnPaint(m_window);
    }

    LRESULT WndProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) noexcept
    {
        LRESULT result;
        auto handled = DwmDefWindowProc(window, message, wParam, lParam, &result);
        if (handled)
        {
            return result;
        }

        switch (message)
        {
            HANDLE_MSG(window, WM_NCCALCSIZE, OnCalcNonClientSize);
            HANDLE_MSG(window, WM_CREATE, OnCreate);
            HANDLE_MSG(window, WM_WINDOWPOSCHANGING, WindowPosChanging);
            HANDLE_MSG(window, WM_DWMNCRENDERINGCHANGED, DwmNonClientRenderingChanged);
            HANDLE_MSG(window, WM_DWMCOLORIZATIONCOLORCHANGED, OnDwmColorizationColorChanged);
            HANDLE_MSG(window, WM_PAINT, OnPaint);
            HANDLE_MSG(window, WM_LBUTTONDOWN, OnLButtonDown);
            HANDLE_MSG(window, WM_RBUTTONDOWN, OnRButtonDown);
            HANDLE_MSG(window, WM_NCLBUTTONDOWN, OnNcLButtonDown);
            HANDLE_MSG(window, WM_MOUSEMOVE, OnMouseMove);
            HANDLE_MSG(window, WM_ERASEBKGND, OnEraseBackground);
            HANDLE_MSG(window, WM_MOUSELEAVE, OnMouseLeave);

        default:
            return DefWindowProcW(window, message, wParam, lParam);
        }
    }

    UINT OnCalcNonClientSize(HWND hwnd, BOOL calc, NCCALCSIZE_PARAMS* info)
    {
        if (!calc)
        {
            return FORWARD_WM_NCCALCSIZE(hwnd, calc, info, DefWindowProcW);
        }

        if (GetWindowLong(hwnd, GWL_STYLE) & WS_CAPTION)
        {
            auto xBorder = GetSystemMetricsForDpi(SM_CXFRAME, m_dpi) + GetSystemMetricsForDpi(SM_CXPADDEDBORDER, m_dpi);
            auto yBorder = GetSystemMetricsForDpi(SM_CYFRAME, m_dpi) + GetSystemMetricsForDpi(SM_CYEDGE, m_dpi);

            auto& coordinates = info->rgrc[0];
            coordinates.left += xBorder;
            coordinates.right -= xBorder;
            coordinates.bottom -= yBorder;
        }

        return 0;
    }

    virtual bool OnCreate(HWND hwnd, LPCREATESTRUCT)
    {
        // Disable transitions
        BOOL disable = TRUE;
        DwmSetWindowAttribute(hwnd, DWMWA_TRANSITIONS_FORCEDISABLED, &disable, sizeof(disable));

        return true;
    }

    void OnDwmColorizationColorChanged(HWND hwnd, DWORD newColorizationColor, BOOL isBlendedWithOpacity)
    {
        // Post WM_PAINT
        RedrawWindow(hwnd, NULL, NULL, RDW_INTERNALPAINT);
    }

    void DwmNonClientRenderingChanged(HWND hwnd, BOOL enabled)
    {
        Rect newWindowRect = m_zone;

        RECT windowRect{};
        ::GetWindowRect(hwnd, &windowRect);

        // Take care of borders
        RECT frameRect{};
        if (SUCCEEDED(DwmGetWindowAttribute(hwnd, DWMWA_EXTENDED_FRAME_BOUNDS, &frameRect, sizeof(frameRect))))
        {
            LONG leftMargin = frameRect.left - windowRect.left;
            LONG rightMargin = frameRect.right - windowRect.right;
            LONG bottomMargin = frameRect.bottom - windowRect.bottom;

            newWindowRect.get()->left -= leftMargin;
            newWindowRect.get()->right -= rightMargin;
            newWindowRect.get()->bottom -= bottomMargin;

            SetWindowPos(hwnd, nullptr, newWindowRect.left(), newWindowRect.top(), newWindowRect.width(), newWindowRect.height(), SWP_NOACTIVATE | SWP_NOZORDER);
        }
    }

    virtual void OnLButtonDown(HWND hwnd, BOOL doubleClick, int x, int y, UINT keyFlags) = 0;

    virtual void OnRButtonDown(HWND hwnd, BOOL doubleClick, int x, int y, UINT keyFlags)
    {
    }

    void SelectZoneWindow(HWND window)
    {
        if (!window)
        {
            return;
        }

        FancyZonesWindowUtils::SwitchToWindow(window);
        m_zoneCurrentWindow = window;

        HWND windowBeforeTheZoneTitleBar = GetDestinedWindowBeforeTheZoneTitleBar();
        SetWindowPos(m_window, windowBeforeTheZoneTitleBar, 0, 0, 0, 0, SWP_NOACTIVATE | SWP_NOMOVE | SWP_NOOWNERZORDER | SWP_NOSIZE);
        RedrawWindow(m_window, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW);
    }

    void OnNcLButtonDown(HWND hwnd, BOOL doubleClick, int hitTest, int x, int y)
    {
        POINT point{ x, y };
        ScreenToClient(hwnd, &point);
        OnLButtonDown(hwnd, doubleClick, point.x, point.y, 0);
    }

    virtual void OnMouseMove(HWND hwnd, int x, int y, UINT keyFlags)
    {
    }

    virtual void OnMouseLeave(HWND hwnd)
    {
    }

    virtual void OnPaint(HWND hwnd) = 0;

    BOOL OnEraseBackground(HWND hwnd, HDC hdc)
    {
        return TRUE;
    }

    void UpdateZoneWindows(std::vector<HWND> zoneWindows) override
    {
        m_zoneWindows = zoneWindows;
        ReadjustPos();
    }

    BOOL WindowPosChanging(HWND hwnd, WINDOWPOS* pos)
    {
        if ((pos->flags & SWP_NOZORDER) == 0)
        {
            pos->hwndInsertAfter = GetDestinedWindowBeforeTheZoneTitleBar();
        }

        pos->flags |= SWP_NOACTIVATE;

        return true;
    }

    int GetScale() const
    {
        auto scale = GetSystemMetricsForDpi(SM_CYHSCROLL, m_dpi);
        if (scale > m_zone.height())
        {
            scale = 0;
        }

        return scale;
    }

    bool m_isAboveZone;
    UINT m_dpi;
    Rect m_zone;
    std::vector<HWND> m_zoneWindows;
    HWND m_zoneCurrentWindow;
    Window m_window;
    std::function<void(HWND)> m_removeWindowFromZoneCallback;

public:
    void Show(bool show) override {}

    Rect GetZoneRect() const override { return m_zone; }

    UINT GetDpi() const override { return m_dpi; }
};

class SlimZoneTitleBar : public VisibleZoneTitleBar
{
public:
    SlimZoneTitleBar(HINSTANCE hinstance, Rect zone, UINT dpi, bool isAboveZone) noexcept :
        VisibleZoneTitleBar(isAboveZone, zone, dpi),
        m_height(GetScale())
    {
        Rect rect(zone.position(), zone.width(), m_height);
        Init(hinstance, rect, WS_POPUP, WS_EX_TOOLWINDOW);
    }

    Rect GetInlineFrame() const override
    {
        auto rect = m_zone;
        rect.get()->top += m_height;
        return rect;
    }

protected:
    bool OnCreate(HWND hwnd, LPCREATESTRUCT)
    {
        m_drawing.Init(hwnd);

        return true;
    }

    void OnLButtonDown(HWND hwnd, BOOL doubleClick, int x, int y, UINT keyFlags)
    {
        auto len = m_height;
        if (len == 0)
        {
            return;
        }

        auto i = x / len;

        if (i >= 0 && i < m_zoneWindows.size())
        {
            SelectZoneWindow(m_zoneWindows[i]);
        }
    }

    void OnPaint(HWND hwnd) override
    {
        ZoneTitleBarColors colors;

        PAINTSTRUCT paint;
        BeginPaint(m_window, &paint);

        if (m_drawing)
        {
            m_drawing.BeginDraw(colors.backColor);

            Render(colors);

            m_drawing.EndDraw();
        }

        EndPaint(m_window, &paint);
    }

    virtual void Render(ZoneTitleBarColors& colors) = 0;

protected:
    int m_height;
    Drawing m_drawing;
};

class NumbersZoneTitleBar : public SlimZoneTitleBar
{
public:
    NumbersZoneTitleBar(HINSTANCE hinstance, Rect zone, UINT dpi, bool isAboveZone) noexcept :
        SlimZoneTitleBar(hinstance, zone, dpi, isAboveZone)
    {
    }

    void Render(ZoneTitleBarColors& colors) override
    {
        auto len = (FLOAT)m_height;

        auto textFormat = m_drawing.CreateTextFormat(L"Segoe ui", len * .7f);
        for (auto i = 0; i < m_zoneWindows.size(); ++i)
        {
            auto rect = D2D1::Rect(len * i, .0f, len * (i + 1), len);

            if (m_zoneWindows[i] == m_zoneCurrentWindow)
            {
                m_drawing.FillRectangle(rect, colors.highlightFrameColor);
                m_drawing.DrawTextW(std::to_wstring(i + 1), textFormat.get(), rect, colors.highlightTextColor);
            }
            else
            {
                m_drawing.FillRectangle(rect, colors.frameColor);
                m_drawing.DrawTextW(std::to_wstring(i + 1), textFormat.get(), rect, colors.textColor);
            }
        }
    }
};

class IconsZoneTitleBar : public SlimZoneTitleBar
{
public:
    IconsZoneTitleBar(HINSTANCE hinstance, Rect zone, UINT dpi, bool isAboveZone) noexcept :
        SlimZoneTitleBar(hinstance, zone, dpi, isAboveZone)
    {
    }

    void Render(ZoneTitleBarColors& colors) override
    {
        auto len = (FLOAT)m_height;

        for (auto i = 0; i < m_zoneWindows.size(); ++i)
        {
            constexpr float p = .15f;
            auto iconRect = D2D1::Rect(len * (i + p), len * p, len * (i + 1 - p), len * (1 - p));
            DrawWindowIcon(m_drawing, iconRect, m_zoneWindows[i]);

            constexpr float s = p * .7f;
            auto strokeRect = D2D1::Rect(len * (i + .5f * s), len * .5f * s, len * (i + 1 - .5f * s), len * (1 - .5f * s));
            auto color = m_zoneWindows[i] == m_zoneCurrentWindow ? colors.highlightFrameColor : colors.frameColor;
            m_drawing.DrawRoundedRectangle(strokeRect, color, len * s);
        }
    }
};

class ThickZoneTitleBar : public VisibleZoneTitleBar
{
protected:
    static constexpr int c_style = WS_OVERLAPPED | WS_CAPTION | WS_THICKFRAME | WS_CLIPCHILDREN;
    static constexpr int c_exStyle = WS_EX_NOREDIRECTIONBITMAP;

    virtual float GetTabWidth() const
    {
        return m_height * GetWidthFactor();
    }

    float GetWidthFactor() const
    {
        constexpr int c_widthFactor = 4;

        auto left = float(m_zone.width() - 2 * m_height);
        auto amount = m_zoneWindows.size();

        if (amount == 0 || left <= 0)
        {
            return 0;
        }

        auto factor = (left / amount) / m_height;
        return min(factor, c_widthFactor);
    }

public:
    ThickZoneTitleBar(HINSTANCE hinstance, Rect zone, UINT dpi, bool isAboveZone, std::function<void(HWND)> removeWindowFromZoneCallback = {}, int customHeight = 0) noexcept :
        VisibleZoneTitleBar(isAboveZone, zone, dpi, std::move(removeWindowFromZoneCallback)),
        m_hiddenWindow(hinstance)
    {
        RECT rect{};
        AdjustWindowRectExForDpi(&rect, c_style, FALSE, c_exStyle, m_dpi);

        auto height = -rect.top;
        if (customHeight > 0)
        {
            height = MulDiv(customHeight, m_dpi, 96);
        }

        m_height = height > zone.height() ? 0 : height;

        Init(hinstance, zone, c_style, c_exStyle);
    }

    Rect GetInlineFrame() const override
    {
        auto rect = m_zone;
        rect.get()->top += m_height;
        return rect;
    }

protected:
    bool OnCreate(HWND hwnd, LPCREATESTRUCT createStruct)
    {
        VisibleZoneTitleBar::OnCreate(hwnd, createStruct);

        // Hide from taskbar
        m_hiddenWindow.HideWindowFromTaskbar(hwnd);

        // Extend frame (twice the size if not overlay)
        int height = m_isAboveZone ? m_height : 2 * m_height;
        MARGINS margins = { 0, 0, height, 0 };
        DwmExtendFrameIntoClientArea(hwnd, &margins);

        // Update frame
        SetWindowPos(hwnd, nullptr, 0, 0, 0, 0, SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOZORDER);

        // Initialize drawing
        m_drawing.Init(hwnd);

        return true;
    }

    void OnLButtonDown(HWND hwnd, BOOL doubleClick, int x, int y, UINT keyFlags)
    {
        auto len = m_height;
        if (len == 0 || x < len)
        {
            return;
        }

        const auto tabWidth = GetTabWidth();
        if (tabWidth <= 0)
        {
            return;
        }

        auto i = int((x - len) / tabWidth);

        if (i >= 0 && i < m_zoneWindows.size())
        {
            SelectZoneWindow(m_zoneWindows[i]);
        }
    }

protected:
    HiddenWindow m_hiddenWindow;
    int m_height;
    CompositionDrawing m_drawing;
};

class TabsZoneTitleBar : public ThickZoneTitleBar
{
public:
    TabsZoneTitleBar(HINSTANCE hinstance, Rect zone, UINT dpi, bool isAboveZone, std::function<void(HWND)> removeWindowFromZoneCallback = {}) noexcept :
        ThickZoneTitleBar(hinstance, zone, dpi, isAboveZone, std::move(removeWindowFromZoneCallback), FancyZonesSettings::settings().tabBarHeight)
    {
        ShowWindow(m_window, SW_HIDE);
    }

    Rect GetInlineFrame() const override
    {
        return m_zoneWindows.size() < 2 ? m_zone : ThickZoneTitleBar::GetInlineFrame();
    }

protected:
    void UpdateZoneWindows(std::vector<HWND> zoneWindows) override
    {
        FinishRename(false);
        m_zoneWindows = std::move(zoneWindows);
        std::erase_if(tabTitleOverrides, [](const auto& entry) { return !IsWindow(entry.first); });
        if (m_hoveredTabIndex >= static_cast<int>(m_zoneWindows.size()))
        {
            m_hoveredTabIndex = -1;
        }

        if (m_zoneWindows.size() < 2)
        {
            m_zoneCurrentWindow = NULL;
            m_hoveredTabIndex = -1;
            m_trackingMouseLeave = false;
            ShowWindow(m_window, SW_HIDE);
            return;
        }

        ShowWindow(m_window, SW_SHOWNOACTIVATE);
        ReadjustPos();
    }

    float GetTabWidth() const override
    {
        if (m_zoneWindows.size() < 2)
        {
            return 0;
        }

        // Account for the window frame border on the right side (WS_THICKFRAME)
        // so the rightmost tab's close button is not clipped at the zone edge.
        const auto frameBorder = GetSystemMetricsForDpi(SM_CXFRAME, m_dpi) + GetSystemMetricsForDpi(SM_CXPADDEDBORDER, m_dpi);
        const auto availableWidth = (std::max)(0, m_zone.width() - m_height - frameBorder);
        if (FancyZonesSettings::settings().tabBarFillZoneWidth)
        {
            return float(availableWidth) / m_zoneWindows.size();
        }

        return float(MulDiv(FancyZonesSettings::settings().tabBarTabWidth, m_dpi, 96));
    }

    void OnPaint(HWND hwnd) override
    {
        constexpr DWORD DWMWA_USE_IMMERSIVE_DARK_MODE = 20;

        BOOL isDarkMode = WindowsColors::is_dark_mode();
        if (FAILED(DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &isDarkMode, sizeof(isDarkMode))))
        {
            isDarkMode = false;
        }

        ZoneTitleBarColors colors(isDarkMode);
        const auto tabFocusColor = ParseHexColor(FancyZonesSettings::settings().tabBarFocusColor);
        const auto tabUnfocusedColor = ParseHexColor(FancyZonesSettings::settings().tabBarUnfocusedColor);
        const auto tabFocusedTextColor = ParseHexColor(FancyZonesSettings::settings().tabBarFocusedTextColor);
        const auto tabUnfocusedTextColor = ParseHexColor(FancyZonesSettings::settings().tabBarUnfocusedTextColor);
        const auto closeButtonColor = ParseHexColor(FancyZonesSettings::settings().tabBarCloseButtonColor);
        const auto closeButtonBackColor = ParseHexColor(FancyZonesSettings::settings().tabBarCloseButtonBackgroundColor);
        const auto closeButtonShape = FancyZonesSettings::settings().tabBarCloseButtonBackgroundShape;
        const auto iconHorizontalSpacing = static_cast<float>(FancyZonesSettings::settings().tabBarIconHorizontalSpacing);
        const auto iconVerticalSpacing = static_cast<float>(FancyZonesSettings::settings().tabBarIconVerticalSpacing);
        const auto iconLeftSpacing = float(MulDiv(FancyZonesSettings::settings().tabBarIconLeftSpacing, m_dpi, 96));
        const auto tabCornerRadius = float(MulDiv(FancyZonesSettings::settings().tabBarCornerRadius, m_dpi, 96));

        PAINTSTRUCT paint;
        BeginPaint(m_window, &paint);

        auto captionHeight = GetSystemMetricsForDpi(SM_CYCAPTION, m_dpi);

        auto zoneCurrentWindow = m_zoneCurrentWindow;

        NONCLIENTMETRICS metrics{};
        metrics.cbSize = sizeof(metrics);
        SystemParametersInfoForDpi(SPI_GETNONCLIENTMETRICS, sizeof(NONCLIENTMETRICS), &metrics, 0, m_dpi);
        TCHAR text[100]{};

        if (m_drawing)
        {
            m_drawing.BeginDraw();

            {
                const auto tabWidth = GetTabWidth();
                auto textFormat = m_drawing.CreateTextFormat(
                    metrics.lfCaptionFont.lfFaceName,
                    float(MulDiv(FancyZonesSettings::settings().tabBarTextSize, m_dpi, 96)),
                    (DWRITE_FONT_WEIGHT)metrics.lfCaptionFont.lfWeight);

                for (auto i = 0; i < m_zoneWindows.size(); ++i)
                {
                    auto xOffset = m_height + tabWidth * i;
                    auto yOffset = 0;

                    // Only a horizontal gap separates adjacent tabs; tabs are flat on the
                    // bottom and extend all the way down to the window's edge (no bottom margin),
                    // with only the top-left/top-right corners rounded.
                    auto horizontalMargin = (m_height - captionHeight) * .4f;
                    auto backWidth = tabWidth - 2 * horizontalMargin;
                    auto backRect = D2D1::RectF(
                        float(xOffset + horizontalMargin),
                        float(yOffset),
                        float(xOffset + horizontalMargin + backWidth),
                        float(m_height));
                    const auto isSelectedTab = m_zoneWindows[i] == zoneCurrentWindow;
                    m_drawing.FillTopRoundedRectangle(backRect, isSelectedTab ? tabFocusColor : tabUnfocusedColor, tabCornerRadius);

                    const auto contentHeight = float(m_height);
                    const auto iconSize = min(float(MulDiv(FancyZonesSettings::settings().tabBarIconSize, m_dpi, 96)), contentHeight);
                    const auto iconLeft = float(xOffset + horizontalMargin) + iconLeftSpacing;
                    const auto iconTop = ((float(m_height) - iconSize) / 2) + (iconVerticalSpacing / 2.f);
                    auto iconRect = D2D1::RectF(
                        iconLeft,
                        iconTop,
                        iconLeft + iconSize,
                        iconTop + iconSize);
                    DrawWindowIcon(m_drawing, iconRect, m_zoneWindows[i]);

                    if (textFormat)
                    {
                        const auto textLeft = iconLeft + iconSize + iconHorizontalSpacing;
                        const auto textRight = float(xOffset + tabWidth - horizontalMargin);
                        auto textRect = D2D1::RectF(
                            textLeft,
                            float(yOffset + (iconVerticalSpacing / 2.f)),
                            max(textLeft, textRight),
                            float(yOffset + m_height));

                        const auto titleOverride = tabTitleOverrides.find(m_zoneWindows[i]);
                        if (titleOverride != tabTitleOverrides.end())
                        {
                            m_drawing.DrawTextTrim(titleOverride->second.c_str(), textFormat.get(), textRect, isSelectedTab ? tabFocusedTextColor : tabUnfocusedTextColor);
                        }
                        else
                        {
                            text[0] = TEXT('\0');
                            GetWindowText(m_zoneWindows[i], text, ARRAYSIZE(text));
                            m_drawing.DrawTextTrim(text, textFormat.get(), textRect, isSelectedTab ? tabFocusedTextColor : tabUnfocusedTextColor);
                        }
                    }

                    if (m_hoveredTabIndex == static_cast<int>(i))
                    {
                        const auto closeButtonRect = GetCloseButtonRect(static_cast<int>(i), tabWidth);
                        if (closeButtonShape != TabBarCloseButtonShape::None)
                        {
                            if (closeButtonShape == TabBarCloseButtonShape::Circle)
                            {
                                const auto center = D2D1::Point2F((closeButtonRect.left + closeButtonRect.right) / 2.f, (closeButtonRect.top + closeButtonRect.bottom) / 2.f);
                                const auto radiusX = (closeButtonRect.right - closeButtonRect.left) / 2.f;
                                const auto radiusY = (closeButtonRect.bottom - closeButtonRect.top) / 2.f;
                                m_drawing.FillEllipse(D2D1::Ellipse(center, radiusX, radiusY), closeButtonBackColor);
                            }
                            else
                            {
                                m_drawing.FillRoundedRectangle(closeButtonRect, closeButtonBackColor, 0.2f);
                            }
                        }

                        const auto closeTextRect = D2D1::RectF(
                            closeButtonRect.left,
                            closeButtonRect.top - 1.0f,
                            closeButtonRect.right,
                            closeButtonRect.bottom);
                        if (!m_closeTextFormat)
                        {
                            m_closeTextFormat = m_drawing.CreateTextFormat(
                                metrics.lfCaptionFont.lfFaceName,
                                float(MulDiv(FancyZonesSettings::settings().tabBarTextSize, m_dpi, 96)),
                                DWRITE_FONT_WEIGHT_BOLD);
                        }

                        if (m_closeTextFormat)
                        {
                            m_drawing.DrawTextW(L"X", m_closeTextFormat.get(), closeTextRect, closeButtonColor);
                        }
                    }
                }
            }

            m_drawing.EndDraw();
        }

        EndPaint(m_window, &paint);
    }

    void OnLButtonDown(HWND hwnd, BOOL doubleClick, int x, int y, UINT keyFlags)
    {
        auto len = m_height;
        if (len == 0 || x < len)
        {
            return;
        }

        const auto tabWidth = GetTabWidth();
        if (tabWidth <= 0)
        {
            return;
        }

        const auto tabIndex = int((x - len) / tabWidth);
        if (tabIndex < 0 || tabIndex >= static_cast<int>(m_zoneWindows.size()))
        {
            return;
        }

        if (HitTestCloseButton(tabIndex, x, y))
        {
            const auto windowToRemove = m_zoneWindows[tabIndex];
            if (m_removeWindowFromZoneCallback)
            {
                m_removeWindowFromZoneCallback(windowToRemove);
            }
            return;
        }

        SelectZoneWindow(m_zoneWindows[tabIndex]);
    }

    void OnRButtonDown(HWND hwnd, BOOL doubleClick, int x, int y, UINT keyFlags) override
    {
        const auto tabIndex = HitTestTabIndex(x);
        if (tabIndex < 0)
        {
            return;
        }

        BeginRename(tabIndex);
    }

    void OnMouseMove(HWND hwnd, int x, int y, UINT keyFlags) override
    {
        if (!m_trackingMouseLeave)
        {
            TRACKMOUSEEVENT mouseTrack{ sizeof(TRACKMOUSEEVENT), TME_LEAVE, hwnd, 0 };
            if (TrackMouseEvent(&mouseTrack))
            {
                m_trackingMouseLeave = true;
            }
        }

        const auto hoveredTabIndex = HitTestTabIndex(x);
        if (hoveredTabIndex != m_hoveredTabIndex)
        {
            m_hoveredTabIndex = hoveredTabIndex;
            RedrawWindow(hwnd, nullptr, nullptr, RDW_INVALIDATE);
        }
    }

    void OnMouseLeave(HWND hwnd) override
    {
        m_trackingMouseLeave = false;
        if (m_hoveredTabIndex != -1)
        {
            m_hoveredTabIndex = -1;
            RedrawWindow(hwnd, nullptr, nullptr, RDW_INVALIDATE);
        }
    }

private:
    static LRESULT CALLBACK RenameEditProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
    {
        auto titleBar = reinterpret_cast<TabsZoneTitleBar*>(GetWindowLongPtr(window, GWLP_USERDATA));
        if (!titleBar)
        {
            return DefWindowProc(window, message, wParam, lParam);
        }

        if (message == WM_KEYDOWN && wParam == VK_RETURN)
        {
            titleBar->FinishRename(true);
            return 0;
        }

        if (message == WM_KEYDOWN && wParam == VK_ESCAPE)
        {
            titleBar->FinishRename(false);
            return 0;
        }

        if (message == WM_KILLFOCUS)
        {
            titleBar->FinishRename(true);
            return 0;
        }

        return CallWindowProc(titleBar->m_renameEditProc, window, message, wParam, lParam);
    }

    void BeginRename(int tabIndex)
    {
        FinishRename(true);

        const auto tabWidth = GetTabWidth();
        if (tabWidth <= 0 || tabIndex < 0 || tabIndex >= static_cast<int>(m_zoneWindows.size()))
        {
            return;
        }

        const auto targetWindow = m_zoneWindows[tabIndex];
        std::wstring title;
        if (const auto titleOverride = tabTitleOverrides.find(targetWindow); titleOverride != tabTitleOverrides.end())
        {
            title = titleOverride->second;
        }
        else
        {
            const auto titleLength = GetWindowTextLength(targetWindow);
            std::vector<wchar_t> titleBuffer(static_cast<size_t>(titleLength) + 1);
            GetWindowText(targetWindow, titleBuffer.data(), static_cast<int>(titleBuffer.size()));
            title = titleBuffer.data();
        }

        const auto xOffset = m_height + tabWidth * tabIndex;
        const auto horizontalMargin = (m_height - GetSystemMetricsForDpi(SM_CYCAPTION, m_dpi)) * .4f;
        const auto editLeft = static_cast<int>(xOffset + horizontalMargin);
        const auto editWidth = (std::max)(1, static_cast<int>(tabWidth - 2 * horizontalMargin));
        m_renameEdit = CreateWindowExW(
            WS_EX_CLIENTEDGE,
            L"EDIT",
            title.c_str(),
            WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
            editLeft,
            0,
            editWidth,
            m_height,
            m_window,
            nullptr,
            nullptr,
            nullptr);
        if (!m_renameEdit)
        {
            return;
        }

        m_renameTabIndex = tabIndex;
        SetWindowLongPtr(m_renameEdit, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
        m_renameEditProc = reinterpret_cast<WNDPROC>(SetWindowLongPtr(m_renameEdit, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(RenameEditProc)));
        SendMessage(m_renameEdit, WM_SETFONT, reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);
        SendMessage(m_renameEdit, EM_SETSEL, 0, -1);
        SetWindowPos(m_renameEdit, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
        RedrawWindow(m_renameEdit, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_UPDATENOW);
        SetFocus(m_renameEdit);
    }

    void FinishRename(bool commit)
    {
        if (!m_renameEdit)
        {
            return;
        }

        const auto edit = m_renameEdit;
        const auto tabIndex = m_renameTabIndex;
        if (commit && tabIndex >= 0 && tabIndex < static_cast<int>(m_zoneWindows.size()))
        {
            const auto titleLength = GetWindowTextLength(edit);
            std::vector<wchar_t> titleBuffer(static_cast<size_t>(titleLength) + 1);
            GetWindowText(edit, titleBuffer.data(), static_cast<int>(titleBuffer.size()));

            const auto targetWindow = m_zoneWindows[tabIndex];
            if (titleLength == 0)
            {
                tabTitleOverrides.erase(targetWindow);
            }
            else
            {
                tabTitleOverrides[targetWindow] = titleBuffer.data();
            }
        }

        m_renameEdit = nullptr;
        m_renameTabIndex = -1;
        SetWindowLongPtr(edit, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(m_renameEditProc));
        m_renameEditProc = nullptr;
        DestroyWindow(edit);
        RedrawWindow(m_window, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW);
    }

    int HitTestTabIndex(int x) const
    {
        if (m_height == 0 || x < m_height)
        {
            return -1;
        }

        const auto tabWidth = GetTabWidth();
        if (tabWidth <= 0)
        {
            return -1;
        }

        const auto tabIndex = int((x - m_height) / tabWidth);
        if (tabIndex < 0 || tabIndex >= static_cast<int>(m_zoneWindows.size()))
        {
            return -1;
        }

        return tabIndex;
    }

    D2D1_RECT_F GetCloseButtonRect(int tabIndex, float tabWidth) const
    {
        const auto xOffset = m_height + tabWidth * tabIndex;
        const auto closeSpacing = float(MulDiv(FancyZonesSettings::settings().tabBarCloseButtonSpacing, m_dpi, 96));
        const auto closeSize = (std::max)(6.0f, float(m_height) * 0.4f);
        const auto closeRight = float(xOffset + tabWidth) - closeSpacing;
        const auto closeLeft = closeRight - closeSize;
        const auto closeTop = (float(m_height) - closeSize) * 0.5f;

        return D2D1::RectF(closeLeft, closeTop, closeRight, closeTop + closeSize);
    }

    bool HitTestCloseButton(int tabIndex, int x, int y) const
    {
        const auto tabWidth = GetTabWidth();
        if (tabWidth <= 0 || tabIndex < 0 || tabIndex >= static_cast<int>(m_zoneWindows.size()))
        {
            return false;
        }

        const auto closeRect = GetCloseButtonRect(tabIndex, tabWidth);

        return x >= closeRect.left && x <= closeRect.right && y >= closeRect.top && y <= closeRect.bottom;
    }

    int m_hoveredTabIndex = -1;
    bool m_trackingMouseLeave = false;
    HWND m_renameEdit = nullptr;
    int m_renameTabIndex = -1;
    WNDPROC m_renameEditProc = nullptr;
    winrt::com_ptr<IDWriteTextFormat> m_closeTextFormat;
};

class LabelsZoneTitleBar : public ThickZoneTitleBar
{
public:
    LabelsZoneTitleBar(HINSTANCE hinstance, Rect zone, UINT dpi, bool isAboveZone) noexcept :
        ThickZoneTitleBar(hinstance, zone, dpi, isAboveZone)
    {
    }

protected:
    void OnPaint(HWND hwnd) override
    {
        constexpr DWORD DWMWA_USE_IMMERSIVE_DARK_MODE = 20;

        BOOL isDarkMode = WindowsColors::is_dark_mode();
        if (FAILED(DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &isDarkMode, sizeof(isDarkMode))))
        {
            isDarkMode = false;
        }

        ZoneTitleBarColors colors(isDarkMode);

        PAINTSTRUCT paint;
        BeginPaint(m_window, &paint);

        auto captionHeight = GetSystemMetricsForDpi(SM_CYCAPTION, m_dpi);

        auto zoneCurrentWindow = m_zoneCurrentWindow;

        NONCLIENTMETRICS metrics{};
        metrics.cbSize = sizeof(metrics);
        SystemParametersInfoForDpi(SPI_GETNONCLIENTMETRICS, sizeof(NONCLIENTMETRICS), &metrics, 0, m_dpi);
        TCHAR text[100]{};

        if (m_drawing)
        {
            m_drawing.BeginDraw();

            {
                auto widthFactor = GetWidthFactor();
                auto textFormat = m_drawing.CreateTextFormat(
                    metrics.lfCaptionFont.lfFaceName,
                    float(-metrics.lfCaptionFont.lfHeight),
                    (DWRITE_FONT_WEIGHT)metrics.lfCaptionFont.lfWeight);

                auto textColor = isDarkMode ? colors.frameColor : Drawing::ConvertColor(WindowsColors::get_gray_text_color());
                auto highlightTextColor = isDarkMode ? colors.highlightTextColor : colors.textColor;

                for (auto i = 0; i < m_zoneWindows.size(); ++i)
                {
                    auto xOffset = m_height * (1 + widthFactor * i);
                    auto yOffset = 0;
                    auto xSize = m_height * widthFactor;
                    auto ySize = m_height;

                    auto sepHeight = captionHeight * .7f;
                    auto sepWidth = sepHeight * .05f;
                    auto sepMargin = (ySize - sepHeight) * .5f;
                    if (i != 0)
                    {
                        auto sepRect = D2D1::RectF(
                            float(xOffset - sepWidth * .5f),
                            float(yOffset + sepMargin),
                            float(xOffset + sepWidth * .5f),
                            float(yOffset + ySize - sepMargin));
                        m_drawing.FillRectangle(sepRect, textColor);
                    }

                    auto isFrontWindow = m_zoneWindows[i] == zoneCurrentWindow;
                    auto xMargin = sepHeight * .9f;

                    auto iconMargin = (ySize - captionHeight) * .5f;
                    auto iconRect = D2D1::RectF(
                        xOffset + xMargin,
                        yOffset + iconMargin,
                        xOffset + xMargin + captionHeight,
                        yOffset + iconMargin + captionHeight);
                    DrawWindowIcon(m_drawing, iconRect, m_zoneWindows[i], isFrontWindow ? 1.f : .4f);

                    auto xSizeIcon = 0;

                    if (textFormat)
                    {
                        auto textMargin = (ySize - captionHeight) * .5f;
                        auto textRect = D2D1::RectF(
                            float(xOffset + xMargin + captionHeight * 1.1f),
                            float(yOffset + textMargin),
                            float(xOffset + xSize - xMargin),
                            float(yOffset + ySize - textMargin));

                        text[0] = TEXT('\0');
                        GetWindowText(m_zoneWindows[i], text, ARRAYSIZE(text));
                        m_drawing.DrawTextTrim(text, textFormat.get(), textRect, isFrontWindow ? highlightTextColor : textColor, isFrontWindow);
                    }
                }
            }

            m_drawing.EndDraw();
        }

        EndPaint(m_window, &paint);
    }
};

class AdornZoneTitleBar : public VisibleZoneTitleBar
{
protected:
    static constexpr int c_style = WS_POPUP;
    static constexpr int c_exStyle = WS_EX_TOOLWINDOW | WS_EX_LAYERED;

public:
    AdornZoneTitleBar(HINSTANCE hinstance, Rect zone, UINT dpi) noexcept :
        VisibleZoneTitleBar(true, zone, dpi)
    {
        Init(hinstance, zone, c_style, c_exStyle);
    }

    Rect GetInlineFrame() const override
    {
        return m_zone;
    }

    Rect GetZoneRect() const override { return m_zone; }

    UINT GetDpi() const override { return m_dpi; }

protected:
    bool OnCreate(HWND hwnd, LPCREATESTRUCT createStruct) override
    {
        VisibleZoneTitleBar::OnCreate(hwnd, createStruct);

        // Initialize drawing
        m_drawing.Init(hwnd);

        // Set layered window
        SetLayeredWindowAttributes(hwnd, RGB(0, 0, 0), 0, LWA_COLORKEY);

        return true;
    }

protected:
    Drawing m_drawing;
};

class PagerZoneTitleBar : public AdornZoneTitleBar
{
public:
    PagerZoneTitleBar(HINSTANCE hinstance, Rect zone, UINT dpi) noexcept :
        AdornZoneTitleBar(hinstance, zone, dpi)
    {
    }

protected:
    void OnLButtonDown(HWND hwnd, BOOL doubleClick, int x, int y, UINT keyFlags) override
    {
        auto q = (FLOAT)GetSystemMetricsForDpi(SM_CYHSCROLL, m_dpi);
        if (q == 0 || m_zoneWindows.size() == 0)
        {
            return;
        }

        auto h = m_zone.height();
        auto w = m_zone.width();

        auto m = q * .25f;
        auto l = h - (q + m);

        if (y < l)
        {
            auto ptr = std::find(m_zoneWindows.begin(), m_zoneWindows.end(), m_zoneCurrentWindow);
            if (ptr == m_zoneWindows.end())
            {
                return;
            }

            auto i = std::distance(m_zoneWindows.begin(), ptr);

            auto last = m_zoneWindows.size() - 1;
            if (x < w / 2)
            {
                auto prev = (i == 0) ? last : (i - 1);
                FancyZonesWindowUtils::SwitchToWindow(m_zoneWindows[prev]);
            }
            else
            {
                auto next = (i == last) ? 0 : (i + 1);
                FancyZonesWindowUtils::SwitchToWindow(m_zoneWindows[next]);
            }
        }
        else
        {
            auto t = m_zoneWindows.size() * q;
            auto o = (w - t) * .5f;

            auto i = (int)((x - o) / q);
            SwitchToWindowByIndex(m_zoneWindows, i);
        }
    }

    void OnPaint(HWND hwnd) override
    {
        PAINTSTRUCT paint;
        BeginPaint(m_window, &paint);

        if (m_drawing)
        {
            m_drawing.Drawing::BeginDraw(D2D1::ColorF(0.f, 0.f, 0.f, 0.f));

            auto color = Drawing::ConvertColor(WindowsColors::get_gray_text_color());

            auto q = (FLOAT)GetSystemMetricsForDpi(SM_CYHSCROLL, m_dpi);

            auto h = m_zone.height();
            auto w = m_zone.width();

            auto m = q * .25f;
            auto l = h - (q + m);

            auto y = q * .7f;
            auto z = q * .3f;
            {
                D2D1_TRIANGLE triangle = {
                    .point1 = D2D1::Point2F(m, l / 2),
                    .point2 = D2D1::Point2F(m + y * .86602540378f, (l / 2) - (y / 2)),
                    .point3 = D2D1::Point2F(m + y * .86602540378f, (l / 2) + (y / 2))
                };

                auto geometry = m_drawing.CreateTriangle(triangle);

                if (geometry)
                {
                    m_drawing.FillGeometry(geometry.get(), color, z * .5f);
                }
            }

            {
                D2D1_TRIANGLE triangle = {
                    .point1 = D2D1::Point2F(w - m, l / 2),
                    .point2 = D2D1::Point2F(w - (m + y * .86602540378f), (l / 2) - (y / 2)),
                    .point3 = D2D1::Point2F(w - (m + y * .86602540378f), (l / 2) + (y / 2))
                };

                auto geometry = m_drawing.CreateTriangle(triangle);

                if (geometry)
                {
                    m_drawing.FillGeometry(geometry.get(), color, z * .5f);
                }
            }

            auto zoneCurrentWindow = m_zoneCurrentWindow;
            auto t = m_zoneWindows.size() * q;
            auto o = (w - t) * .5f;
            for (auto i = 0; i < m_zoneWindows.size(); ++i)
            {
                auto center = D2D1::Point2(o + q * (i + .5f), l + (q * .5f));
                auto radius = q * .25f * (m_zoneWindows[i] == zoneCurrentWindow ? 2.f : 1.f);
                auto circle = D2D1::Ellipse(center, radius, radius);
                m_drawing.FillEllipse(circle, color);
            }

            m_drawing.EndDraw();
        }

        EndPaint(m_window, &paint);
    }
};

class SideZoneTitleBar : public VisibleZoneTitleBar
{
protected:
    static constexpr int c_style = WS_POPUP;
    static constexpr int c_exStyle = WS_EX_TOOLWINDOW | WS_EX_LAYERED;

    int GetWidthFactor() const
    {
        constexpr int c_widthFactor = 2;

        auto q = c_widthFactor * GetSystemMetricsForDpi(SM_CYHSCROLL, m_dpi);

        if (q < 0 || m_zone.width() < 2 * q)
        {
            return 0;
        }

        return q;
    }

public:
    SideZoneTitleBar(HINSTANCE hinstance, Rect zone, UINT dpi, bool isAboveZone) noexcept :
        VisibleZoneTitleBar(isAboveZone, zone, dpi)
    {
        auto q = GetWidthFactor();
        zone.get()->right = zone.left() + q;

        Init(hinstance, zone, c_style, c_exStyle);
    }

    Rect GetInlineFrame() const override
    {
        auto q = GetWidthFactor();

        Rect zone = m_zone;
        zone.get()->left += q;

        return zone;
    }

protected:
    bool OnCreate(HWND hwnd, LPCREATESTRUCT createStruct) override
    {
        VisibleZoneTitleBar::OnCreate(hwnd, createStruct);

        // Initialize drawing
        m_drawing.Init(hwnd);

        // Set layered window
        SetLayeredWindowAttributes(hwnd, RGB(0, 0, 0), 0, LWA_COLORKEY);

        return true;
    }

protected:
    Drawing m_drawing;
};

class ButtonsZoneTitleBar : public SideZoneTitleBar
{
public:
    ButtonsZoneTitleBar(HINSTANCE hinstance, Rect zone, UINT dpi, bool isAboveZone) noexcept :
        SideZoneTitleBar(hinstance, zone, dpi, isAboveZone)
    {
    }

protected:
    void OnLButtonDown(HWND hwnd, BOOL doubleClick, int x, int y, UINT keyFlags) override
    {
        auto q = GetWidthFactor();
        if (q <= 0)
        {
            return;
        }

        auto i = y / q;
        SwitchToWindowByIndex(m_zoneWindows, i);
    }

    void OnPaint(HWND hwnd) override
    {
        PAINTSTRUCT paint;
        BeginPaint(m_window, &paint);

        if (m_drawing)
        {
            ZoneTitleBarColors colors;
            auto q = GetWidthFactor();
            m_drawing.Drawing::BeginDraw(D2D1::ColorF(0.f, 0.f, 0.f, 0.f));

            for (auto i = 0; i < m_zoneWindows.size(); ++i)
            {
                auto m = .1f * q;
                auto rect = D2D1::RectF(0.f + m, i * q + m, q - m, (i + 1) * q - m);

                if (m_zoneWindows[i] == m_zoneCurrentWindow)
                {
                    m_drawing.FillRoundedRectangle(rect, colors.highlightFrameColor, .3f);
                }
                else
                {
                    m_drawing.FillRoundedRectangle(rect, colors.frameColor, .3f);
                }

                m = .17f * q;
                auto iconRect = D2D1::RectF(0.f + m, i * q + m, q - m, (i + 1) * q - m);
                DrawWindowIcon(m_drawing, iconRect, m_zoneWindows[i]);
            }

            m_drawing.EndDraw();
        }

        EndPaint(m_window, &paint);
    }
};

class AutoHideZoneTitleBar : public IZoneTitleBar
{
public:
    AutoHideZoneTitleBar(HINSTANCE hinstance, Rect zone, UINT dpi, ZoneTitleBarStyle style, std::function<void(HWND)> removeWindowFromZoneCallback) :
        m_hinstance(hinstance),
        m_zone(zone),
        m_dpi(dpi),
        m_style(style),
        m_removeWindowFromZoneCallback(std::move(removeWindowFromZoneCallback))
    {
        m_timerId = SetTimer(nullptr, 0, 100, TimerProc);
        if (m_timerId)
        {
            s_timers.emplace(m_timerId, this);
        }
    }

    ~AutoHideZoneTitleBar()
    {
        if (m_timerId)
        {
            s_timers.erase(m_timerId);
            KillTimer(nullptr, m_timerId);
        }
    }

    virtual void Show(bool show) override
    {
        if (show)
        {
            if (!m_zoneTitleBar)
            {
                switch (m_style)
                {
                case ZoneTitleBarStyle::Numbers:
                    m_zoneTitleBar = std::make_unique<NumbersZoneTitleBar>(m_hinstance, m_zone, m_dpi, true);
                    break;

                case ZoneTitleBarStyle::Icons:
                    m_zoneTitleBar = std::make_unique<IconsZoneTitleBar>(m_hinstance, m_zone, m_dpi, true);
                    break;

                case ZoneTitleBarStyle::Tabs:
                    m_zoneTitleBar = std::make_unique<TabsZoneTitleBar>(m_hinstance, m_zone, m_dpi, true, m_removeWindowFromZoneCallback);
                    break;

                case ZoneTitleBarStyle::Labels:
                    m_zoneTitleBar = std::make_unique<LabelsZoneTitleBar>(m_hinstance, m_zone, m_dpi, true);
                    break;

                case ZoneTitleBarStyle::Pager:
                    m_zoneTitleBar = std::make_unique<PagerZoneTitleBar>(m_hinstance, m_zone, m_dpi);
                    break;

                case ZoneTitleBarStyle::Buttons:
                    m_zoneTitleBar = std::make_unique<ButtonsZoneTitleBar>(m_hinstance, m_zone, m_dpi, true);
                    break;

                case ZoneTitleBarStyle::None:
                default:
                    m_zoneTitleBar = std::make_unique<NoZoneTitleBar>(m_zone);
                    break;
                }

                m_zoneTitleBar->UpdateZoneWindows(m_zoneWindows);
            }
        }
        else
        {
            m_zoneTitleBar.reset();
        }
    }

    virtual void UpdateZoneWindows(std::vector<HWND> zoneWindows) override
    {
        m_zoneWindows = zoneWindows;

        if (m_zoneTitleBar)
        {
            m_zoneTitleBar->UpdateZoneWindows(zoneWindows);
        }

        UpdateVisibility();
    }

    virtual void ReadjustPos() override
    {
        if (m_zoneTitleBar)
        {
            m_zoneTitleBar->ReadjustPos();
        }
    }

    Rect GetInlineFrame() const override
    {
        return m_zone;
    }

    Rect GetZoneRect() const override { return m_zone; }

    UINT GetDpi() const override { return m_dpi; }

protected:
    static void CALLBACK TimerProc(HWND, UINT, UINT_PTR timerId, DWORD)
    {
        const auto timer = s_timers.find(timerId);
        if (timer != s_timers.end())
        {
            timer->second->UpdateVisibility();
        }
    }

    void UpdateVisibility()
    {
        POINT cursor{};
        if (!GetCursorPos(&cursor))
        {
            return;
        }

        MONITORINFO monitorInfo{ .cbSize = sizeof(MONITORINFO) };
        const auto monitor = MonitorFromPoint(cursor, MONITOR_DEFAULTTONEAREST);
        const bool show = monitor && GetMonitorInfoW(monitor, &monitorInfo) && cursor.y < monitorInfo.rcMonitor.top + 5;
        if (show != m_isVisible)
        {
            m_isVisible = show;
            Show(show);
        }
    }

    HINSTANCE m_hinstance;
    Rect m_zone;
    UINT m_dpi;
    ZoneTitleBarStyle m_style;
    std::vector<HWND> m_zoneWindows;
    std::function<void(HWND)> m_removeWindowFromZoneCallback;

    std::unique_ptr<IZoneTitleBar> m_zoneTitleBar;
    UINT_PTR m_timerId = 0;
    bool m_isVisible = false;

    inline static std::unordered_map<UINT_PTR, AutoHideZoneTitleBar*> s_timers;
};

std::unique_ptr<IZoneTitleBar> MakeZoneTitleBar(ZoneTitleBarStyle style, HINSTANCE hinstance, Rect zone, UINT dpi, std::function<void(HWND)> removeWindowFromZoneCallback)
{
    bool isAutoHide = ((int)style & (int)ZoneTitleBarStyle::AutoHide) && style != ZoneTitleBarStyle::AutoHide;
    if (isAutoHide)
    {
        return std::make_unique<AutoHideZoneTitleBar>(hinstance, zone, dpi, (ZoneTitleBarStyle)((int)style & ~(int)ZoneTitleBarStyle::AutoHide), std::move(removeWindowFromZoneCallback));
    }

    switch (style)
    {
    case ZoneTitleBarStyle::Numbers:
        return std::make_unique<NumbersZoneTitleBar>(hinstance, zone, dpi, false);

    case ZoneTitleBarStyle::Icons:
        return std::make_unique<IconsZoneTitleBar>(hinstance, zone, dpi, false);

    case ZoneTitleBarStyle::Tabs:
        return std::make_unique<TabsZoneTitleBar>(hinstance, zone, dpi, false, std::move(removeWindowFromZoneCallback));

    case ZoneTitleBarStyle::Labels:
        return std::make_unique<LabelsZoneTitleBar>(hinstance, zone, dpi, false);

    case ZoneTitleBarStyle::Pager:
        return std::make_unique<PagerZoneTitleBar>(hinstance, zone, dpi);

    case ZoneTitleBarStyle::Buttons:
        return std::make_unique<ButtonsZoneTitleBar>(hinstance, zone, dpi, false);

    case ZoneTitleBarStyle::None:
    default:
        return std::make_unique<NoZoneTitleBar>(zone);
    }
}

#pragma warning(pop)
