#include <Engine/Platform/Window.h>
#include <Engine/Core/Log.h>

#include <format>
#include <limits>

#pragma comment(lib, "User32.lib")
#pragma comment(lib, "Gdi32.lib")

namespace
{
    constexpr wchar_t windowClassName[] = L"WP1.Engine.Window";
}

namespace Engine
{
    Window::~Window()
    {
        if (handle_ != nullptr)
        {
            DestroyWindow(handle_);
        }
        if (classAtom_ != 0)
        {
            UnregisterClassW(windowClassName, instance_);
        }
    }

    bool Window::Create(const wchar_t* title, int width, int height)
    {
        if (handle_ != nullptr || title == nullptr || width <= 0 || height <= 0)
        {
            Log::Error("Invalid window creation arguments or window already exists.");
            return false;
        }

        constexpr DWORD style = WS_OVERLAPPEDWINDOW;
        RECT rectangle{ 0, 0, 0, 0 };
        if (!AdjustWindowRectEx(&rectangle, style, FALSE, 0))
        {
            Log::Error(std::format("AdjustWindowRectEx failed: {}", GetLastError()));
            return false;
        }
        const int borderWidth = rectangle.right - rectangle.left;
        const int borderHeight = rectangle.bottom - rectangle.top;
        if (width > (std::numeric_limits<int>::max)() - borderWidth ||
            height > (std::numeric_limits<int>::max)() - borderHeight)
        {
            Log::Error("Window dimensions are too large.");
            return false;
        }

        instance_ = GetModuleHandleW(nullptr);
        if (classAtom_ == 0)
        {
            WNDCLASSEXW windowClass{};
            windowClass.cbSize = sizeof(windowClass);
            windowClass.style = CS_HREDRAW | CS_VREDRAW;
            windowClass.lpfnWndProc = WindowProcedure;
            windowClass.hInstance = instance_;
            windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
            windowClass.hbrBackground = static_cast<HBRUSH>(GetStockObject(DKGRAY_BRUSH));
            windowClass.lpszClassName = windowClassName;
            classAtom_ = RegisterClassExW(&windowClass);
            if (classAtom_ == 0)
            {
                Log::Error(std::format("RegisterClassExW failed: {}", GetLastError()));
                return false;
            }
        }

        closeRequested_ = false;
        handle_ = CreateWindowExW(0, windowClassName, title, style,
            CW_USEDEFAULT, CW_USEDEFAULT, width + borderWidth, height + borderHeight,
            nullptr, nullptr, instance_, this);
        if (handle_ == nullptr)
        {
            Log::Error(std::format("CreateWindowExW failed: {}", GetLastError()));
            return false;
        }
        return true;
    }

    void Window::Show()
    {
        if (handle_ != nullptr)
        {
            ShowWindow(handle_, SW_SHOW);
            UpdateWindow(handle_);
        }
    }

    bool Window::ProcessMessages(int& exitCode)
    {
        exitCode = 0;
        if (handle_ == nullptr)
        {
            exitCode = 1;
            return false;
        }

        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE))
        {
            if (message.message == WM_QUIT)
            {
                exitCode = static_cast<int>(message.wParam);
                return false;
            }
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        return handle_ != nullptr && !closeRequested_;
    }

    HWND Window::GetHandle() const noexcept
    {
        return handle_;
    }

    LRESULT CALLBACK Window::WindowProcedure(HWND handle, UINT message, WPARAM wParam, LPARAM lParam)
    {
        if (message == WM_NCCREATE)
        {
            const auto* creation = reinterpret_cast<const CREATESTRUCTW*>(lParam);
            auto* window = static_cast<Window*>(creation->lpCreateParams);
            SetWindowLongPtrW(handle, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(window));
            window->handle_ = handle;
        }
        else if (message == WM_CLOSE || message == WM_DESTROY)
        {
            auto* window = reinterpret_cast<Window*>(GetWindowLongPtrW(handle, GWLP_USERDATA));
            if (window != nullptr)
            {
                window->closeRequested_ = true;
            }
            return 0;
        }
        else if (message == WM_NCDESTROY)
        {
            auto* window = reinterpret_cast<Window*>(GetWindowLongPtrW(handle, GWLP_USERDATA));
            if (window != nullptr)
            {
                window->handle_ = nullptr;
            }
            SetWindowLongPtrW(handle, GWLP_USERDATA, 0);
        }
        return DefWindowProcW(handle, message, wParam, lParam);
    }
}
