#include "LevelBuilderRegistry.h"

#include "LevelBuilderInterfaces.h"
#include "Plugins/PolyphaseEngineAPI.h"

#include <algorithm>
#include <cstring>
#include <vector>

LevelBuilderRegistry& LevelBuilderRegistry::Get()
{
    static LevelBuilderRegistry sInstance;
    return sInstance;
}

void LevelBuilderRegistry::Initialize(PolyphaseEngineAPI* api)
{
    mEngineAPI = api;
    mContext.mEngineAPI = api;
    mContext.mLastError[0] = 0;
}

void LevelBuilderRegistry::Shutdown()
{
    // Deactivate active tool first so siblings can clean up state.
    if (!mActiveTool.empty())
    {
        if (LevelBuilderTool* t = FindTool(mActiveTool.c_str()))
            t->Deactivate();
    }

    // Drop external references — siblings own them and are responsible
    // for freeing. Our owned palettes are destroyed by unique_ptr below.
    mTools.clear();
    mToolOrder.clear();
    mActiveTool.clear();

    mBrushes.clear();
    mBrushOrder.clear();
    mActiveBrush.clear();

    mSnaps.clear();
    mSnapOrder.clear();
    mActiveSnap.clear();

    mExternalPalettes.clear();
    mOwnedPalettes.clear();
    mPaletteOrder.clear();
    mActivePalette.clear();

    mExtensionTabs.clear();
    mToolViewportSubs.clear();
    mSpawnFns.clear();

    mPreviewAsset.clear();
    mPreviewState = LBPreview_Hidden;

    mEngineAPI = nullptr;
    mContext.mEngineAPI = nullptr;
    mContext.mLastError[0] = 0;
}

// ===== Tools =====================================================================

void LevelBuilderRegistry::RegisterTool(const char* name, LevelBuilderTool* tool)
{
    if (!name || !*name || !tool) return;
    std::string key = name;
    if (mTools.find(key) == mTools.end())
        mToolOrder.push_back(key);
    mTools[key] = tool;
}

void LevelBuilderRegistry::UnregisterTool(const char* name)
{
    if (!name) return;
    std::string key = name;
    auto it = mTools.find(key);
    if (it == mTools.end()) return;
    if (mActiveTool == key)
    {
        it->second->Deactivate();
        mActiveTool.clear();
    }
    mTools.erase(it);
    mToolOrder.erase(std::remove(mToolOrder.begin(), mToolOrder.end(), key), mToolOrder.end());
}

LevelBuilderTool* LevelBuilderRegistry::FindTool(const char* name)
{
    if (!name) return nullptr;
    auto it = mTools.find(name);
    return it == mTools.end() ? nullptr : it->second;
}

void LevelBuilderRegistry::SetActiveTool(const char* name)
{
    std::string nextName = name ? name : "";
    if (nextName == mActiveTool) return;
    if (!mActiveTool.empty())
    {
        if (LevelBuilderTool* prev = FindTool(mActiveTool.c_str()))
            prev->Deactivate();
    }
    mActiveTool = nextName;
    if (!mActiveTool.empty())
    {
        if (LevelBuilderTool* next = FindTool(mActiveTool.c_str()))
            next->Activate(&mContext);
        else
            mActiveTool.clear();
    }
}

const char* LevelBuilderRegistry::GetToolNameAt(int index) const
{
    if (index < 0 || index >= (int)mToolOrder.size()) return "";
    return mToolOrder[index].c_str();
}

// ===== Brushes ===================================================================

void LevelBuilderRegistry::RegisterBrush(const char* name, LevelBuilderBrush* brush)
{
    if (!name || !*name || !brush) return;
    std::string key = name;
    if (mBrushes.find(key) == mBrushes.end())
        mBrushOrder.push_back(key);
    mBrushes[key] = brush;
}

void LevelBuilderRegistry::UnregisterBrush(const char* name)
{
    if (!name) return;
    std::string key = name;
    mBrushes.erase(key);
    mBrushOrder.erase(std::remove(mBrushOrder.begin(), mBrushOrder.end(), key), mBrushOrder.end());
    if (mActiveBrush == key) mActiveBrush.clear();
}

LevelBuilderBrush* LevelBuilderRegistry::FindBrush(const char* name)
{
    if (!name) return nullptr;
    auto it = mBrushes.find(name);
    return it == mBrushes.end() ? nullptr : it->second;
}

void LevelBuilderRegistry::SetActiveBrush(const char* name)
{
    mActiveBrush = name ? name : "";
    if (!mActiveBrush.empty() && !FindBrush(mActiveBrush.c_str()))
        mActiveBrush.clear();
}

const char* LevelBuilderRegistry::GetBrushNameAt(int index) const
{
    if (index < 0 || index >= (int)mBrushOrder.size()) return "";
    return mBrushOrder[index].c_str();
}

// ===== Snap providers ============================================================

void LevelBuilderRegistry::RegisterSnapProvider(const char* name, LevelBuilderSnapProvider* sp)
{
    if (!name || !*name || !sp) return;
    std::string key = name;
    if (mSnaps.find(key) == mSnaps.end())
        mSnapOrder.push_back(key);
    mSnaps[key] = sp;
}

void LevelBuilderRegistry::UnregisterSnapProvider(const char* name)
{
    if (!name) return;
    std::string key = name;
    mSnaps.erase(key);
    mSnapOrder.erase(std::remove(mSnapOrder.begin(), mSnapOrder.end(), key), mSnapOrder.end());
    if (mActiveSnap == key) mActiveSnap.clear();
}

LevelBuilderSnapProvider* LevelBuilderRegistry::FindSnapProvider(const char* name)
{
    if (!name) return nullptr;
    auto it = mSnaps.find(name);
    return it == mSnaps.end() ? nullptr : it->second;
}

void LevelBuilderRegistry::SetActiveSnapProvider(const char* name)
{
    mActiveSnap = name ? name : "";
    if (!mActiveSnap.empty() && !FindSnapProvider(mActiveSnap.c_str()))
        mActiveSnap.clear();
}

const char* LevelBuilderRegistry::GetSnapProviderNameAt(int index) const
{
    if (index < 0 || index >= (int)mSnapOrder.size()) return "";
    return mSnapOrder[index].c_str();
}

// ===== Palettes ==================================================================

LevelBuilderPalette* LevelBuilderRegistry::CreatePalette(const char* name)
{
    if (!name || !*name) return nullptr;
    std::string key = name;
    auto it = mOwnedPalettes.find(key);
    if (it != mOwnedPalettes.end())
        return it->second.get();

    auto p = std::make_unique<LevelBuilderPalette>(key);
    LevelBuilderPalette* raw = p.get();
    mOwnedPalettes[key] = std::move(p);
    if (std::find(mPaletteOrder.begin(), mPaletteOrder.end(), key) == mPaletteOrder.end())
        mPaletteOrder.push_back(key);
    return raw;
}

void LevelBuilderRegistry::RegisterPalette(const char* name, LevelBuilderPalette* palette)
{
    if (!name || !*name || !palette) return;
    std::string key = name;
    mExternalPalettes[key] = palette;
    if (std::find(mPaletteOrder.begin(), mPaletteOrder.end(), key) == mPaletteOrder.end())
        mPaletteOrder.push_back(key);
}

void LevelBuilderRegistry::UnregisterPalette(const char* name)
{
    if (!name) return;
    std::string key = name;
    mExternalPalettes.erase(key);
    mOwnedPalettes.erase(key);
    mPaletteOrder.erase(std::remove(mPaletteOrder.begin(), mPaletteOrder.end(), key), mPaletteOrder.end());
    if (mActivePalette == key) mActivePalette.clear();
}

LevelBuilderPalette* LevelBuilderRegistry::FindPalette(const char* name)
{
    return FindPaletteAnywhere(name ? name : "");
}

LevelBuilderPalette* LevelBuilderRegistry::FindPaletteAnywhere(const std::string& name)
{
    if (name.empty()) return nullptr;
    auto it = mOwnedPalettes.find(name);
    if (it != mOwnedPalettes.end()) return it->second.get();
    auto eit = mExternalPalettes.find(name);
    if (eit != mExternalPalettes.end()) return eit->second;
    return nullptr;
}

void LevelBuilderRegistry::SetActivePalette(const char* name)
{
    mActivePalette = name ? name : "";
    if (!mActivePalette.empty() && !FindPaletteAnywhere(mActivePalette))
        mActivePalette.clear();
}

LevelBuilderPalette* LevelBuilderRegistry::GetActivePaletteObject()
{
    return FindPaletteAnywhere(mActivePalette);
}

const char* LevelBuilderRegistry::GetPaletteNameAt(int index) const
{
    if (index < 0 || index >= (int)mPaletteOrder.size()) return "";
    return mPaletteOrder[index].c_str();
}

// ===== Placement =================================================================

LevelBuilderPlacementResult LevelBuilderRegistry::Place(const LevelBuilderPlacementRequest& request)
{
    LevelBuilderPlacementResult result{};
    result.success = 0;
    result.spawnedNode = nullptr;
    result.errorMessage = nullptr;

    LevelBuilderBrush* brush = FindBrush(mActiveBrush.c_str());
    if (!brush)
    {
        SetLastError("No active brush");
        result.errorMessage = GetLastError();
        return result;
    }
    if (!brush->CanPlace(request))
    {
        SetLastError("Brush rejected placement");
        result.errorMessage = GetLastError();
        return result;
    }
    result = brush->Place(request);
    if (!result.success && (!result.errorMessage || !*result.errorMessage))
    {
        SetLastError("Brush::Place failed");
        result.errorMessage = GetLastError();
    }
    else if (result.success)
    {
        mContext.mLastError[0] = 0;
    }
    return result;
}

// ===== Preview ===================================================================

void LevelBuilderRegistry::SetPreviewAsset(const char* assetName)
{
    mPreviewAsset = assetName ? assetName : "";
}

void LevelBuilderRegistry::SetPreviewTransform(const LBVec3& pos, const LBQuat& rot, const LBVec3& scale)
{
    mPreviewPos = pos;
    mPreviewRot = rot;
    mPreviewScale = scale;
}

void LevelBuilderRegistry::GetPreviewTransform(LBVec3& outPos, LBQuat& outRot, LBVec3& outScale) const
{
    outPos = mPreviewPos;
    outRot = mPreviewRot;
    outScale = mPreviewScale;
}

// ===== Extension tabs ============================================================

void LevelBuilderRegistry::RegisterExtensionTab(const char* name,
                                                LevelBuilderCoreAPI::ExtensionTabDrawFn drawFn,
                                                void* userData)
{
    if (!name || !*name || !drawFn) return;
    UnregisterExtensionTab(name);
    ExtensionTab tab;
    tab.name = name;
    tab.drawFn = drawFn;
    tab.userData = userData;
    mExtensionTabs.push_back(std::move(tab));
}

void LevelBuilderRegistry::UnregisterExtensionTab(const char* name)
{
    if (!name) return;
    std::string key = name;
    mExtensionTabs.erase(
        std::remove_if(mExtensionTabs.begin(), mExtensionTabs.end(),
            [&](const ExtensionTab& t){ return t.name == key; }),
        mExtensionTabs.end());
}

// ===== Tool viewport-input subscriptions (v2) ====================================

void LevelBuilderRegistry::RegisterToolViewportInput(const char* toolName,
                                                     LevelBuilderCoreAPI::LBViewportInputFn cb,
                                                     void* userData)
{
    if (!toolName || !*toolName) return;
    if (cb == nullptr)
    {
        mToolViewportSubs.erase(toolName);
        return;
    }
    ToolViewportSub sub;
    sub.cb = cb;
    sub.userData = userData;
    mToolViewportSubs[toolName] = sub;
}

void LevelBuilderRegistry::UnregisterToolViewportInput(const char* toolName)
{
    if (!toolName) return;
    mToolViewportSubs.erase(toolName);
}

const LevelBuilderRegistry::ToolViewportSub*
LevelBuilderRegistry::FindToolViewportSub(const char* toolName) const
{
    if (!toolName) return nullptr;
    auto it = mToolViewportSubs.find(toolName);
    if (it == mToolViewportSubs.end()) return nullptr;
    return &it->second;
}

// ===== Brush spawn-fn hooks (v4) =================================================

void LevelBuilderRegistry::RegisterSpawnFn(const char* toolName,
                                           LevelBuilderCoreAPI::LBBrushSpawnFn fn,
                                           void* userData)
{
    if (!toolName || !*toolName) return;
    if (fn == nullptr)
    {
        mSpawnFns.erase(toolName);
        return;
    }
    SpawnFnEntry e;
    e.fn = fn;
    e.userData = userData;
    mSpawnFns[toolName] = e;
}

void LevelBuilderRegistry::UnregisterSpawnFn(const char* toolName)
{
    if (!toolName) return;
    mSpawnFns.erase(toolName);
}

const LevelBuilderRegistry::SpawnFnEntry*
LevelBuilderRegistry::FindSpawnFn(const char* toolName) const
{
    if (!toolName) return nullptr;
    auto it = mSpawnFns.find(toolName);
    if (it == mSpawnFns.end()) return nullptr;
    return &it->second;
}

// ===== Placed-piece enumeration hooks (v7) =======================================

void LevelBuilderRegistry::RegisterEnumerateFn(const char* toolName,
                                               LevelBuilderCoreAPI::LBEnumeratePlacementsFn fn,
                                               void* userData)
{
    if (!toolName || !*toolName) return;
    if (fn == nullptr)
    {
        mEnumerateFns.erase(toolName);
        return;
    }
    EnumerateFnEntry e;
    e.fn = fn;
    e.userData = userData;
    mEnumerateFns[toolName] = e;
}

void LevelBuilderRegistry::UnregisterEnumerateFn(const char* toolName)
{
    if (!toolName) return;
    mEnumerateFns.erase(toolName);
}

const LevelBuilderRegistry::EnumerateFnEntry*
LevelBuilderRegistry::FindEnumerateFn(const char* toolName) const
{
    if (!toolName) return nullptr;
    auto it = mEnumerateFns.find(toolName);
    if (it == mEnumerateFns.end()) return nullptr;
    return &it->second;
}

// ===== Rebuild-from-world hooks (v13) ============================================

void LevelBuilderRegistry::RegisterRebuildFromWorldFn(const char* toolName,
                                                      LevelBuilderCoreAPI::LBRebuildFromWorldFn fn,
                                                      void* userData)
{
    if (!toolName || !*toolName) return;
    if (fn == nullptr) { mRebuildFns.erase(toolName); return; }
    RebuildFnEntry e;
    e.fn = fn;
    e.userData = userData;
    mRebuildFns[toolName] = e;
}

void LevelBuilderRegistry::UnregisterRebuildFromWorldFn(const char* toolName)
{
    if (!toolName) return;
    mRebuildFns.erase(toolName);
}

void LevelBuilderRegistry::RebuildAllFromWorld()
{
    // Snapshot the entries so a callback that touches the registry
    // (e.g. unregisters itself) can't invalidate the iterator.
    std::vector<RebuildFnEntry> snapshot;
    snapshot.reserve(mRebuildFns.size());
    for (const auto& kv : mRebuildFns) snapshot.push_back(kv.second);
    for (const RebuildFnEntry& e : snapshot)
        if (e.fn) e.fn(e.userData);
}

// ===== Diagnostics ===============================================================

void LevelBuilderRegistry::SetLastError(const char* msg)
{
    if (!msg) { mContext.mLastError[0] = 0; return; }
    std::strncpy(mContext.mLastError, msg, sizeof(mContext.mLastError) - 1);
    mContext.mLastError[sizeof(mContext.mLastError) - 1] = 0;
}
