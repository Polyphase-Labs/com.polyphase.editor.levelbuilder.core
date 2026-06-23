/**
 * @file LevelBuilderRegistry.h
 * @brief Central singleton that owns palettes and holds weak refs to
 *        sibling-addon-owned tools/brushes/snap providers.
 *
 * Lifetime model:
 *   - Core OWNS LevelBuilderPalette objects (created via CreatePalette).
 *     They live as long as core stays loaded.
 *   - Sibling addons OWN their LevelBuilderTool/Brush/SnapProvider derived
 *     instances. Core just holds raw pointers in a name→ptr map.
 *
 *   When a sibling unloads it MUST call UnregisterTool/Brush/SnapProvider
 *   for everything it registered, BEFORE destroying its own instances —
 *   otherwise core will dereference freed memory next frame.
 */

#pragma once

#include "LevelBuilderCoreAPI.h"
#include "LevelBuilderContext.h"
#include "LevelBuilderPalette.h"

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

struct PolyphaseEngineAPI;

class LevelBuilderRegistry
{
public:
    static LevelBuilderRegistry& Get();

    // Called once by ComPolyphaseEditorLevelbuilderCore.cpp::OnLoad.
    void Initialize(PolyphaseEngineAPI* api);

    // Called by OnUnload. Tears down owned palettes, clears all maps,
    // resets the active tool/brush/snap/palette.
    void Shutdown();

    PolyphaseEngineAPI* GetEngineAPI() const { return mEngineAPI; }
    LevelBuilderContext* GetContext() { return &mContext; }
    LevelBuilderContext& GetContextRef() { return mContext; }

    // ---- Tool registry ----
    void RegisterTool(const char* name, LevelBuilderTool* tool);
    void UnregisterTool(const char* name);
    LevelBuilderTool* FindTool(const char* name);
    void SetActiveTool(const char* name);
    const std::string& GetActiveToolName() const { return mActiveTool; }

    int GetToolCount() const { return (int)mToolOrder.size(); }
    const char* GetToolNameAt(int index) const;

    // ---- Brush registry ----
    void RegisterBrush(const char* name, LevelBuilderBrush* brush);
    void UnregisterBrush(const char* name);
    LevelBuilderBrush* FindBrush(const char* name);
    void SetActiveBrush(const char* name);
    const std::string& GetActiveBrushName() const { return mActiveBrush; }

    int GetBrushCount() const { return (int)mBrushOrder.size(); }
    const char* GetBrushNameAt(int index) const;

    // ---- Snap provider registry ----
    void RegisterSnapProvider(const char* name, LevelBuilderSnapProvider* sp);
    void UnregisterSnapProvider(const char* name);
    LevelBuilderSnapProvider* FindSnapProvider(const char* name);
    void SetActiveSnapProvider(const char* name);
    const std::string& GetActiveSnapProviderName() const { return mActiveSnap; }

    int GetSnapProviderCount() const { return (int)mSnapOrder.size(); }
    const char* GetSnapProviderNameAt(int index) const;

    // ---- Palette registry ----
    LevelBuilderPalette* CreatePalette(const char* name);
    void RegisterPalette(const char* name, LevelBuilderPalette* palette);
    void UnregisterPalette(const char* name);
    LevelBuilderPalette* FindPalette(const char* name);
    void SetActivePalette(const char* name);
    const std::string& GetActivePaletteName() const { return mActivePalette; }
    LevelBuilderPalette* GetActivePaletteObject();

    int GetPaletteCount() const { return (int)mPaletteOrder.size(); }
    const char* GetPaletteNameAt(int index) const;

    // ---- Placement ----
    LevelBuilderPlacementResult Place(const LevelBuilderPlacementRequest& request);

    // ---- Preview ghost state ----
    void SetPreviewAsset(const char* assetName);
    const std::string& GetPreviewAsset() const { return mPreviewAsset; }
    void SetPreviewTransform(const LBVec3& pos, const LBQuat& rot, const LBVec3& scale);
    void GetPreviewTransform(LBVec3& outPos, LBQuat& outRot, LBVec3& outScale) const;
    void SetPreviewState(LevelBuilderPreviewState s) { mPreviewState = s; }
    LevelBuilderPreviewState GetPreviewState() const { return mPreviewState; }
    void HidePreview() { mPreviewState = LBPreview_Hidden; }

    // ---- Extension tabs (modular adds its own tab to the window) ----
    struct ExtensionTab
    {
        std::string name;
        LevelBuilderCoreAPI::ExtensionTabDrawFn drawFn;
        void* userData;
    };
    void RegisterExtensionTab(const char* name,
                              LevelBuilderCoreAPI::ExtensionTabDrawFn drawFn,
                              void* userData);
    void UnregisterExtensionTab(const char* name);
    int  GetExtensionTabCount() const { return (int)mExtensionTabs.size(); }
    const ExtensionTab& GetExtensionTab(int index) const { return mExtensionTabs[index]; }

    // ---- Tool viewport-input subscriptions (v2) ----
    // Keyed by tool name; re-registering replaces. Sibling addons that
    // implement click-driven placement subscribe here and the per-frame
    // driver in LevelBuilderEditorUI fires the callback when the active
    // tool's name matches and the viewport reports a click.
    struct ToolViewportSub
    {
        LevelBuilderCoreAPI::LBViewportInputFn cb = nullptr;
        void* userData = nullptr;
    };
    void RegisterToolViewportInput(const char* toolName,
                                   LevelBuilderCoreAPI::LBViewportInputFn cb,
                                   void* userData);
    void UnregisterToolViewportInput(const char* toolName);
    const ToolViewportSub* FindToolViewportSub(const char* toolName) const;

    // ---- Brush spawn-fn hooks (v4) ----
    // Each sibling addon (modular, grid, …) registers ONE spawn callback
    // keyed by tool name. Generic brushes in tool.core route every shape
    // point through the active tool's spawn fn — so a Line brush placing
    // 7 walls invokes modular's StaticMesh-vs-Scene auto-spawn 7 times
    // without tool.core ever knowing what a kit is.
    struct SpawnFnEntry
    {
        LevelBuilderCoreAPI::LBBrushSpawnFn fn = nullptr;
        void* userData = nullptr;
    };
    void RegisterSpawnFn(const char* toolName,
                         LevelBuilderCoreAPI::LBBrushSpawnFn fn,
                         void* userData);
    void UnregisterSpawnFn(const char* toolName);
    const SpawnFnEntry* FindSpawnFn(const char* toolName) const;

    // ---- Placed-piece enumeration hooks (v7) ----
    // Same shape as the spawn-fn registry — each sibling registers one
    // enumerate callback keyed by its tool name. Brushes that need to
    // operate on already-placed nodes (Paint Erase, ReplaceAssetsWith)
    // query GetEnumerateFnForActiveTool() and call back into the sibling.
    struct EnumerateFnEntry
    {
        LevelBuilderCoreAPI::LBEnumeratePlacementsFn fn = nullptr;
        void* userData = nullptr;
    };
    void RegisterEnumerateFn(const char* toolName,
                             LevelBuilderCoreAPI::LBEnumeratePlacementsFn fn,
                             void* userData);
    void UnregisterEnumerateFn(const char* toolName);
    const EnumerateFnEntry* FindEnumerateFn(const char* toolName) const;

    // ---- Diagnostics ----
    void SetLastError(const char* msg);
    const char* GetLastError() const { return mContext.mLastError; }

private:
    LevelBuilderRegistry() = default;
    LevelBuilderRegistry(const LevelBuilderRegistry&) = delete;
    LevelBuilderRegistry& operator=(const LevelBuilderRegistry&) = delete;

    PolyphaseEngineAPI* mEngineAPI = nullptr;
    LevelBuilderContext mContext;

    std::unordered_map<std::string, LevelBuilderTool*>          mTools;
    std::vector<std::string>                                    mToolOrder;
    std::string                                                 mActiveTool;

    std::unordered_map<std::string, LevelBuilderBrush*>         mBrushes;
    std::vector<std::string>                                    mBrushOrder;
    std::string                                                 mActiveBrush;

    std::unordered_map<std::string, LevelBuilderSnapProvider*>  mSnaps;
    std::vector<std::string>                                    mSnapOrder;
    std::string                                                 mActiveSnap;

    // Core OWNS these palettes (unique_ptr) so it can outlive sibling addons.
    std::unordered_map<std::string, std::unique_ptr<LevelBuilderPalette>> mOwnedPalettes;
    // For palettes a sibling instantiated and just registered the pointer
    // (rare — we expect CreatePalette to be the common path):
    std::unordered_map<std::string, LevelBuilderPalette*>       mExternalPalettes;
    std::vector<std::string>                                    mPaletteOrder;
    std::string                                                 mActivePalette;

    std::string                 mPreviewAsset;
    LBVec3                      mPreviewPos{0,0,0};
    LBQuat                      mPreviewRot{0,0,0,1};
    LBVec3                      mPreviewScale{1,1,1};
    LevelBuilderPreviewState    mPreviewState = LBPreview_Hidden;

    std::vector<ExtensionTab>   mExtensionTabs;

    std::unordered_map<std::string, ToolViewportSub> mToolViewportSubs;

    std::unordered_map<std::string, SpawnFnEntry>    mSpawnFns;
    std::unordered_map<std::string, EnumerateFnEntry> mEnumerateFns;

    LevelBuilderPalette* FindPaletteAnywhere(const std::string& name);
};
