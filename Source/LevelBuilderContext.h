/**
 * @file LevelBuilderContext.h
 * @brief Transient state shared between the active tool, brush, and snap
 *        provider. Owned by the registry; passed to LevelBuilderTool::Activate.
 */

#pragma once

#include "LevelBuilderCoreAPI.h"

struct PolyphaseEngineAPI;

class LevelBuilderContext
{
public:
    LevelBuilderContext() = default;
    ~LevelBuilderContext() = default;

    // Engine access — set by core during OnLoad.
    PolyphaseEngineAPI* mEngineAPI = nullptr;

    // Last raycast result from the viewport. Populated every frame by the
    // core driver in LevelBuilderEditorUI::DrawViewportPreview when the
    // cursor is over the viewport. Siblings read it back via the v2 ABI
    // entry LevelBuilderCoreAPI::Viewport_GetHoverHit.
    LBVec3 mLastRaycastHit{0.0f, 0.0f, 0.0f};
    LBVec3 mLastRaycastNormal{0.0f, 1.0f, 0.0f};
    void*  mLastRaycastNode = nullptr;
    bool   mLastRaycastValid = false;

    // Current rotation snap (degrees) the user has dialed in via hotkeys
    // or the brush UI. Tools/brushes apply this on top of any snap rotation.
    float  mRotationStepDeg = 90.0f;
    float  mManualYawDeg    = 0.0f;

    // Last placement error — copied here by the registry's Place() impl so
    // the UI can show it in the Debug tab.
    char   mLastError[256] = {0};

    // Viewport mouse-button state. Populated each frame by the core
    // driver in DrawViewportPreview (from the engine's v5
    // Viewport_GetMouseState hook). Read back by drag-input brushes
    // (Paint) via the v7 ABI entries Viewport_IsLmbDown / Viewport_IsRmbDown.
    bool   mLmbDown = false;
    bool   mRmbDown = false;
};
