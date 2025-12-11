#pragma once

#include <imgui.h>

namespace UIStyle {
    // Color palette - FilePilot-inspired dark theme
    namespace Colors {
        // Background colors
        constexpr ImVec4 MainBackground    = ImVec4(0x11/255.0f, 0x13/255.0f, 0x18/255.0f, 1.0f); // #111318
        constexpr ImVec4 PanelBackground   = ImVec4(0x18/255.0f, 0x1b/255.0f, 0x21/255.0f, 1.0f); // #181b21
        constexpr ImVec4 HeaderBackground  = ImVec4(0x1e/255.0f, 0x21/255.0f, 0x29/255.0f, 1.0f); // #1e2129
        constexpr ImVec4 StrongBackground  = ImVec4(0x20/255.0f, 0x25/255.0f, 0x30/255.0f, 1.0f); // #202530

        // Accent colors
        constexpr ImVec4 Accent            = ImVec4(0x4e/255.0f, 0xa6/255.0f, 0xff/255.0f, 1.0f); // #4EA6FF
        constexpr ImVec4 AccentHover       = ImVec4(0x5e/255.0f, 0xb5/255.0f, 0xff/255.0f, 1.0f); // #5EB5FF
        constexpr ImVec4 AccentActive      = ImVec4(0x3d/255.0f, 0x8f/255.0f, 0xe8/255.0f, 1.0f); // #3D8FE8

        // Text colors
        constexpr ImVec4 TextPrimary       = ImVec4(0xf5/255.0f, 0xf6/255.0f, 0xfa/255.0f, 1.0f); // #F5F6FA
        constexpr ImVec4 TextSecondary     = ImVec4(0xa0/255.0f, 0xa4/255.0f, 0xb0/255.0f, 1.0f); // #A0A4B0
        constexpr ImVec4 TextDisabled      = ImVec4(0x6f/255.0f, 0x74/255.0f, 0x80/255.0f, 1.0f); // #6F7480

        // Border colors
        constexpr ImVec4 Border            = ImVec4(0x2b/255.0f, 0x31/255.0f, 0x3d/255.0f, 1.0f); // #2B313D
        constexpr ImVec4 BorderHover       = ImVec4(0x3a/255.0f, 0x3e/255.0f, 0x46/255.0f, 1.0f); // #3A3E46

        // Table colors
        constexpr ImVec4 TableAltRow       = ImVec4(0x15/255.0f, 0x18/255.0f, 0x20/255.0f, 1.0f); // #151820

        // Scrollbar colors
        constexpr ImVec4 ScrollbarGrab     = ImVec4(0x30/255.0f, 0x36/255.0f, 0x44/255.0f, 1.0f); // #303644
        constexpr ImVec4 ScrollbarGrabHover= ImVec4(0x3b/255.0f, 0x42/255.0f, 0x52/255.0f, 1.0f); // #3B4252
    }

    // Spacing constants
    namespace Spacing {
        constexpr float WindowPadding = 10.0f;
        constexpr float FramePadding = 6.0f;
        constexpr float ItemSpacing = 8.0f;
        constexpr float ItemInnerSpacing = 6.0f;
        constexpr float ScrollbarSize = 14.0f;
        constexpr float GrabMinSize = 10.0f;
    }

    // Rounding constants
    namespace Rounding {
        constexpr float Window = 5.0f;
        constexpr float Child = 4.0f;
        constexpr float Frame = 3.0f;
        constexpr float Popup = 4.0f;
        constexpr float Scrollbar = 9.0f;
        constexpr float Grab = 3.0f;
        constexpr float Tab = 4.0f;
    }

    // Apply the complete FilePilot-inspired style
    void ApplyStyle();
}
