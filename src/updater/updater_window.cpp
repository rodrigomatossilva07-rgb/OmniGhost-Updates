#include "updater_window.h"
#include "../project/resources/resource.h"

#include <Windows.h>
#include <windowsx.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <mutex>
#include <optional>
#include <thread>

namespace OmniGhost::Update {
namespace {
constexpr COLORREF kBackground = RGB(11, 11, 13);
constexpr COLORREF kSurface = RGB(19, 19, 22);
constexpr COLORREF kGold = RGB(215, 179, 58);
constexpr COLORREF kText = RGB(245, 245, 245);
constexpr COLORREF kMuted = RGB(150, 150, 157);
constexpr COLORREF kBorder = RGB(41, 41, 45);
constexpr COLORREF kSuccess = RGB(65, 200, 125);
constexpr UINT kStateChanged = WM_APP + 71;

std::wstring FormatBytes(std::uint64_t value) {
    const wchar_t* units[] = {L"B", L"KB", L"MB", L"GB"};
    double amount = static_cast<double>(value);
    unsigned unit = 0;
    while (amount >= 1024.0 && unit < 3) { amount /= 1024.0; ++unit; }
    wchar_t buffer[64]{};
    swprintf_s(buffer, L"%.1f %s", amount, units[unit]);
    return buffer;
}

void Fill(HDC dc, const RECT& rect, COLORREF colour) {
    HBRUSH brush = CreateSolidBrush(colour);
    FillRect(dc, &rect, brush);
    DeleteObject(brush);
}

void Text(HDC dc, const wchar_t* value, RECT rect, HFONT font, COLORREF colour,
          UINT flags = DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX) {
    SelectObject(dc, font);
    SetTextColor(dc, colour);
    SetBkMode(dc, TRANSPARENT);
    DrawTextW(dc, value, -1, &rect, flags);
}
}

struct UpdaterWindow::Impl {
    std::mutex mutex;
    std::condition_variable ready;
    std::condition_variable actionChanged;
    std::jthread thread;
    HWND hwnd{};
    HFONT brandFont{}, subtitleFont{}, titleFont{}, bodyFont{}, smallFont{}, buttonFont{};
    HICON icon{};
    std::wstring version;
    std::wstring title = L"A preparar atualização...";
    std::wstring detail = L"A preparar os ficheiros necessários.";
    std::wstring technical;
    double progress = -1.0;
    std::uint64_t received{}, total{};
    double speed{};
    bool initialized{}, stopping{}, success{}, error{}, detailsVisible{};
    int errorAction{}; // 1 = retry, 2 = close
    unsigned animation{};

    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
        auto* self = reinterpret_cast<Impl*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (message == WM_NCCREATE) {
            const auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
            self = static_cast<Impl*>(create->lpCreateParams);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        }
        if (!self) return DefWindowProcW(hwnd, message, wParam, lParam);
        switch (message) {
        case WM_TIMER:
            ++self->animation;
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        case kStateChanged:
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        case WM_LBUTTONDOWN: {
            POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            std::lock_guard lock(self->mutex);
            if (self->error && point.y >= 235 && point.y <= 274) {
                if (point.x >= 28 && point.x <= 198) {
                    self->errorAction = 1;
                    self->actionChanged.notify_all();
                    DestroyWindow(hwnd);
                } else if (point.x >= 286 && point.x <= 452) {
                    self->detailsVisible = !self->detailsVisible;
                    InvalidateRect(hwnd, nullptr, FALSE);
                }
            } else if (point.y < 54) {
                ReleaseCapture();
                SendMessageW(hwnd, WM_NCLBUTTONDOWN, HTCAPTION, 0);
            }
            return 0;
        }
        case WM_DPICHANGED: {
            const RECT* suggested = reinterpret_cast<const RECT*>(lParam);
            SetWindowPos(hwnd, nullptr, suggested->left, suggested->top,
                         suggested->right - suggested->left, suggested->bottom - suggested->top,
                         SWP_NOZORDER | SWP_NOACTIVATE);
            return 0;
        }
        case WM_CLOSE:
            // Installation must not be interrupted. Errors may be dismissed.
            if (self->error) {
                { std::lock_guard lock(self->mutex); self->errorAction = 2; }
                self->actionChanged.notify_all();
                DestroyWindow(hwnd);
            }
            return 0;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        case WM_PAINT:
            self->Paint();
            return 0;
        default:
            return DefWindowProcW(hwnd, message, wParam, lParam);
        }
    }

    void Paint() {
        PAINTSTRUCT paint{};
        HDC dc = BeginPaint(hwnd, &paint);
        RECT client{}; GetClientRect(hwnd, &client);
        HDC buffer = CreateCompatibleDC(dc);
        HBITMAP bitmap = CreateCompatibleBitmap(dc, client.right, client.bottom);
        HGDIOBJ oldBitmap = SelectObject(buffer, bitmap);
        Fill(buffer, client, kBackground);

        std::lock_guard lock(mutex);
        RECT header{0, 0, client.right, 66}; Fill(buffer, header, kSurface);
        RECT accent{0, 0, 3, client.bottom}; Fill(buffer, accent, error ? RGB(220, 65, 80) : kGold);
        if (icon) DrawIconEx(buffer, 22, 17, icon, 34, 34, 0, nullptr, DI_NORMAL);
        Text(buffer, L"OMNIGHOST", {68, 11, 300, 37}, brandFont, kText);
        Text(buffer, L"Atualizador", {68, 33, 210, 55}, subtitleFont, kGold);
        if (!version.empty()) {
            std::wstring versionText = L"Versão " + version;
            Text(buffer, versionText.c_str(), {300, 15, 452, 45}, smallFont, kMuted,
                 DT_RIGHT | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
        }

        const COLORREF statusColour = error ? RGB(245, 94, 105) : (success ? kSuccess : kText);
        Text(buffer, title.c_str(), {28, 82, 452, 116}, titleFont, statusColour);
        Text(buffer, detail.c_str(), {28, 116, 452, 150}, bodyFont, kMuted);

        RECT track{28, 164, 452, 170}; Fill(buffer, track, RGB(35, 35, 39));
        RECT bar = track;
        if (progress >= 0.0) {
            bar.right = bar.left + static_cast<LONG>((bar.right - bar.left) * std::clamp(progress, 0.0, 1.0));
        } else if (!error && !success) {
            const LONG width = 92;
            const LONG travel = (track.right - track.left) + width;
            const LONG x = static_cast<LONG>((animation * 7) % travel) - width;
            bar.left = std::max(track.left, track.left + x);
            bar.right = std::min(track.right, track.left + x + width);
        } else {
            bar.right = success ? track.right : track.left;
        }
        if (bar.right > bar.left) Fill(buffer, bar, success ? kSuccess : kGold);

        if (total) {
            std::wstring left = FormatBytes(received) + L" / " + FormatBytes(total);
            std::wstring right;
            if (speed > 0.0) right = FormatBytes(static_cast<std::uint64_t>(speed)) + L"/s";
            Text(buffer, left.c_str(), {28, 178, 270, 202}, smallFont, kMuted);
            Text(buffer, right.c_str(), {280, 178, 452, 202}, smallFont, kMuted,
                 DT_RIGHT | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
        }

        if (error && detailsVisible) {
            RECT details{28, 202, 452, 235}; Fill(buffer, details, RGB(16, 16, 19));
            Text(buffer, technical.c_str(), {38, 202, 442, 235}, smallFont, kMuted,
                 DT_LEFT | DT_WORDBREAK | DT_NOPREFIX);
        }
        const wchar_t* footer = success
            ? L"O OmniGhost será iniciado automaticamente."
            : L"O OmniGhost será iniciado automaticamente após a atualização.";
        Text(buffer, footer, {28, 236, error ? 276 : 452, 276}, smallFont, kMuted);
        if (error) {
            RECT retry{28, 235, 198, 274};
            HBRUSH retryBrush = CreateSolidBrush(kGold);
            HPEN retryPen = CreatePen(PS_SOLID, 1, kGold);
            SelectObject(buffer, retryBrush); SelectObject(buffer, retryPen);
            RoundRect(buffer, retry.left, retry.top, retry.right, retry.bottom, 10, 10);
            DeleteObject(retryBrush); DeleteObject(retryPen);
            Text(buffer, L"Tentar novamente", retry, buttonFont, RGB(12, 12, 14),
                 DT_CENTER | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
            RECT button{286, 235, 452, 274};
            HBRUSH brush = CreateSolidBrush(RGB(24, 24, 28));
            HPEN pen = CreatePen(PS_SOLID, 1, kBorder);
            SelectObject(buffer, brush); SelectObject(buffer, pen);
            RoundRect(buffer, button.left, button.top, button.right, button.bottom, 10, 10);
            DeleteObject(brush); DeleteObject(pen);
            Text(buffer, detailsVisible ? L"Ocultar detalhes" : L"Detalhes do erro", button,
                 buttonFont, kText, DT_CENTER | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
        }

        BitBlt(dc, 0, 0, client.right, client.bottom, buffer, 0, 0, SRCCOPY);
        SelectObject(buffer, oldBitmap); DeleteObject(bitmap); DeleteDC(buffer);
        EndPaint(hwnd, &paint);
    }

    void Run() {
        SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        WNDCLASSEXW wc{sizeof(wc)};
        wc.lpfnWndProc = WindowProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hIcon = LoadIconW(wc.hInstance, MAKEINTRESOURCEW(IDI_OMNIGHOST_ICON));
        wc.hIconSm = wc.hIcon;
        wc.lpszClassName = L"OmniGhostIndependentUpdater";
        RegisterClassExW(&wc);
        icon = wc.hIcon;

        brandFont = CreateFontW(-18, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
        subtitleFont = CreateFontW(-12, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
        titleFont = CreateFontW(-21, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
        bodyFont = CreateFontW(-14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
        smallFont = CreateFontW(-12, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
        buttonFont = CreateFontW(-13, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");

        constexpr int width = 480, height = 300;
        POINT cursor{}; GetCursorPos(&cursor);
        HMONITOR monitor = MonitorFromPoint(cursor, MONITOR_DEFAULTTOPRIMARY);
        MONITORINFO info{sizeof(info)}; GetMonitorInfoW(monitor, &info);
        const int x = info.rcWork.left + ((info.rcWork.right - info.rcWork.left) - width) / 2;
        const int y = info.rcWork.top + ((info.rcWork.bottom - info.rcWork.top) - height) / 2;
        hwnd = CreateWindowExW(WS_EX_APPWINDOW, wc.lpszClassName, L"OmniGhost — Atualizador",
            WS_POPUP | WS_SYSMENU, x, y, width, height, nullptr, nullptr, wc.hInstance, this);
        if (hwnd) {
            SetWindowRgn(hwnd, CreateRoundRectRgn(0, 0, width + 1, height + 1, 18, 18), TRUE);
            SendMessageW(hwnd, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(icon));
            SendMessageW(hwnd, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(icon));
            SetTimer(hwnd, 1, 30, nullptr);
            ShowWindow(hwnd, SW_SHOWNORMAL);
            UpdateWindow(hwnd);
        }
        {
            std::lock_guard lock(mutex); initialized = true;
        }
        ready.notify_all();
        MSG message{};
        while (hwnd && GetMessageW(&message, nullptr, 0, 0) > 0) {
            TranslateMessage(&message); DispatchMessageW(&message);
        }
        hwnd = nullptr;
        for (HFONT font : {brandFont, subtitleFont, titleFont, bodyFont, smallFont, buttonFont})
            if (font) DeleteObject(font);
    }

    void Notify() { if (hwnd) PostMessageW(hwnd, kStateChanged, 0, 0); }
};

UpdaterWindow::UpdaterWindow() : impl_(std::make_unique<Impl>()) {}
UpdaterWindow::~UpdaterWindow() { Stop(); }

bool UpdaterWindow::Start(const std::string& targetVersion) {
    if (impl_->thread.joinable()) return impl_->hwnd != nullptr;
    impl_->version.assign(targetVersion.begin(), targetVersion.end());
    impl_->thread = std::jthread([this](std::stop_token) { impl_->Run(); });
    std::unique_lock lock(impl_->mutex);
    impl_->ready.wait_for(lock, std::chrono::seconds(3), [this] { return impl_->initialized; });
    return impl_->hwnd != nullptr;
}

void UpdaterWindow::SetStage(std::wstring title, std::wstring detail, double progress) {
    { std::lock_guard lock(impl_->mutex); impl_->title = std::move(title); impl_->detail = std::move(detail);
      impl_->progress = progress; impl_->error = false; impl_->success = false; impl_->technical.clear(); }
    impl_->Notify();
}

void UpdaterWindow::SetTransfer(std::uint64_t received, std::uint64_t total, double bytesPerSecond) {
    { std::lock_guard lock(impl_->mutex); impl_->received = received; impl_->total = total;
      impl_->speed = bytesPerSecond; impl_->progress = total ? static_cast<double>(received) / total : -1.0; }
    impl_->Notify();
}

void UpdaterWindow::SetSuccess() {
    { std::lock_guard lock(impl_->mutex); impl_->success = true; impl_->error = false; impl_->progress = 1.0;
      impl_->title = L"Atualização concluída"; impl_->detail = L"O OmniGhost está atualizado."; }
    impl_->Notify();
}

void UpdaterWindow::SetError(std::wstring summary, std::wstring technicalDetail) {
    { std::lock_guard lock(impl_->mutex); impl_->error = true; impl_->success = false; impl_->progress = -1.0;
      impl_->title = L"Não foi possível concluir a atualização"; impl_->detail = std::move(summary);
      impl_->technical = std::move(technicalDetail); impl_->errorAction = 0; }
    impl_->Notify();
}

bool UpdaterWindow::WaitForErrorAction() {
    std::unique_lock lock(impl_->mutex);
    impl_->actionChanged.wait(lock, [this] { return impl_->errorAction != 0 || !impl_->hwnd; });
    return impl_->errorAction == 1;
}

void UpdaterWindow::Stop() {
    if (!impl_ || !impl_->thread.joinable()) return;
    if (impl_->hwnd) PostMessageW(impl_->hwnd, WM_DESTROY, 0, 0);
    impl_->thread.request_stop();
    impl_->thread.join();
}
}
