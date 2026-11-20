#pragma once
#include <windows.h>

#include <chrono>

#include "Renderer.h"

class WatermarkWindow {
public:
    WatermarkWindow() = default;
    ~WatermarkWindow();

    WatermarkWindow(const WatermarkWindow&) = delete;
    WatermarkWindow& operator=(const WatermarkWindow&) = delete;

    void Create(HINSTANCE instance);

    void Render();

private:
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
    LRESULT HandleMessage(UINT msg, WPARAM wp, LPARAM lp);

    void UpdateAnimation(float seconds);
    void FitToPrimaryMonitor();

    HWND     m_hwnd = nullptr;
    UINT     m_width = 0;
    UINT     m_height = 0;
    float    m_dpi = 96.0f;
    Renderer m_renderer;

    float         m_side = 100.0f;
    D2D1_POINT_2F m_pos{ 100.0f, 100.0f };
    D2D1_POINT_2F m_vel{ 140.0f, 100.0f };
    std::chrono::steady_clock::time_point m_lastFrame{};
};
