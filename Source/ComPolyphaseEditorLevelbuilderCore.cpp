/**
 * @file ComPolyphaseEditorLevelbuilderCore.cpp
 * @brief Entry point for com.polyphase.editor.levelbuilder.core.
 *
 * Responsibilities:
 *   - Wire the engine API + ImGui context into the addon.
 *   - Initialize the registry singleton.
 *   - Register the editor UI (menu + dockable window + viewport overlay).
 *   - Tear everything down cleanly in OnUnload so hot reload is safe.
 */

#define LEVELBUILDER_CORE_BUILD 1

#include "Plugins/PolyphasePluginAPI.h"
#include "Plugins/PolyphaseEngineAPI.h"

#if EDITOR
#include "Plugins/EditorUIHooks.h"
#include "Plugins/ImGuiPluginContext.h"
#include "imgui.h"
#endif

#include "LevelBuilderRegistry.h"
#include "LevelBuilderEditorUI.h"
#include "LevelBuilderCoreAPI.h"
#include "LBKitRegistry.h"

static PolyphaseEngineAPI* sEngineAPI = nullptr;
static uint64_t            sHookId    = 0;

#if EDITOR
static EditorUIHooks*      sHooks     = nullptr;
#endif

static int OnLoad(PolyphaseEngineAPI* api)
{
    sEngineAPI = api;
    LevelBuilderRegistry::Get().Initialize(api);

#if EDITOR
    // ImGui bootstrap — required before any ImGui call from this DLL.
    if (api && api->GetImGuiContext)
    {
        ImGuiPluginContext ctx{};
        api->GetImGuiContext(&ctx);
        if (ctx.context)
        {
            ImGui::SetCurrentContext(ctx.context);
            ImGui::SetAllocatorFunctions(ctx.allocFunc, ctx.freeFunc, ctx.allocUserData);
        }
    }
#endif

    // v3: core owns kit data. Seed the built-in starter kit then scan
    // <project>/Kits/ so that by the time sibling addons' OnLoad runs
    // (modular declares core as a dependency, so core loads first), the
    // registry is already populated.
    LBKitRegistry::Get().EnsureBuiltinKit();
    int loaded = LBKitRegistry::Get().ScanProjectKits();
    if (api && api->LogDebug)
    {
        api->LogDebug("[LevelBuilderCore] kit registry seeded: %d kit(s) total (%d loaded from disk)",
                      LBKitRegistry::Get().GetKitCount(), loaded);
    }
    if (api && api->LogWarning)
    {
        for (const auto& e : LBKitRegistry::Get().GetLastReloadErrors())
            api->LogWarning("[LevelBuilderCore] %s", e.c_str());
    }

    if (api && api->LogDebug)
        api->LogDebug("[LevelBuilderCore] loaded (API v%u)", LEVEL_BUILDER_CORE_API_VERSION);

    return 0;
}

static void OnUnload()
{
#if EDITOR
    // RemoveAllHooks is also called by the host AFTER OnUnload, but doing
    // it here clears our cached EditorUIHooks pointer first so any racy
    // teardown can't redraw into a freed window.
    if (sHooks)
    {
        LevelBuilderEditorUI::Unregister(sHooks, sHookId);
        sHooks->RemoveAllHooks(sHookId);
        sHooks = nullptr;
    }
#endif

    LevelBuilderRegistry::Get().Shutdown();
    LBKitRegistry::Get().Clear();

    if (sEngineAPI && sEngineAPI->LogDebug)
        sEngineAPI->LogDebug("[LevelBuilderCore] unloaded");

    sEngineAPI = nullptr;
    sHookId = 0;
}

static void RegisterTypes(void* /*nodeFactory*/)
{
    // No engine RTTI types in core — placed pieces are plain Polyphase nodes
    // spawned through the engine API. Siblings may register their own types.
}

static void RegisterScriptFuncs(struct lua_State* /*L*/)
{
    // Lua surface is a stretch goal; nothing for MVP.
}

#if EDITOR
static void RegisterEditorUI(EditorUIHooks* hooks, uint64_t hookId)
{
    sHooks  = hooks;
    sHookId = hookId;
    LevelBuilderEditorUI::Register(hooks, hookId);
}
#endif

extern "C" OCTAVE_PLUGIN_API int PolyphasePlugin_GetDesc(PolyphasePluginDesc* desc)
{
    desc->apiVersion       = OCTAVE_PLUGIN_API_VERSION;
    desc->pluginName       = "com.polyphase.editor.levelbuilder.core";
    desc->pluginVersion    = "0.1.0";
    desc->OnLoad           = OnLoad;
    desc->OnUnload         = OnUnload;
    desc->Tick             = nullptr;
    desc->TickEditor       = nullptr;
    desc->RegisterTypes    = RegisterTypes;
    desc->RegisterScriptFuncs = RegisterScriptFuncs;
#if EDITOR
    desc->RegisterEditorUI = RegisterEditorUI;
#else
    desc->RegisterEditorUI = nullptr;
#endif
    desc->OnEditorPreInit  = nullptr;
    desc->OnEditorReady    = nullptr;
    return 0;
}
