/**
 * @file LevelBuilderEditorUI.h
 * @brief Editor UI registration for the Level Builder Core addon.
 *        Owns the dockable window, the Addons menu entry, and the
 *        viewport overlay for the placement preview.
 *
 * All entry points are no-ops in non-editor builds.
 */

#pragma once

#if EDITOR

#include <stdint.h>

struct EditorUIHooks;

namespace LevelBuilderEditorUI
{
    void Register(EditorUIHooks* hooks, uint64_t hookId);
    void Unregister(EditorUIHooks* hooks, uint64_t hookId);

    void OpenWindow();
    void CloseWindow();
    bool IsWindowOpen();
}

#endif // EDITOR
