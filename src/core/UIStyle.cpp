#include "UIStyle.h"

void UIStyle::ApplyStyle() {
    ImGuiStyle& style = ImGui::GetStyle();
    ImVec4* colors = style.Colors;

    // Window colors
    colors[ImGuiCol_WindowBg]          = Colors::PanelBackground;
    colors[ImGuiCol_ChildBg]           = Colors::PanelBackground;
    colors[ImGuiCol_PopupBg]           = Colors::PanelBackground;
    colors[ImGuiCol_MenuBarBg]         = Colors::HeaderBackground;
    colors[ImGuiCol_TitleBg]           = Colors::HeaderBackground;
    colors[ImGuiCol_TitleBgActive]     = Colors::HeaderBackground;
    colors[ImGuiCol_TitleBgCollapsed]  = Colors::HeaderBackground;

    // Border colors
    colors[ImGuiCol_Border]            = Colors::Border;
    colors[ImGuiCol_BorderShadow]      = ImVec4(0, 0, 0, 0);  // No shadow

    // Text colors
    colors[ImGuiCol_Text]              = Colors::TextPrimary;
    colors[ImGuiCol_TextDisabled]      = Colors::TextDisabled;
    colors[ImGuiCol_TextSelectedBg]    = ImVec4(Colors::Accent.x, Colors::Accent.y, Colors::Accent.z, 0.35f);

    // Button colors
    colors[ImGuiCol_Button]            = ImVec4(0.26f, 0.29f, 0.35f, 1.0f);
    colors[ImGuiCol_ButtonHovered]     = ImVec4(0.36f, 0.39f, 0.45f, 1.0f);
    colors[ImGuiCol_ButtonActive]      = Colors::AccentActive;

    // Frame colors (input fields, sliders, etc.)
    colors[ImGuiCol_FrameBg]           = ImVec4(0.20f, 0.22f, 0.27f, 1.0f);
    colors[ImGuiCol_FrameBgHovered]    = ImVec4(0.26f, 0.29f, 0.35f, 1.0f);
    colors[ImGuiCol_FrameBgActive]     = ImVec4(0.26f, 0.29f, 0.35f, 1.0f);

    // Tab colors
    colors[ImGuiCol_Tab]               = Colors::PanelBackground;
    colors[ImGuiCol_TabHovered]        = Colors::AccentHover;
    colors[ImGuiCol_TabActive]         = Colors::Accent;
    colors[ImGuiCol_TabUnfocused]      = Colors::PanelBackground;
    colors[ImGuiCol_TabUnfocusedActive]= ImVec4(0.26f, 0.29f, 0.35f, 1.0f);

    // Header colors (collapsing headers, tree nodes)
    colors[ImGuiCol_Header]            = ImVec4(0.26f, 0.29f, 0.35f, 1.0f);
    colors[ImGuiCol_HeaderHovered]     = Colors::AccentHover;
    colors[ImGuiCol_HeaderActive]      = Colors::Accent;

    // Checkboxes & radio buttons
    colors[ImGuiCol_CheckMark]         = Colors::Accent;

    // Slider colors
    colors[ImGuiCol_SliderGrab]        = Colors::Accent;
    colors[ImGuiCol_SliderGrabActive]  = Colors::AccentActive;

    // Scrollbar colors
    colors[ImGuiCol_ScrollbarBg]       = Colors::PanelBackground;
    colors[ImGuiCol_ScrollbarGrab]     = Colors::ScrollbarGrab;
    colors[ImGuiCol_ScrollbarGrabHovered] = Colors::ScrollbarGrabHover;
    colors[ImGuiCol_ScrollbarGrabActive]  = Colors::Accent;

    // Separator colors
    colors[ImGuiCol_Separator]         = Colors::Border;
    colors[ImGuiCol_SeparatorHovered]  = Colors::AccentHover;
    colors[ImGuiCol_SeparatorActive]   = Colors::Accent;

    // Resize grip colors
    colors[ImGuiCol_ResizeGrip]        = Colors::Border;
    colors[ImGuiCol_ResizeGripHovered] = Colors::AccentHover;
    colors[ImGuiCol_ResizeGripActive]  = Colors::Accent;

    // Docking colors
    colors[ImGuiCol_DockingPreview]    = ImVec4(Colors::Accent.x, Colors::Accent.y, Colors::Accent.z, 0.3f);
    colors[ImGuiCol_DockingEmptyBg]    = Colors::MainBackground;

    // Table colors
    colors[ImGuiCol_TableHeaderBg]     = Colors::HeaderBackground;
    colors[ImGuiCol_TableBorderStrong] = Colors::Border;
    colors[ImGuiCol_TableBorderLight]  = Colors::Border;
    colors[ImGuiCol_TableRowBg]        = ImVec4(0, 0, 0, 0);
    colors[ImGuiCol_TableRowBgAlt]     = Colors::TableAltRow;

    // Modal/dimming colors
    colors[ImGuiCol_ModalWindowDimBg]  = ImVec4(0.0f, 0.0f, 0.0f, 0.6f);

    // --- Spacing and sizing ---
    style.WindowPadding    = ImVec2(Spacing::WindowPadding, Spacing::WindowPadding);
    style.FramePadding     = ImVec2(8.0f, 5.0f);  // Slightly wider for buttons
    style.ItemSpacing      = ImVec2(Spacing::ItemSpacing, 6.0f);
    style.ItemInnerSpacing = ImVec2(Spacing::ItemInnerSpacing, Spacing::ItemInnerSpacing);
    style.ScrollbarSize    = Spacing::ScrollbarSize;
    style.GrabMinSize      = Spacing::GrabMinSize;
    style.IndentSpacing    = 20.0f;

    // --- Rounding ---
    style.WindowRounding    = Rounding::Window;
    style.ChildRounding     = Rounding::Child;
    style.FrameRounding     = Rounding::Frame;
    style.PopupRounding     = Rounding::Popup;
    style.ScrollbarRounding = Rounding::Scrollbar;
    style.GrabRounding      = Rounding::Grab;
    style.TabRounding       = Rounding::Tab;

    // --- Borders ---
    style.WindowBorderSize = 1.0f;
    style.ChildBorderSize  = 1.0f;
    style.PopupBorderSize  = 1.0f;
    style.FrameBorderSize  = 0.0f;  // No border on frames
    style.TabBorderSize    = 0.0f;  // No border on tabs

    // --- Misc ---
    style.AntiAliasedLines = true;
    style.AntiAliasedFill  = true;
}
