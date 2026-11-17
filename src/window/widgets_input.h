#pragma once

#include "widgets.h"
#include <string>

namespace CyberWidgets {

// =============================================================================
// Input Widgets
// =============================================================================

bool InputText(const char* label, std::string* value, ImGuiInputTextFlags flags = 0);
bool InputTextMultiline(const char* label, std::string* value, const ImVec2& size = ImVec2(0, 0), ImGuiInputTextFlags flags = 0);

bool InputFloat(const char* label, float* value, float step = 0.0f, float step_fast = 0.0f, const char* format = "%.3f");
bool InputDouble(const char* label, double* value, double step = 0.0, double step_fast = 0.0, const char* format = "%.6f");
bool InputInt(const char* label, int* value, int step = 1, int step_fast = 10);
bool InputInt64(const char* label, long long* value, long long step = 1, long long step_fast = 100);
bool InputUInt64(const char* label, unsigned long long* value, unsigned long long step = 1, unsigned long long step_fast = 100);

// Drag widgets (slider-like but with keyboard input)
bool DragFloat(const char* label, float* value, float speed = 1.0f, float min = 0.0f, float max = 0.0f, const char* format = "%.3f");
bool DragFloat2(const char* label, float v[2], float speed = 1.0f, float min = 0.0f, float max = 0.0f, const char* format = "%.3f");
bool DragFloat3(const char* label, float v[3], float speed = 1.0f, float min = 0.0f, float max = 0.0f, const char* format = "%.3f");
bool DragFloat4(const char* label, float v[4], float speed = 1.0f, float min = 0.0f, float max = 0.0f, const char* format = "%.3f");
bool DragInt(const char* label, int* value, float speed = 1.0f, int min = 0, int max = 0, const char* format = "%d");
bool DragInt2(const char* label, int v[2], float speed = 1.0f, int min = 0, int max = 0, const char* format = "%d");
bool DragInt3(const char* label, int v[3], float speed = 1.0f, int min = 0, int max = 0, const char* format = "%d");
bool DragInt4(const char* label, int v[4], float speed = 1.0f, int min = 0, int max = 0, const char* format = "%d");

// Slider widgets
bool SliderFloat(const char* label, float* value, float min, float max, const char* format = "%.3f");
bool SliderFloat2(const char* label, float v[2], float min, float max, const char* format = "%.3f");
bool SliderFloat3(const char* label, float v[3], float min, float max, const char* format = "%.3f");
bool SliderFloat4(const char* label, float v[4], float min, float max, const char* format = "%.3f");
bool SliderInt(const char* label, int* value, int min, int max, const char* format = "%d");
bool SliderInt2(const char* label, int v[2], int min, int max, const char* format = "%d");
bool SliderInt3(const char* label, int v[3], int min, int max, const char* format = "%d");
bool SliderInt4(const char* label, int v[4], int min, int max, const char* format = "%d");

// Color picker/editor
bool ColorEdit3(const char* label, float col[3], ImGuiColorEditFlags flags = 0);
bool ColorEdit4(const char* label, float col[4], ImGuiColorEditFlags flags = 0);
bool ColorPicker3(const char* label, float col[3], ImGuiColorEditFlags flags = 0);
bool ColorPicker4(const char* label, float col[4], ImGuiColorEditFlags flags = 0);

} // namespace CyberWidgets