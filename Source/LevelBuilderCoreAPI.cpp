/**
 * @file LevelBuilderCoreAPI.cpp
 * @brief C ABI wrappers around LevelBuilderRegistry. The single exported
 *        symbol `LevelBuilderCore_GetAPI` returns a static function-pointer
 *        table that sibling addons resolve via GetProcAddress.
 */

#define LEVELBUILDER_CORE_BUILD 1

#include "LevelBuilderCoreAPI.h"

#include "LBKitRegistry.h"
#include "LBKitTypes.h"
#include "LBKitJson.h"
#include "LBKitShare.h"
#include "LevelBuilderRegistry.h"
#include "LevelBuilderInterfaces.h"
#include "LevelBuilderEditorUI.h"

#include "Plugins/PolyphaseEngineAPI.h"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>

namespace
{
    // ---- Tools ----
    void  C_RegisterTool(const char* n, LevelBuilderTool* t)        { LevelBuilderRegistry::Get().RegisterTool(n, t); }
    void  C_UnregisterTool(const char* n)                           { LevelBuilderRegistry::Get().UnregisterTool(n); }
    LevelBuilderTool* C_FindTool(const char* n)                     { return LevelBuilderRegistry::Get().FindTool(n); }
    void  C_SetActiveTool(const char* n)                            { LevelBuilderRegistry::Get().SetActiveTool(n); }
    const char* C_GetActiveToolName()                               { return LevelBuilderRegistry::Get().GetActiveToolName().c_str(); }

    // ---- Snap providers ----
    void  C_RegisterSnap(const char* n, LevelBuilderSnapProvider* s){ LevelBuilderRegistry::Get().RegisterSnapProvider(n, s); }
    void  C_UnregisterSnap(const char* n)                           { LevelBuilderRegistry::Get().UnregisterSnapProvider(n); }
    LevelBuilderSnapProvider* C_FindSnap(const char* n)             { return LevelBuilderRegistry::Get().FindSnapProvider(n); }
    void  C_SetActiveSnap(const char* n)                            { LevelBuilderRegistry::Get().SetActiveSnapProvider(n); }
    const char* C_GetActiveSnapName()                               { return LevelBuilderRegistry::Get().GetActiveSnapProviderName().c_str(); }

    // ---- Brushes ----
    void  C_RegisterBrush(const char* n, LevelBuilderBrush* b)      { LevelBuilderRegistry::Get().RegisterBrush(n, b); }
    void  C_UnregisterBrush(const char* n)                          { LevelBuilderRegistry::Get().UnregisterBrush(n); }
    LevelBuilderBrush* C_FindBrush(const char* n)                   { return LevelBuilderRegistry::Get().FindBrush(n); }
    void  C_SetActiveBrush(const char* n)                           { LevelBuilderRegistry::Get().SetActiveBrush(n); }
    const char* C_GetActiveBrushName()                              { return LevelBuilderRegistry::Get().GetActiveBrushName().c_str(); }

    // ---- Palettes ----
    void  C_RegisterPalette(const char* n, LevelBuilderPalette* p)  { LevelBuilderRegistry::Get().RegisterPalette(n, p); }
    void  C_UnregisterPalette(const char* n)                        { LevelBuilderRegistry::Get().UnregisterPalette(n); }
    LevelBuilderPalette* C_FindPalette(const char* n)               { return LevelBuilderRegistry::Get().FindPalette(n); }
    void  C_SetActivePalette(const char* n)                         { LevelBuilderRegistry::Get().SetActivePalette(n); }
    const char* C_GetActivePaletteName()                            { return LevelBuilderRegistry::Get().GetActivePaletteName().c_str(); }
    LevelBuilderPalette* C_GetActivePalette()                       { return LevelBuilderRegistry::Get().GetActivePaletteObject(); }

    LevelBuilderPalette* C_CreatePalette(const char* n)             { return LevelBuilderRegistry::Get().CreatePalette(n); }
    void C_Palette_Clear(LevelBuilderPalette* p)                    { if (p) p->Clear(); }
    void C_Palette_AddItem(LevelBuilderPalette* p, const LevelBuilderPaletteItem* it) { if (p && it) p->AddItem(*it); }
    int  C_Palette_GetItemCount(LevelBuilderPalette* p)             { return p ? p->GetItemCount() : 0; }
    int  C_Palette_GetItem(LevelBuilderPalette* p, int i, LevelBuilderPaletteItem* out) { return (p && p->GetItem(i, out)) ? 1 : 0; }
    void C_Palette_SetActiveIndex(LevelBuilderPalette* p, int i)    { if (p) p->SetActiveIndex(i); }
    int  C_Palette_GetActiveIndex(LevelBuilderPalette* p)           { return p ? p->GetActiveIndex() : -1; }

    // ---- Placement ----
    LevelBuilderPlacementResult C_Place(const LevelBuilderPlacementRequest* req)
    {
        if (!req)
        {
            LevelBuilderPlacementResult r{};
            r.success = 0;
            r.errorMessage = "Null request";
            return r;
        }
        return LevelBuilderRegistry::Get().Place(*req);
    }

    // ---- Preview ----
    void C_SetPreviewAsset(const char* a)
    {
        LevelBuilderRegistry::Get().SetPreviewAsset(a);
    }
    void C_SetPreviewTransform(const LBVec3* pos, const LBQuat* rot, const LBVec3* scale)
    {
        LBVec3 p = pos ? *pos : LBVec3{0,0,0};
        LBQuat r = rot ? *rot : LBQuat{0,0,0,1};
        LBVec3 s = scale ? *scale : LBVec3{1,1,1};
        LevelBuilderRegistry::Get().SetPreviewTransform(p, r, s);
    }
    void C_SetPreviewState(int s)
    {
        LevelBuilderRegistry::Get().SetPreviewState((LevelBuilderPreviewState)s);
    }
    void C_HidePreview()                                            { LevelBuilderRegistry::Get().HidePreview(); }
    int  C_GetPreviewState()                                        { return (int)LevelBuilderRegistry::Get().GetPreviewState(); }
    void C_GetPreviewTransform(LBVec3* outP, LBQuat* outR, LBVec3* outS)
    {
        LBVec3 p, s; LBQuat r;
        LevelBuilderRegistry::Get().GetPreviewTransform(p, r, s);
        if (outP) *outP = p;
        if (outR) *outR = r;
        if (outS) *outS = s;
    }
    const char* C_GetPreviewAssetName()                             { return LevelBuilderRegistry::Get().GetPreviewAsset().c_str(); }

    // ---- UI ----
    void C_OpenLevelBuilderWindow()
    {
#if EDITOR
        LevelBuilderEditorUI::OpenWindow();
#endif
    }
    void C_CloseLevelBuilderWindow()
    {
#if EDITOR
        LevelBuilderEditorUI::CloseWindow();
#endif
    }
    int  C_IsLevelBuilderWindowOpen()
    {
#if EDITOR
        return LevelBuilderEditorUI::IsWindowOpen() ? 1 : 0;
#else
        return 0;
#endif
    }

    // ---- Extension tabs ----
    void C_RegisterExtensionTab(const char* name,
                                LevelBuilderCoreAPI::ExtensionTabDrawFn fn,
                                void* ud)
    {
        LevelBuilderRegistry::Get().RegisterExtensionTab(name, fn, ud);
    }
    void C_UnregisterExtensionTab(const char* name)
    {
        LevelBuilderRegistry::Get().UnregisterExtensionTab(name);
    }

    // ---- Logging passthrough ----
    void C_LogDebug(const char* m)
    {
        PolyphaseEngineAPI* api = LevelBuilderRegistry::Get().GetEngineAPI();
        if (api && api->LogDebug) api->LogDebug("%s", m ? m : "");
    }
    void C_LogWarning(const char* m)
    {
        PolyphaseEngineAPI* api = LevelBuilderRegistry::Get().GetEngineAPI();
        if (api && api->LogWarning) api->LogWarning("%s", m ? m : "");
    }
    void C_LogError(const char* m)
    {
        PolyphaseEngineAPI* api = LevelBuilderRegistry::Get().GetEngineAPI();
        if (api && api->LogError) api->LogError("%s", m ? m : "");
    }

    void* C_GetEngineAPI()
    {
        return (void*)LevelBuilderRegistry::Get().GetEngineAPI();
    }

    // ---- v2: viewport hover + click dispatch ----
    int C_Viewport_GetHoverHit(LBVec3* outPos, LBVec3* outNormal, void** outNode)
    {
        const LevelBuilderContext& ctx = LevelBuilderRegistry::Get().GetContextRef();
        if (outPos)    *outPos    = ctx.mLastRaycastHit;
        if (outNormal) *outNormal = ctx.mLastRaycastNormal;
        if (outNode)   *outNode   = ctx.mLastRaycastNode;
        return ctx.mLastRaycastValid ? 1 : 0;
    }

    void C_RegisterToolViewportInput(const char* toolName,
                                     LevelBuilderCoreAPI::LBViewportInputFn cb,
                                     void* userData)
    {
        LevelBuilderRegistry::Get().RegisterToolViewportInput(toolName, cb, userData);
    }

    void C_UnregisterToolViewportInput(const char* toolName)
    {
        LevelBuilderRegistry::Get().UnregisterToolViewportInput(toolName);
    }

    // ===== v3 — kit registry wrappers ===================================
    //
    // All wrappers route through LBKitRegistry::Get(). The const char*
    // outputs in the snapshot structs point into the registry's owned
    // std::string buffers — valid until Kit_Reload / Kit_Save / any
    // mutation. Siblings that need stable pointers across frames must
    // copy out of the snapshot immediately.

    static LBPiece* GetMutablePiece(int kitIdx, int pieceIdx)
    {
        LBKit* k = LBKitRegistry::Get().FindKitByIndex(kitIdx);
        if (!k) return nullptr;
        if (pieceIdx < 0 || pieceIdx >= (int)k->pieces.size()) return nullptr;
        return &k->pieces[pieceIdx];
    }

    static const LBSocket* GetSocketConst(int kitIdx, int pieceIdx, int socketIdx)
    {
        const LBPiece* p = GetMutablePiece(kitIdx, pieceIdx);
        if (!p) return nullptr;
        if (socketIdx < 0 || socketIdx >= (int)p->sockets.size()) return nullptr;
        return &p->sockets[socketIdx];
    }

    // --- Enumeration ---
    int  C_Kit_GetCount()                                       { return LBKitRegistry::Get().GetKitCount(); }
    const char* C_Kit_GetNameAt(int idx)
    {
        return LBKitRegistry::Get().GetKitNameAt(idx).c_str();
    }
    int  C_Kit_FindIndex(const char* name)
    {
        if (!name) return -1;
        return LBKitRegistry::Get().FindKitIndex(name);
    }
    int  C_Kit_GetInfo(int kitIdx, LBKitInfo* out)
    {
        if (!out) return 0;
        LBKit* k = LBKitRegistry::Get().FindKitByIndex(kitIdx);
        if (!k) return 0;
        out->name       = k->name.c_str();
        out->sourceFile = k->sourceFile.c_str();
        out->pieceCount = (int)k->pieces.size();
        return 1;
    }
    int  C_Kit_GetPieceInfo(int kitIdx, int pieceIdx, LBPieceInfo* out)
    {
        if (!out) return 0;
        const LBPiece* p = GetMutablePiece(kitIdx, pieceIdx);
        if (!p) return 0;
        out->name        = p->name.c_str();
        out->assetName   = p->assetName.c_str();
        out->category    = p->category.c_str();
        out->iconPath    = p->iconPath.c_str();
        out->size[0]     = p->size[0];
        out->size[1]     = p->size[1];
        out->size[2]     = p->size[2];
        out->socketCount = (int)p->sockets.size();
        return 1;
    }
    int  C_Kit_FindPieceByAsset(int kitIdx, const char* assetName)
    {
        if (!assetName) return -1;
        LBKit* k = LBKitRegistry::Get().FindKitByIndex(kitIdx);
        if (!k) return -1;
        for (int i = 0; i < (int)k->pieces.size(); ++i)
            if (k->pieces[i].assetName == assetName) return i;
        return -1;
    }
    int  C_Kit_GetSocketInfo(int kitIdx, int pieceIdx, int socketIdx, LBSocketInfo* out)
    {
        if (!out) return 0;
        const LBSocket* s = GetSocketConst(kitIdx, pieceIdx, socketIdx);
        if (!s) return 0;
        out->name                = s->name.c_str();
        out->type                = s->type.c_str();
        out->position[0]         = s->localPos[0];
        out->position[1]         = s->localPos[1];
        out->position[2]         = s->localPos[2];
        out->rotationDeg[0]      = s->localEulerDeg[0];
        out->rotationDeg[1]      = s->localEulerDeg[1];
        out->rotationDeg[2]      = s->localEulerDeg[2];
        out->compatibleTypeCount = (int)s->compatibleTypes.size();
        return 1;
    }
    const char* C_Kit_GetSocketCompatibleType(int kitIdx, int pieceIdx, int socketIdx, int compatIdx)
    {
        const LBSocket* s = GetSocketConst(kitIdx, pieceIdx, socketIdx);
        if (!s) return nullptr;
        if (compatIdx < 0 || compatIdx >= (int)s->compatibleTypes.size()) return nullptr;
        return s->compatibleTypes[compatIdx].c_str();
    }

    // --- Active selection ---
    const char* C_Kit_GetActiveName()           { return LBKitRegistry::Get().GetActiveKitName().c_str(); }
    void        C_Kit_SetActiveByName(const char* n) { LBKitRegistry::Get().SetActiveKit(n ? n : ""); }
    int         C_Kit_GetActiveIndex()
    {
        const std::string& name = LBKitRegistry::Get().GetActiveKitName();
        if (name.empty()) return -1;
        return LBKitRegistry::Get().FindKitIndex(name);
    }

    // --- Mutation ---
    void C_Kit_SetPieceSize(int kitIdx, int pieceIdx, float x, float y, float z)
    {
        LBPiece* p = GetMutablePiece(kitIdx, pieceIdx);
        if (!p) return;
        p->size[0] = x; p->size[1] = y; p->size[2] = z;
    }

    // --- I/O ---
    int C_Kit_Save(int kitIdx, char* outErrBuf, int errBufSize)
    {
        auto writeErr = [outErrBuf, errBufSize](const std::string& m)
        {
            if (!outErrBuf || errBufSize <= 0) return;
            std::snprintf(outErrBuf, errBufSize, "%s", m.c_str());
        };
        LBKit* k = LBKitRegistry::Get().FindKitByIndex(kitIdx);
        if (!k)
        {
            writeErr(std::string("Kit_Save: kit index out of range"));
            return 1;
        }
        if (k->sourceFile.empty())
        {
            writeErr(std::string("Kit_Save: kit '") + k->name + "' has no source file");
            return 2;
        }
        std::string err;
        if (!LBKitJson::SaveToFile(k->sourceFile, *k, err))
        {
            writeErr(err);
            return 3;
        }
        if (outErrBuf && errBufSize > 0) outErrBuf[0] = '\0';
        return 0;
    }
    int  C_Kit_Reload()                          { return LBKitRegistry::Get().ScanProjectKits(); }
    int  C_Kit_GetLastReloadKitCount()           { return LBKitRegistry::Get().GetLastReloadKitCount(); }
    int  C_Kit_GetLastReloadErrorCount()         { return LBKitRegistry::Get().GetLastReloadErrorCount(); }
    const char* C_Kit_GetLastReloadError(int i)
    {
        const auto& errs = LBKitRegistry::Get().GetLastReloadErrors();
        if (i < 0 || i >= (int)errs.size()) return nullptr;
        return errs[i].c_str();
    }

    // --- Project root ---
    const char* C_GetProjectRoot()               { return LBKitRegistry::GetProjectRoot().c_str(); }
    const char* C_GetProjectKitsFolder()         { return LBKitRegistry::GetProjectKitsFolder().c_str(); }
    void C_SetProjectRootOverride(const char* p) { LBKitRegistry::SetProjectRootOverride(p); }
    void C_ClearProjectRootCache()               { LBKitRegistry::ClearProjectRootCache(); }

    // --- Math ---
    int  C_Kit_ComposeSocketWorld(int kitIdx, int pieceIdx, int socketIdx,
                                  const LBVec3* pieceWorldPos,
                                  const LBQuat* pieceWorldRot,
                                  LBVec3* outPos, LBQuat* outRot)
    {
        const LBSocket* s = GetSocketConst(kitIdx, pieceIdx, socketIdx);
        if (!s || !pieceWorldPos || !pieceWorldRot || !outPos || !outRot) return 0;
        LBComposeSocketWorld(*pieceWorldPos, *pieceWorldRot, *s, *outPos, *outRot);
        return 1;
    }
    int  C_Kit_SocketsAreCompatible(int kitIdx,
                                    int piA, int sA, int piB, int sB)
    {
        const LBSocket* a = GetSocketConst(kitIdx, piA, sA);
        const LBSocket* b = GetSocketConst(kitIdx, piB, sB);
        if (!a || !b) return 0;
        return LBSocketsAreCompatible(*a, *b) ? 1 : 0;
    }
    void C_Kit_EulerDegToQuat(float pitchDeg, float yawDeg, float rollDeg, LBQuat* out)
    {
        if (!out) return;
        float e[3] = { pitchDeg, yawDeg, rollDeg };
        LBEulerDegToQuat(e, *out);
    }

    // ===== v4 — brush spawn-fn hooks =====
    void C_RegisterSpawnFn(const char* n,
                           LevelBuilderCoreAPI::LBBrushSpawnFn fn,
                           void* ud)
    {
        LevelBuilderRegistry::Get().RegisterSpawnFn(n, fn, ud);
    }
    void C_UnregisterSpawnFn(const char* n)
    {
        LevelBuilderRegistry::Get().UnregisterSpawnFn(n);
    }
    LevelBuilderCoreAPI::LBBrushSpawnFn C_GetSpawnFnForActiveTool(void** outUd)
    {
        const std::string& active = LevelBuilderRegistry::Get().GetActiveToolName();
        const LevelBuilderRegistry::SpawnFnEntry* e =
            LevelBuilderRegistry::Get().FindSpawnFn(active.c_str());
        if (!e)
        {
            if (outUd) *outUd = nullptr;
            return nullptr;
        }
        if (outUd) *outUd = e->userData;
        return e->fn;
    }

    // ===== v7 — Paint brush drag input + placed-piece enumeration =====
    int  C_Viewport_IsLmbDown()
    {
        return LevelBuilderRegistry::Get().GetContextRef().mLmbDown ? 1 : 0;
    }
    int  C_Viewport_IsRmbDown()
    {
        return LevelBuilderRegistry::Get().GetContextRef().mRmbDown ? 1 : 0;
    }
    void C_RegisterEnumerateFn(const char* n,
                               LevelBuilderCoreAPI::LBEnumeratePlacementsFn fn,
                               void* ud)
    {
        LevelBuilderRegistry::Get().RegisterEnumerateFn(n, fn, ud);
    }
    void C_UnregisterEnumerateFn(const char* n)
    {
        LevelBuilderRegistry::Get().UnregisterEnumerateFn(n);
    }
    LevelBuilderCoreAPI::LBEnumeratePlacementsFn C_GetEnumerateFnForActiveTool(void** outUd)
    {
        const std::string& active = LevelBuilderRegistry::Get().GetActiveToolName();
        const LevelBuilderRegistry::EnumerateFnEntry* e =
            LevelBuilderRegistry::Get().FindEnumerateFn(active.c_str());
        if (!e)
        {
            if (outUd) *outUd = nullptr;
            return nullptr;
        }
        if (outUd) *outUd = e->userData;
        return e->fn;
    }

    // v8 / C_History_* wrappers removed in v9. Undo now lives in the
    // engine's ActionManager — see PolyphaseEngineAPI::EditorAction_*.

    // ===== v5 — socket mutation =====
    static LBSocket* GetMutableSocket(int kitIdx, int pieceIdx, int socketIdx)
    {
        LBPiece* p = GetMutablePiece(kitIdx, pieceIdx);
        if (!p) return nullptr;
        if (socketIdx < 0 || socketIdx >= (int)p->sockets.size()) return nullptr;
        return &p->sockets[socketIdx];
    }

    int C_Kit_AddSocket(int kitIdx, int pieceIdx)
    {
        LBPiece* p = GetMutablePiece(kitIdx, pieceIdx);
        if (!p) return -1;
        LBSocket s;
        s.name = "Socket";
        s.type = "Wall";
        p->sockets.push_back(std::move(s));
        return (int)p->sockets.size() - 1;
    }

    int C_Kit_RemoveSocket(int kitIdx, int pieceIdx, int socketIdx)
    {
        LBPiece* p = GetMutablePiece(kitIdx, pieceIdx);
        if (!p) return 0;
        if (socketIdx < 0 || socketIdx >= (int)p->sockets.size()) return 0;
        p->sockets.erase(p->sockets.begin() + socketIdx);
        return 1;
    }

    void C_Kit_SetSocketName(int kitIdx, int pieceIdx, int socketIdx, const char* name)
    {
        LBSocket* s = GetMutableSocket(kitIdx, pieceIdx, socketIdx);
        if (!s) return;
        s->name = name ? name : "";
    }

    void C_Kit_SetSocketType(int kitIdx, int pieceIdx, int socketIdx, const char* type)
    {
        LBSocket* s = GetMutableSocket(kitIdx, pieceIdx, socketIdx);
        if (!s) return;
        s->type = type ? type : "";
    }

    void C_Kit_SetSocketPosition(int kitIdx, int pieceIdx, int socketIdx,
                                 float x, float y, float z)
    {
        LBSocket* s = GetMutableSocket(kitIdx, pieceIdx, socketIdx);
        if (!s) return;
        s->localPos[0] = x; s->localPos[1] = y; s->localPos[2] = z;
    }

    void C_Kit_SetSocketRotation(int kitIdx, int pieceIdx, int socketIdx,
                                 float pitchDeg, float yawDeg, float rollDeg)
    {
        LBSocket* s = GetMutableSocket(kitIdx, pieceIdx, socketIdx);
        if (!s) return;
        s->localEulerDeg[0] = pitchDeg;
        s->localEulerDeg[1] = yawDeg;
        s->localEulerDeg[2] = rollDeg;
    }

    void C_Kit_SetSocketCompatibleTypes(int kitIdx, int pieceIdx, int socketIdx,
                                        const char* const* types, int count)
    {
        LBSocket* s = GetMutableSocket(kitIdx, pieceIdx, socketIdx);
        if (!s) return;
        s->compatibleTypes.clear();
        if (count <= 0 || !types) return;
        s->compatibleTypes.reserve(count);
        for (int i = 0; i < count; ++i)
            s->compatibleTypes.emplace_back(types[i] ? types[i] : "");
    }

    // ===== v6 — kit sharing metadata =====================================

    int C_Kit_GetMetaInfo(int kitIdx, LBKitMetaInfo* out)
    {
        if (!out) return 0;
        LBKit* k = LBKitRegistry::Get().FindKitByIndex(kitIdx);
        if (!k) return 0;
        out->kitId                   = k->kitId.c_str();
        out->kitVersion              = k->kitVersion.c_str();
        out->author                  = k->author.c_str();
        out->authorUrl               = k->authorUrl.c_str();
        out->license                 = k->license.c_str();
        out->licenseUrl              = k->licenseUrl.c_str();
        out->description             = k->description.c_str();
        out->previewImage            = k->previewImage.c_str();
        out->homepage                = k->homepage.c_str();
        out->minimumPolyphaseVersion = k->minimumPolyphaseVersion.c_str();
        out->tagCount                = (int)k->tags.size();
        out->dependencyCount         = (int)k->dependencies.size();
        out->kitIdSynthesized        = k->kitIdSynthesized ? 1 : 0;
        return 1;
    }

    int C_Kit_GetTagCount(int kitIdx)
    {
        LBKit* k = LBKitRegistry::Get().FindKitByIndex(kitIdx);
        return k ? (int)k->tags.size() : 0;
    }
    const char* C_Kit_GetTagAt(int kitIdx, int tagIdx)
    {
        LBKit* k = LBKitRegistry::Get().FindKitByIndex(kitIdx);
        if (!k) return nullptr;
        if (tagIdx < 0 || tagIdx >= (int)k->tags.size()) return nullptr;
        return k->tags[tagIdx].c_str();
    }
    void C_Kit_ClearTags(int kitIdx)
    {
        if (LBKit* k = LBKitRegistry::Get().FindKitByIndex(kitIdx))
            k->tags.clear();
    }
    void C_Kit_AddTag(int kitIdx, const char* tag)
    {
        if (!tag || !*tag) return;
        if (LBKit* k = LBKitRegistry::Get().FindKitByIndex(kitIdx))
            k->tags.emplace_back(tag);
    }

    int C_Kit_GetDependencyInfo(int kitIdx, int depIdx, LBKitDependencyInfo* out)
    {
        if (!out) return 0;
        LBKit* k = LBKitRegistry::Get().FindKitByIndex(kitIdx);
        if (!k) return 0;
        if (depIdx < 0 || depIdx >= (int)k->dependencies.size()) return 0;
        out->kitId   = k->dependencies[depIdx].kitId.c_str();
        out->version = k->dependencies[depIdx].version.c_str();
        return 1;
    }

    void C_Kit_SetMetaString(int kitIdx, int field, const char* value)
    {
        LBKit* k = LBKitRegistry::Get().FindKitByIndex(kitIdx);
        if (!k) return;
        const std::string v = value ? value : "";
        switch (field)
        {
        case LBKitMeta_KitId:
            k->kitId = v;
            // User-set kitId is no longer the synthesized placeholder.
            if (!v.empty()) k->kitIdSynthesized = false;
            break;
        case LBKitMeta_KitVersion:              k->kitVersion              = v; break;
        case LBKitMeta_Author:                  k->author                  = v; break;
        case LBKitMeta_AuthorUrl:               k->authorUrl               = v; break;
        case LBKitMeta_License:                 k->license                 = v; break;
        case LBKitMeta_LicenseUrl:              k->licenseUrl              = v; break;
        case LBKitMeta_Description:             k->description             = v; break;
        case LBKitMeta_PreviewImage:            k->previewImage            = v; break;
        case LBKitMeta_Homepage:                k->homepage                = v; break;
        case LBKitMeta_MinimumPolyphaseVersion: k->minimumPolyphaseVersion = v; break;
        default: break;   // unknown field — no-op for forward compat
        }
    }

    int C_Kit_ExportToKitFile(int kitIdx, const char* dstPath,
                              char* outErrBuf, int errBufSize)
    {
        auto writeErr = [&](const std::string& m) {
            if (!outErrBuf || errBufSize <= 0) return;
            std::snprintf(outErrBuf, errBufSize, "%s", m.c_str());
        };
        if (!dstPath) { writeErr("Kit_ExportToKitFile: dstPath is null"); return 1; }
        std::string err;
        if (!LBKitShare::ExportKit(kitIdx, dstPath, err)) { writeErr(err); return 2; }
        if (outErrBuf && errBufSize > 0) outErrBuf[0] = '\0';
        return 0;
    }

    int C_Kit_PreviewKitFile(const char* srcPath,
                             LBKitMetaInfo* outMeta,
                             char* outName, int outNameSize,
                             char* outErrBuf, int errBufSize)
    {
        auto writeErr = [&](const std::string& m) {
            if (!outErrBuf || errBufSize <= 0) return;
            std::snprintf(outErrBuf, errBufSize, "%s", m.c_str());
        };
        if (!srcPath) { writeErr("Kit_PreviewKitFile: srcPath is null"); return 1; }
        LBKit kit;
        std::string err;
        if (!LBKitShare::PreviewKitFile(srcPath, kit, err)) { writeErr(err); return 2; }

        // The kit lives on the caller's stack so the const char* pointers
        // inside outMeta only stay valid through this function's lifetime.
        // For the preview dialog the caller copies what it needs into its
        // own buffers immediately — that's why this getter is a one-shot
        // peek, not a registry entry.
        if (outMeta)
        {
            // Use thread-local strings so the const char* pointers stay
            // valid until the next Kit_PreviewKitFile call on this thread.
            static thread_local LBKit sCached;
            sCached = std::move(kit);
            outMeta->kitId                   = sCached.kitId.c_str();
            outMeta->kitVersion              = sCached.kitVersion.c_str();
            outMeta->author                  = sCached.author.c_str();
            outMeta->authorUrl               = sCached.authorUrl.c_str();
            outMeta->license                 = sCached.license.c_str();
            outMeta->licenseUrl              = sCached.licenseUrl.c_str();
            outMeta->description             = sCached.description.c_str();
            outMeta->previewImage            = sCached.previewImage.c_str();
            outMeta->homepage                = sCached.homepage.c_str();
            outMeta->minimumPolyphaseVersion = sCached.minimumPolyphaseVersion.c_str();
            outMeta->tagCount                = (int)sCached.tags.size();
            outMeta->dependencyCount         = (int)sCached.dependencies.size();
            outMeta->kitIdSynthesized        = sCached.kitIdSynthesized ? 1 : 0;
            if (outName && outNameSize > 0)
                std::snprintf(outName, outNameSize, "%s", sCached.name.c_str());
        }
        else if (outName && outNameSize > 0)
        {
            std::snprintf(outName, outNameSize, "%s", kit.name.c_str());
        }
        if (outErrBuf && errBufSize > 0) outErrBuf[0] = '\0';
        return 0;
    }

    int C_Kit_ImportKitFile(const char* srcPath, int conflictMode,
                            char* outDstJsonPath, int outDstSize,
                            char* outErrBuf, int errBufSize)
    {
        auto writeErr = [&](const std::string& m) {
            if (!outErrBuf || errBufSize <= 0) return;
            std::snprintf(outErrBuf, errBufSize, "%s", m.c_str());
        };
        if (!srcPath) { writeErr("Kit_ImportKitFile: srcPath is null"); return 1; }

        std::string dst, kitId, err;
        const auto mode = (conflictMode == 0) ? LBKitShare::ConflictMode::Replace
                        : (conflictMode == 2) ? LBKitShare::ConflictMode::Abort
                                              : LBKitShare::ConflictMode::RenameNew;
        if (!LBKitShare::ImportKit(srcPath, mode, dst, kitId, err))
        {
            writeErr(err);
            return 2;
        }
        if (outDstJsonPath && outDstSize > 0)
            std::snprintf(outDstJsonPath, outDstSize, "%s", dst.c_str());
        if (outErrBuf && errBufSize > 0)
            std::snprintf(outErrBuf, errBufSize, "%s", err.c_str());   // may carry non-fatal warnings
        return 0;
    }

    const char* C_Kit_GetPreviewImageAbsPath(int kitIdx)
    {
        static thread_local std::string sCached;
        sCached.clear();

        LBKit* k = LBKitRegistry::Get().FindKitByIndex(kitIdx);
        if (!k || k->previewImage.empty() || k->sourceFile.empty()) return "";

        namespace fs = std::filesystem;
        fs::path p(k->previewImage);
        if (p.is_absolute())
        {
            sCached = p.string();
        }
        else
        {
            fs::path src(k->sourceFile);
            sCached = (src.parent_path() / p).string();
        }
        return sCached.c_str();
    }

    // ---- The function-pointer table itself ----
    LevelBuilderCoreAPI sTable = {
        /*apiVersion*/             LEVEL_BUILDER_CORE_API_VERSION,

        &C_RegisterTool, &C_UnregisterTool, &C_FindTool, &C_SetActiveTool, &C_GetActiveToolName,
        &C_RegisterSnap, &C_UnregisterSnap, &C_FindSnap, &C_SetActiveSnap, &C_GetActiveSnapName,
        &C_RegisterBrush,&C_UnregisterBrush,&C_FindBrush,&C_SetActiveBrush,&C_GetActiveBrushName,
        &C_RegisterPalette, &C_UnregisterPalette, &C_FindPalette,
        &C_SetActivePalette, &C_GetActivePaletteName, &C_GetActivePalette,

        &C_CreatePalette, &C_Palette_Clear, &C_Palette_AddItem,
        &C_Palette_GetItemCount, &C_Palette_GetItem,
        &C_Palette_SetActiveIndex, &C_Palette_GetActiveIndex,

        &C_Place,

        &C_SetPreviewAsset, &C_SetPreviewTransform, &C_SetPreviewState,
        &C_HidePreview, &C_GetPreviewState, &C_GetPreviewTransform,
        &C_GetPreviewAssetName,

        &C_OpenLevelBuilderWindow, &C_CloseLevelBuilderWindow, &C_IsLevelBuilderWindowOpen,

        &C_RegisterExtensionTab, &C_UnregisterExtensionTab,

        &C_LogDebug, &C_LogWarning, &C_LogError,
        &C_GetEngineAPI,

        // v2 additions
        &C_Viewport_GetHoverHit,
        &C_RegisterToolViewportInput,
        &C_UnregisterToolViewportInput,

        // v3 additions — kit registry
        &C_Kit_GetCount,
        &C_Kit_GetNameAt,
        &C_Kit_FindIndex,
        &C_Kit_GetInfo,

        &C_Kit_GetPieceInfo,
        &C_Kit_FindPieceByAsset,

        &C_Kit_GetSocketInfo,
        &C_Kit_GetSocketCompatibleType,

        &C_Kit_GetActiveName,
        &C_Kit_SetActiveByName,
        &C_Kit_GetActiveIndex,

        &C_Kit_SetPieceSize,

        &C_Kit_Save,
        &C_Kit_Reload,
        &C_Kit_GetLastReloadKitCount,
        &C_Kit_GetLastReloadErrorCount,
        &C_Kit_GetLastReloadError,

        &C_GetProjectRoot,
        &C_GetProjectKitsFolder,
        &C_SetProjectRootOverride,
        &C_ClearProjectRootCache,

        &C_Kit_ComposeSocketWorld,
        &C_Kit_SocketsAreCompatible,
        &C_Kit_EulerDegToQuat,

        // v4 additions — brush spawn-fn hooks
        &C_RegisterSpawnFn,
        &C_UnregisterSpawnFn,
        &C_GetSpawnFnForActiveTool,

        // v5 additions — socket mutation
        &C_Kit_AddSocket,
        &C_Kit_RemoveSocket,
        &C_Kit_SetSocketName,
        &C_Kit_SetSocketType,
        &C_Kit_SetSocketPosition,
        &C_Kit_SetSocketRotation,
        &C_Kit_SetSocketCompatibleTypes,

        // v6 additions — kit sharing metadata (S1)
        &C_Kit_GetMetaInfo,
        &C_Kit_GetTagCount,
        &C_Kit_GetTagAt,
        &C_Kit_ClearTags,
        &C_Kit_AddTag,
        &C_Kit_GetDependencyInfo,
        &C_Kit_SetMetaString,
        &C_Kit_GetPreviewImageAbsPath,

        // v6 additions — kit sharing I/O (S2)
        &C_Kit_ExportToKitFile,
        &C_Kit_PreviewKitFile,
        &C_Kit_ImportKitFile,

        // v7 additions — Paint brush drag input + placed-piece enumeration
        &C_Viewport_IsLmbDown,
        &C_Viewport_IsRmbDown,
        &C_RegisterEnumerateFn,
        &C_UnregisterEnumerateFn,
        &C_GetEnumerateFnForActiveTool,

        // v8 history block removed at v9 — see header note. Slots are
        // gone (not nulled), so plugins compiled against v8 won't
        // resolve the missing entries; that's by design — v9 isn't
        // backward-compatible with v8.
    };
}

extern "C" LEVELBUILDER_CORE_API LevelBuilderCoreAPI* LevelBuilderCore_GetAPI()
{
    return &sTable;
}
