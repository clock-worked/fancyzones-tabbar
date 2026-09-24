#include "pch.h"
#include "WorkArea.h"

#include <common/logger/logger.h>

#include "FancyZonesData/AppliedLayouts.h"
#include "FancyZonesData/AppZoneHistory.h"
#include "FancyZonesData/CustomLayouts.h"
#include "ZonesOverlay.h"
#include "Settings.h"
#include <FancyZonesLib/FancyZonesWindowProperties.h>
#include <FancyZonesLib/VirtualDesktop.h>
#include <FancyZonesLib/WindowUtils.h>

// disabling warning 4458 - declaration of 'identifier' hides class member
// to avoid warnings from GDI files - can't add winRT directory to external code
// in the Cpp.Build.props
#pragma warning(push)
#pragma warning(disable : 4458)
#include <gdiplus.h>
#pragma warning(pop)

// Non-Localizable strings
namespace NonLocalizable
{
    const wchar_t ToolWindowClassName[] = L"FancyZones_ZonesOverlay";
    const wchar_t ToolWindowName[] = L"FancyZones_ZonesOverlay";
}

using namespace FancyZonesUtils;

namespace
{
    constexpr bool IsTabsTitleBarStyle(ZoneTitleBarStyle style)
    {
        const auto styleValue = static_cast<int>(style);
        return (styleValue & ~static_cast<int>(ZoneTitleBarStyle::AutoHide)) == static_cast<int>(ZoneTitleBarStyle::Tabs);
    }

    // The reason for using this class is the need to call ShowWindow(window, SW_SHOWNORMAL); on each
    // newly created window for it to be displayed properly. The call sometimes has side effects when
    // a fullscreen app is running, and happens when the resolution change event is triggered
    // (e.g. when running some games).
    // This class will serve as a pool of windows for which this call was already done.
    class WindowPool
    {
        std::vector<HWND> m_pool;
        std::mutex m_mutex;

        HWND ExtractWindow()
        {
            std::unique_lock lock(m_mutex);

            if (m_pool.empty())
            {
                return NULL;
            }

            HWND window = m_pool.back();
            m_pool.pop_back();
            return window;
        }

    public:
        HWND NewZonesOverlayWindow(Rect position, HINSTANCE hinstance, WorkArea* owner)
        {
            HWND windowFromPool = ExtractWindow();
            if (windowFromPool == NULL)
            {
                HWND window = CreateWindowExW(WS_EX_TOOLWINDOW, NonLocalizable::ToolWindowClassName, NonLocalizable::ToolWindowName, WS_POPUP, position.left(), position.top(), position.width(), position.height(), nullptr, nullptr, hinstance, owner);
                Logger::info("Creating new ZonesOverlay window, hWnd = {}", (void*)window);
                FancyZonesWindowUtils::MakeWindowTransparent(window);

                // According to ShowWindow docs, we must call it with SW_SHOWNORMAL the first time
                ShowWindow(window, SW_SHOWNORMAL);
                ShowWindow(window, SW_HIDE);
                return window;
            }
            else
            {
                Logger::info("Reusing ZonesOverlay window from pool, hWnd = {}", (void*)windowFromPool);
                SetWindowLongPtrW(windowFromPool, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(owner));
                MoveWindow(windowFromPool, position.left(), position.top(), position.width(), position.height(), TRUE);
                return windowFromPool;
            }
        }

        void FreeZonesOverlayWindow(HWND window)
        {
            Logger::info("Freeing ZonesOverlay window into pool, hWnd = {}", (void*)window);
            SetWindowLongPtrW(window, GWLP_USERDATA, 0);
            ShowWindow(window, SW_HIDE);

            std::unique_lock lock(m_mutex);
            m_pool.push_back(window);
        }

        ~WindowPool()
        {
            for (HWND window : m_pool)
            {
                DestroyWindow(window);
            }
        }
    };

    WindowPool windowPool;
}

WorkArea::WorkArea(HINSTANCE hinstance, const FancyZonesDataTypes::WorkAreaId& uniqueId, const FancyZonesUtils::Rect& workAreaRect) :
    m_uniqueId(uniqueId),
    m_hinstance(hinstance),
    m_workAreaRect(workAreaRect)
{
    WNDCLASSEXW wcex{};
    wcex.cbSize = sizeof(WNDCLASSEX);
    wcex.lpfnWndProc = s_WndProc;
    wcex.hInstance = hinstance;
    wcex.lpszClassName = NonLocalizable::ToolWindowClassName;
    wcex.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    RegisterClassExW(&wcex);
}

WorkArea::~WorkArea()
{
    // Tear down the renderer (joining its background thread) before returning
    // the HWND to the pool. Otherwise, the render thread can still be drawing
    // through m_renderTarget into an HWND that has already been recycled by a
    // subsequent NewZonesOverlayWindow call.
    m_zonesOverlay.reset();
    windowPool.FreeZonesOverlayWindow(m_window);
}

bool WorkArea::Snap(HWND window, const ZoneIndexSet& zones, bool updatePosition)
{
    if (!m_layout || zones.empty())
    {
        return false;
    }

    for (ZoneIndex zone : zones)
    {
        if (static_cast<size_t>(zone) >= m_layout->Zones().size())
        {
            return false;
        }
    }

    m_layoutWindows.Assign(window, zones);
    AppZoneHistory::instance().SetAppLastZones(window, m_uniqueId, m_layout->Id(), zones);

    // Create the title bar before sizing the windows so its inline frame can
    // reserve space for the bar instead of covering the windows' client area.
    UpdateZoneTitleBars();

    if (updatePosition)
    {
        FancyZonesWindowUtils::SaveWindowSizeAndOrigin(window);

        const RECT rect = *GetZoneInlineFrame(zones).get();

        // Resizing all windows in the group is needed when this snap creates
        // a title bar: windows that were already in the zone must also move
        // below it.
        const auto& windows = m_layoutWindows.WindowsByIndexSets().at(zones);
        for (const auto zoneWindow : windows)
        {
            const auto adjustedRect = FancyZonesWindowUtils::AdjustRectForSizeWindowToRect(zoneWindow, rect, m_window, /*rectAlreadyInScreenCoordinates*/ true);
            FancyZonesWindowUtils::SizeWindowToRect(zoneWindow, adjustedRect);
        }

        const auto titleBar = m_zoneTitleBars.find(zones);
        if (titleBar != m_zoneTitleBars.end())
        {
            titleBar->second->ReadjustPos();
        }
    }

    const bool stamped = FancyZonesWindowProperties::StampZoneIndexProperty(window, zones);
    return stamped;
}

bool WorkArea::Unsnap(HWND window)
{
    if (!m_layout)
    {
        return false;
    }

    const auto zones = m_layoutWindows.GetZoneIndexSetFromWindow(window);
    m_layoutWindows.Dismiss(window);
    AppZoneHistory::instance().RemoveAppLastZone(window, m_uniqueId, m_layout->Id());
    FancyZonesWindowProperties::RemoveZoneIndexProperty(window);
    UpdateZoneTitleBars();

    const bool usesTabs = IsTabsTitleBarStyle(FancyZonesSettings::settings().zoneTitleBarStyle);
    const auto windowsByIndexSet = m_layoutWindows.WindowsByIndexSets().find(zones);
    if (usesTabs && windowsByIndexSet != m_layoutWindows.WindowsByIndexSets().end() && windowsByIndexSet->second.size() == 1)
    {
        const RECT rect = *GetZoneInlineFrame(zones).get();
        const auto remainingWindow = windowsByIndexSet->second.front();
        const auto adjustedRect = FancyZonesWindowUtils::AdjustRectForSizeWindowToRect(remainingWindow, rect, m_window, /*rectAlreadyInScreenCoordinates*/ true);
        FancyZonesWindowUtils::SizeWindowToRect(remainingWindow, adjustedRect);
    }

    return true;
}

const GUID WorkArea::GetLayoutId() const noexcept
{
    if (m_layout)
    {
        return m_layout->Id();
    }

    return GUID{};
}

void WorkArea::ShowZones(const ZoneIndexSet& highlight, HWND draggedWindow/* = nullptr*/)
{
    if (m_layout && m_zonesOverlay)
    {
        SetWorkAreaWindowAsTopmost(draggedWindow);
        m_zonesOverlay->DrawActiveZoneSet(m_layout->Zones(), highlight, Colors::GetZoneColors(), FancyZonesSettings::settings().showZoneNumber);
        m_zonesOverlay->Show();
    }
}

void WorkArea::HideZones()
{
    if (m_zonesOverlay)
    {
        m_zonesOverlay->Hide();
    }
}

void WorkArea::FlashZones()
{
    if (m_layout && m_zonesOverlay)
    {
        SetWorkAreaWindowAsTopmost(nullptr);
        m_zonesOverlay->DrawActiveZoneSet(m_layout->Zones(), {}, Colors::GetZoneColors(), FancyZonesSettings::settings().showZoneNumber);
        m_zonesOverlay->Flash();
    }
}

void WorkArea::ShowMonitorRotationPreview(const std::vector<RECT>& windowRects, size_t monitorNumber, std::optional<bool> reverse, bool animateRotation)
{
    if (!m_zonesOverlay)
    {
        return;
    }

    std::vector<RECT> localWindowRects;
    localWindowRects.reserve(windowRects.size());
    for (const auto& rect : windowRects)
    {
        localWindowRects.push_back(RECT{
            .left = rect.left - m_workAreaRect.left(),
            .top = rect.top - m_workAreaRect.top(),
            .right = rect.right - m_workAreaRect.left(),
            .bottom = rect.bottom - m_workAreaRect.top(),
        });
    }

    SetWorkAreaWindowAsTopmost(nullptr);
    m_zonesOverlay->DrawMonitorRotationPreview(localWindowRects, monitorNumber, reverse, animateRotation);
    m_zonesOverlay->Show();
}

void WorkArea::InitLayout()
{
    InitLayout({});

    if (m_window && m_layout)
    {
        m_zonesOverlay->DrawActiveZoneSet(m_layout->Zones(), {}, Colors::GetZoneColors(), FancyZonesSettings::settings().showZoneNumber);
    }

    UpdateZoneTitleBars();
}

void WorkArea::UpdateWindowPositions()
{
    const auto& snappedWindows = m_layoutWindows.SnappedWindows();
    for (const auto& [window, zones] : snappedWindows)
    {
        Snap(window, zones, true);
    }
}

void WorkArea::CycleWindows(HWND window, bool reverse)
{
    m_layoutWindows.CycleWindows(window, reverse);
    UpdateZoneTitleBars();
}

void WorkArea::UpdateZoneTitleBars()
{
    if (!m_layout)
    {
        m_zoneTitleBars.clear();
        m_zoneTitleBarStyle.reset();
        return;
    }

    const auto style = FancyZonesSettings::settings().zoneTitleBarStyle;
    if (m_zoneTitleBarStyle != style)
    {
        m_zoneTitleBars.clear();
        m_zoneTitleBarStyle = style;
    }

    const auto& windowsByIndexSets = m_layoutWindows.WindowsByIndexSets();

    for (auto it = m_zoneTitleBars.begin(); it != m_zoneTitleBars.end();)
    {
        if (!windowsByIndexSets.contains(it->first))
        {
            it = m_zoneTitleBars.erase(it);
        }
        else
        {
            ++it;
        }
    }

    for (const auto& [indexSet, windows] : windowsByIndexSets)
    {
        if (windows.empty())
        {
            m_zoneTitleBars.erase(indexSet);
            continue;
        }

        if (IsTabsTitleBarStyle(style) && windows.size() == 1)
        {
            m_zoneTitleBars.erase(indexSet);
            continue;
        }

        const RECT zoneRect = m_layout->GetCombinedZonesRect(indexSet);
        // Zone rects from the layout are relative to this WorkArea's own overlay
        // window (i.e. monitor work area), not the virtual screen. The title bar is
        // a real top-level window, so it needs absolute screen coordinates - otherwise
        // it renders at the wrong location on any monitor that isn't at the origin.
        RECT zoneScreenRect = zoneRect;
        MapWindowRect(m_window, nullptr, &zoneScreenRect);
        const auto zoneRectFz = FancyZonesUtils::Rect(zoneScreenRect);
        // Use the work area's own monitor-bound window to resolve DPI. The snapped
        // window can still report the DPI of its previous monitor for a moment after
        // being moved across monitors with different scaling/height, which caused the
        // title bar to be sized incorrectly (or hidden) on multi-monitor setups.
        const UINT dpi = GetDpiForWindow(m_window);

        auto existingTitleBar = m_zoneTitleBars.find(indexSet);
        if (existingTitleBar != m_zoneTitleBars.end() &&
            existingTitleBar->second->GetZoneRect() == zoneRectFz &&
            existingTitleBar->second->GetDpi() == dpi)
        {
            existingTitleBar->second->UpdateZoneWindows(windows);
        }
        else
        {
            auto newTitleBar = MakeZoneTitleBar(style, m_hinstance, zoneRectFz, dpi, [this](HWND windowToUnsnap)
                {
                    Unsnap(windowToUnsnap);
                });
            newTitleBar->UpdateZoneWindows(windows);
            m_zoneTitleBars[indexSet] = std::move(newTitleBar);
        }
    }
}

FancyZonesUtils::Rect WorkArea::GetZoneInlineFrame(const ZoneIndexSet& zones) const
{
    const auto titleBar = m_zoneTitleBars.find(zones);
    if (titleBar != m_zoneTitleBars.end())
    {
        // Title bar frames are already in absolute screen coordinates.
        return titleBar->second->GetInlineFrame();
    }

    if (m_layout)
    {
        // No title bar: the layout's zone rect is relative to this WorkArea's own
        // window, so convert it to absolute screen coordinates for consistency.
        RECT rect = m_layout->GetCombinedZonesRect(zones);
        MapWindowRect(m_window, nullptr, &rect);
        return FancyZonesUtils::Rect(rect);
    }

    return {};
}

#pragma region private

bool WorkArea::InitWindow(HINSTANCE hinstance)
{
    m_window = windowPool.NewZonesOverlayWindow(m_workAreaRect, hinstance, this);
    if (!m_window)
    {
        Logger::error(L"No work area window");
        return false;
    }

    m_zonesOverlay = std::make_unique<ZonesOverlay>(m_window);
    return true;
}

void WorkArea::InitLayout(const FancyZonesDataTypes::WorkAreaId& parentUniqueId)
{
    Logger::info(L"Initialize layout on {}, work area rect = {}x{}", m_uniqueId.toString(), m_workAreaRect.width(), m_workAreaRect.height());

    const bool isLayoutAlreadyApplied = AppliedLayouts::instance().IsLayoutApplied(m_uniqueId);
    if (!isLayoutAlreadyApplied)
    {
        if (!AppliedLayouts::instance().CloneLayout(parentUniqueId, m_uniqueId))
        {
            AppliedLayouts::instance().ApplyDefaultLayout(m_uniqueId);
        }

        AppliedLayouts::instance().SaveData();
    }

    CalculateZoneSet();
}

void WorkArea::InitSnappedWindows()
{
    static bool updatePositionOnceOnStartFlag = true;
    Logger::info(L"Init work area {} windows, update positions = {}", m_uniqueId.toString(), updatePositionOnceOnStartFlag);

    for (const auto& window : VirtualDesktop::instance().GetWindowsFromCurrentDesktop())
    {
        auto indexes = FancyZonesWindowProperties::RetrieveZoneIndexProperty(window);
        if (indexes.size() == 0)
        {
            continue;
        }

        if (!m_uniqueId.monitorId.monitor) // one work area across monitors
        {
            Snap(window, indexes, updatePositionOnceOnStartFlag);
        }
        else
        {
            const auto monitor = MonitorFromWindow(window, MONITOR_DEFAULTTONULL);
            if (monitor && m_uniqueId.monitorId.monitor == monitor)
            {
                // prioritize snapping on the current monitor if the window was snapped to several work areas
                Snap(window, indexes, updatePositionOnceOnStartFlag);
            }
            else
            {
                // if the window is not snapped on the current monitor, then check the others
                auto savedIndexes = AppZoneHistory::instance().GetAppLastZoneIndexSet(window, m_uniqueId, GetLayoutId());
                if (savedIndexes == indexes)
                {
                    Snap(window, indexes, updatePositionOnceOnStartFlag);
                }
            }
        }
    }

    updatePositionOnceOnStartFlag = false;
}

void WorkArea::CalculateZoneSet()
{
    auto appliedLayout = AppliedLayouts::instance().GetDeviceLayout(m_uniqueId);
    if (!appliedLayout.has_value())
    {
        Logger::error(L"Layout wasn't applied. Can't init layout on work area {}x{}", m_workAreaRect.width(), m_workAreaRect.height());
        return;
    }

    // For custom layouts the spacing, sensitivity radius and zone count live in the custom
    // layout definition (custom-layouts.json), while the applied-layouts.json snapshot keeps a
    // copy taken at apply time. Editing those properties only rewrites custom-layouts.json, so
    // without this sync the snapshot stays stale and the edits don't take effect until the layout
    // is re-applied (see GH #44058). Re-derive the scalar properties from the current custom
    // layout using the same logic the apply path uses (CustomLayouts::GetLayout also derives the
    // canvas zone count from the zone list, keeping it consistent with Layout::Init validation).
    if (appliedLayout->type == FancyZonesDataTypes::ZoneSetLayoutType::Custom)
    {
        if (const auto refreshed = CustomLayouts::instance().GetLayout(appliedLayout->uuid))
        {
            appliedLayout = refreshed;
        }
    }

    m_layout = std::make_unique<Layout>(appliedLayout.value());
    m_layout->Init(m_workAreaRect, m_uniqueId.monitorId.monitor);
}

LRESULT WorkArea::WndProc(UINT message, WPARAM wparam, LPARAM lparam) noexcept
{
    switch (message)
    {
    case WM_NCDESTROY:
    {
        ::DefWindowProc(m_window, message, wparam, lparam);
        SetWindowLongPtr(m_window, GWLP_USERDATA, 0);
    }
    break;

    case WM_ERASEBKGND:
        return 1;

    default:
    {
        return DefWindowProc(m_window, message, wparam, lparam);
    }
    }
    return 0;
}

void WorkArea::SetWorkAreaWindowAsTopmost(HWND draggedWindow) noexcept
{
    if (!m_window)
    {
        return;
    }

    HWND windowInsertAfter = draggedWindow ? draggedWindow : HWND_TOPMOST;

    constexpr UINT flags = SWP_NOSIZE | SWP_NOMOVE | SWP_NOACTIVATE;
    SetWindowPos(m_window, windowInsertAfter, 0, 0, 0, 0, flags);
}

#pragma endregion

LRESULT CALLBACK WorkArea::s_WndProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) noexcept
{
    auto thisRef = reinterpret_cast<WorkArea*>(GetWindowLongPtr(window, GWLP_USERDATA));
    if ((thisRef == nullptr) && (message == WM_CREATE))
    {
        auto createStruct = reinterpret_cast<LPCREATESTRUCT>(lparam);
        thisRef = static_cast<WorkArea*>(createStruct->lpCreateParams);
        SetWindowLongPtr(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(thisRef));
    }

    return (thisRef != nullptr) ? thisRef->WndProc(message, wparam, lparam) :
                                  DefWindowProc(window, message, wparam, lparam);
}
