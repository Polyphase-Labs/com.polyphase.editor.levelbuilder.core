#include "LevelBuilderEditorUI.h"

#if EDITOR

#include "LevelBuilderRegistry.h"
#include "LevelBuilderInterfaces.h"

#include "Plugins/EditorUIHooks.h"
#include "Plugins/PolyphaseEngineAPI.h"

#include "imgui.h"

#include <cstring>

static const char* kWindowId   = "level_builder_window";
static const char* kWindowName = "Level Builder";

namespace
{
    char sSearchBuf[256]    = {0};
    char sNewPaletteBuf[64] = "MyPalette";

    // Switch the active tool to `toolName` if it differs from what's
    // active now. Used by the brush picker's auto-switch — picking a
    // sibling-specific brush like "Modular Single" pulls its owner tool
    // along so the user doesn't have to set two dropdowns by hand.
    void MaybeSwitchActiveTool(LevelBuilderRegistry& reg, const char* toolName)
    {
        if (!toolName || !*toolName) return;
        if (reg.GetActiveToolName() == toolName) return;
        reg.SetActiveTool(toolName);
    }

    void DrawBrushTab()
    {
        LevelBuilderRegistry& reg = LevelBuilderRegistry::Get();

        // ---- Active Tool (was the Mode tab — folded in v0.5) ----
        ImGui::TextUnformatted("Active Tool");
        ImGui::Separator();
        {
            const std::string& activeTool = reg.GetActiveToolName();
            const char* toolPreview = activeTool.empty() ? "<none>" : activeTool.c_str();
            ImGui::SetNextItemWidth(220);
            if (ImGui::BeginCombo("##tool_combo", toolPreview))
            {
                if (ImGui::Selectable("<none>", activeTool.empty()))
                    reg.SetActiveTool("");
                for (int i = 0; i < reg.GetToolCount(); ++i)
                {
                    const char* n = reg.GetToolNameAt(i);
                    bool sel = (activeTool == n);
                    if (ImGui::Selectable(n, sel))
                        reg.SetActiveTool(n);
                }
                ImGui::EndCombo();
            }
            if (activeTool.empty())
                ImGui::TextDisabled("Install a sibling addon (modular, grid, ...) and pick a tool, or pick a brush below.");
        }

        // ---- Active Brush ----
        ImGui::Spacing();
        ImGui::TextUnformatted("Active Brush");
        ImGui::Separator();

        const std::string& active = reg.GetActiveBrushName();
        const char* preview = active.empty() ? "<none>" : active.c_str();
        ImGui::SetNextItemWidth(220);
        if (ImGui::BeginCombo("##brush_combo", preview))
        {
            if (ImGui::Selectable("<none>", active.empty()))
                reg.SetActiveBrush("");
            for (int i = 0; i < reg.GetBrushCount(); ++i)
            {
                const char* n = reg.GetBrushNameAt(i);
                if (ImGui::Selectable(n, active == n))
                {
                    reg.SetActiveBrush(n);
                    // Auto-switch the active tool when the picked brush
                    // names its owner tool (see LevelBuilderBrush::GetOwnerTool).
                    if (LevelBuilderBrush* picked = reg.FindBrush(n))
                        MaybeSwitchActiveTool(reg, picked->GetOwnerTool());
                }
            }
            ImGui::EndCombo();
        }

        // Indicator: which tool's spawn semantics will the active brush
        // actually use? For owner-tool brushes this matches the dropdown
        // above. For tool-agnostic brushes (Line / Box / ...) it shows
        // "spawning via: <whatever tool the user picked>" so it's obvious
        // that flipping the tool dropdown changes the snap behavior.
        if (LevelBuilderBrush* activeBrush = reg.FindBrush(active.c_str()))
        {
            const char* owner = activeBrush->GetOwnerTool();
            if (!owner || !*owner)
            {
                const std::string& tool = reg.GetActiveToolName();
                ImGui::TextDisabled("Spawning via: %s",
                                    tool.empty() ? "<no tool — pick one above>" : tool.c_str());
            }
        }

        // Active TOOL's panel — sibling-specific UI that stays visible
        // regardless of which brush is picked (piece picker, snap settings,
        // rotation controls, etc. for modular; cell-size etc. for grid).
        // Tool-agnostic brushes (Line, Box, ...) operate against whichever
        // tool is active, so the user needs the tool's piece picker to
        // persist while they switch between brushes.
        if (LevelBuilderTool* activeToolObj = reg.FindTool(reg.GetActiveToolName().c_str()))
        {
            ImGui::Spacing();
            activeToolObj->DrawSettingsUI();
        }

        // Per-brush settings panel — brushes opt in by overriding
        // LevelBuilderBrush::DrawSettingsUI. Sibling-specific brushes
        // ("Modular Single", "Grid Single") leave this as a no-op since
        // their UI is already drawn via the tool above. Tool-agnostic
        // brushes (Line stride etc.) add their own knobs here.
        if (LevelBuilderBrush* activeBrush = reg.FindBrush(active.c_str()))
        {
            ImGui::Spacing();
            activeBrush->DrawSettingsUI();
        }

        ImGui::Spacing();
        ImGui::TextUnformatted("Snap Provider");
        ImGui::Separator();

        const std::string& snap = reg.GetActiveSnapProviderName();
        const char* snapPreview = snap.empty() ? "<none>" : snap.c_str();
        ImGui::SetNextItemWidth(220);
        if (ImGui::BeginCombo("##snap_combo", snapPreview))
        {
            if (ImGui::Selectable("<none>", snap.empty()))
                reg.SetActiveSnapProvider("");
            for (int i = 0; i < reg.GetSnapProviderCount(); ++i)
            {
                const char* n = reg.GetSnapProviderNameAt(i);
                if (ImGui::Selectable(n, snap == n))
                    reg.SetActiveSnapProvider(n);
            }
            ImGui::EndCombo();
        }
    }

    void DrawPaletteTab()
    {
        LevelBuilderRegistry& reg = LevelBuilderRegistry::Get();

        ImGui::TextUnformatted("Active Palette");
        ImGui::Separator();

        const std::string& active = reg.GetActivePaletteName();
        const char* preview = active.empty() ? "<none>" : active.c_str();
        if (ImGui::BeginCombo("##palette_combo", preview))
        {
            if (ImGui::Selectable("<none>", active.empty()))
                reg.SetActivePalette("");
            for (int i = 0; i < reg.GetPaletteCount(); ++i)
            {
                const char* n = reg.GetPaletteNameAt(i);
                if (ImGui::Selectable(n, active == n))
                    reg.SetActivePalette(n);
            }
            ImGui::EndCombo();
        }

        ImGui::SameLine();
        ImGui::SetNextItemWidth(120);
        ImGui::InputText("##new_palette_name", sNewPaletteBuf, sizeof(sNewPaletteBuf));
        ImGui::SameLine();
        if (ImGui::Button("New"))
            reg.CreatePalette(sNewPaletteBuf);

        ImGui::Spacing();

        LevelBuilderPalette* pal = reg.GetActivePaletteObject();
        if (!pal)
        {
            ImGui::TextDisabled("Select or create a palette to see its items.");
            return;
        }

        // Search + category filter row.
        ImGui::SetNextItemWidth(180);
        if (ImGui::InputTextWithHint("##search", "Search...", sSearchBuf, sizeof(sSearchBuf)))
            pal->SetSearchFilter(sSearchBuf);

        ImGui::SameLine();
        ImGui::SetNextItemWidth(140);
        const std::string& catFilter = pal->GetCategoryFilter();
        const char* catPreview = catFilter.empty() ? "<all>" : catFilter.c_str();
        if (ImGui::BeginCombo("##cat_combo", catPreview))
        {
            if (ImGui::Selectable("<all>", catFilter.empty()))
                pal->SetCategoryFilter("");
            for (int i = 0; i < pal->GetCategoryCount(); ++i)
            {
                const char* c = pal->GetCategory(i);
                if (ImGui::Selectable(c, catFilter == c))
                    pal->SetCategoryFilter(c);
            }
            ImGui::EndCombo();
        }

        ImGui::Separator();
        ImGui::BeginChild("palette_items", ImVec2(0, 0), true);
        const int n = pal->GetItemCount();
        for (int i = 0; i < n; ++i)
        {
            if (!pal->ItemMatchesFilter(i)) continue;
            LevelBuilderPaletteItem item;
            if (!pal->GetItem(i, &item)) continue;
            bool selected = (pal->GetActiveIndex() == i);
            char label[256];
            std::snprintf(label, sizeof(label), "%s##pal_%d",
                          (item.displayName && *item.displayName) ? item.displayName : item.assetName, i);
            if (ImGui::Selectable(label, selected))
            {
                pal->SetActiveIndex(i);
                reg.SetPreviewAsset(item.assetName);
                reg.SetPreviewState(LBPreview_Valid);
            }
            if (item.category && *item.category)
            {
                ImGui::SameLine();
                ImGui::TextDisabled("  [%s]", item.category);
            }
        }
        ImGui::EndChild();
    }

    void DrawPlacementTab()
    {
        LevelBuilderRegistry& reg = LevelBuilderRegistry::Get();
        LevelBuilderContext* ctx = reg.GetContext();

        ImGui::TextUnformatted("Placement Settings");
        ImGui::Separator();

        ImGui::SliderFloat("Rotation Step (deg)", &ctx->mRotationStepDeg, 1.0f, 180.0f, "%.1f");
        ImGui::SliderFloat("Manual Yaw (deg)",    &ctx->mManualYawDeg,  -180.0f, 180.0f, "%.1f");

        if (ImGui::Button("Rotate -Step")) ctx->mManualYawDeg -= ctx->mRotationStepDeg;
        ImGui::SameLine();
        if (ImGui::Button("Rotate +Step")) ctx->mManualYawDeg += ctx->mRotationStepDeg;
        ImGui::SameLine();
        if (ImGui::Button("Reset"))        ctx->mManualYawDeg = 0.0f;

        ImGui::Spacing();
        ImGui::TextUnformatted("Preview");
        ImGui::Separator();

        const char* stateLabels[] = {
            "Hidden", "Valid", "Invalid", "Overlapping", "MissingAsset"
        };
        int s = (int)reg.GetPreviewState();
        if (s < 0 || s >= (int)(sizeof(stateLabels)/sizeof(stateLabels[0]))) s = 0;
        ImGui::Text("State: %s", stateLabels[s]);
        ImGui::Text("Asset: %s", reg.GetPreviewAsset().empty() ? "<none>" : reg.GetPreviewAsset().c_str());
        if (ImGui::Button("Hide Preview"))
            reg.HidePreview();
    }

    void DrawDebugTab()
    {
        LevelBuilderRegistry& reg = LevelBuilderRegistry::Get();
        LevelBuilderContext* ctx = reg.GetContext();

        ImGui::Text("Tools registered:    %d", reg.GetToolCount());
        ImGui::Text("Brushes registered:  %d", reg.GetBrushCount());
        ImGui::Text("Snap providers:      %d", reg.GetSnapProviderCount());
        ImGui::Text("Palettes:            %d", reg.GetPaletteCount());
        ImGui::Text("Extension tabs:      %d", reg.GetExtensionTabCount());

        ImGui::Separator();
        ImGui::TextUnformatted("Last Placement Error");
        const char* err = ctx->mLastError;
        if (err && *err)
            ImGui::TextWrapped("%s", err);
        else
            ImGui::TextDisabled("<none>");

        ImGui::Separator();
        ImGui::TextUnformatted("Last Raycast");
        ImGui::Text("Valid: %s", ctx->mLastRaycastValid ? "yes" : "no");
        ImGui::Text("Hit: (%.3f, %.3f, %.3f)",
                    ctx->mLastRaycastHit.x, ctx->mLastRaycastHit.y, ctx->mLastRaycastHit.z);
    }

    void DrawLevelBuilderWindow(void* /*userData*/)
    {
        LevelBuilderRegistry& reg = LevelBuilderRegistry::Get();

        if (ImGui::BeginTabBar("level_builder_tabs"))
        {
            // Mode tab retired in v0.5 — the brush dropdown auto-switches
            // the active tool via LevelBuilderBrush::GetOwnerTool(), and
            // the Brush tab's header carries the explicit tool dropdown
            // for tool-agnostic brushes (Line / Box / ...).
            if (ImGui::BeginTabItem("Brush")) { DrawBrushTab(); ImGui::EndTabItem(); }
            if (ImGui::BeginTabItem("Palette")) { DrawPaletteTab(); ImGui::EndTabItem(); }
            if (ImGui::BeginTabItem("Placement")) { DrawPlacementTab(); ImGui::EndTabItem(); }
            if (ImGui::BeginTabItem("Debug")) { DrawDebugTab(); ImGui::EndTabItem(); }

            // Sibling-addon-provided tabs (Modular, Grid, ...).
            for (int i = 0; i < reg.GetExtensionTabCount(); ++i)
            {
                const auto& tab = reg.GetExtensionTab(i);
                if (!tab.drawFn) continue;
                if (ImGui::BeginTabItem(tab.name.c_str()))
                {
                    tab.drawFn(tab.userData);
                    ImGui::EndTabItem();
                }
            }
            ImGui::EndTabBar();
        }
    }

    // Menu callback — toggles the window. Bound to "Addons > Level Builder >
    // Open Level Builder" so it's discoverable in the Addons menu without
    // needing a custom top-level menu.
    EditorUIHooks* sHooksForMenu = nullptr;

    void OnOpenLevelBuilderMenu(void* /*ud*/)
    {
        if (!sHooksForMenu) return;
        if (sHooksForMenu->IsWindowOpen(kWindowId))
            sHooksForMenu->CloseWindow(kWindowId);
        else
            sHooksForMenu->OpenWindow(kWindowId);
    }

    // ---- Viewport-mode tick ----
    //
    // The mouse polling + raycast + preview-ghost driver + active-brush
    // TickEditor were previously crammed into DrawViewportPreview. With
    // AddViewportMode (engine v7+ hook) the editor only fires Tick while
    // the Level Builder mode is active, so dragging another viewport
    // tool no longer pays for hover-state we don't use. Tick runs BEFORE
    // the viewport overlays each frame, so the gizmo's wire-sphere
    // already has fresh state when DrawViewportPreview draws it.
    static void DoViewportTick(PolyphaseEngineAPI* api,
                               EditorUIHooks* hooks,
                               LevelBuilderRegistry& reg,
                               LevelBuilderContext& ctx);

    static void OnLevelBuilderActivate(void* /*ud*/)
    {
        // Open the dockable Level Builder window so artists see the panel
        // immediately on mode entry. The window keeps any user-set dock
        // position across activate/deactivate cycles (engine remembers).
        if (sHooksForMenu) sHooksForMenu->OpenWindow(kWindowId);
    }

    static void OnLevelBuilderDeactivate(void* /*ud*/)
    {
        if (sHooksForMenu) sHooksForMenu->CloseWindow(kWindowId);
    }

    static void OnLevelBuilderTick(float /*dt*/, void* /*ud*/)
    {
        LevelBuilderRegistry& reg = LevelBuilderRegistry::Get();
        PolyphaseEngineAPI* api = reg.GetEngineAPI();
        if (!api) return;
        EditorUIHooks* hooks = api->editorUI;
        LevelBuilderContext& ctx = reg.GetContextRef();
        DoViewportTick(api, hooks, reg, ctx);
    }

    static void OnLevelBuilderDrawPanel(void* /*ud*/)
    {
        // Panel content lives in the dockable window (registered via
        // RegisterWindow → DrawLevelBuilderWindow). Leaving this empty
        // avoids the engine's note about double-drawing. If a future
        // engine version makes DrawPanel the canonical surface we'd
        // move the tab-bar render here and drop RegisterWindow.
    }

    // Viewport overlay — placement preview gizmo. Tick happens elsewhere
    // (OnLevelBuilderTick when the mode is active); this callback only
    // draws what tick already computed. Runs every frame regardless of
    // mode, but no-ops when there's no armed preview asset.
    void DrawViewportPreview(float /*vx*/, float /*vy*/, float /*vw*/, float /*vh*/, void* /*ud*/)
    {
        LevelBuilderRegistry& reg = LevelBuilderRegistry::Get();
        PolyphaseEngineAPI* api = reg.GetEngineAPI();
        if (!api) return;

        // ---- Gizmo render only — tick already ran via OnLevelBuilderTick ----
        if (reg.GetPreviewState() == LBPreview_Hidden) return;
        if (!api->Gizmos_DrawWireSphere) return;

        LBVec3 pos, scl;
        LBQuat rot;
        reg.GetPreviewTransform(pos, rot, scl);

        switch (reg.GetPreviewState())
        {
        case LBPreview_Valid:        api->Gizmos_SetColor(0.20f, 1.0f, 0.30f, 0.6f); break;
        case LBPreview_Invalid:      api->Gizmos_SetColor(1.0f, 0.25f, 0.25f, 0.6f); break;
        case LBPreview_Overlapping:  api->Gizmos_SetColor(1.0f, 0.55f, 0.10f, 0.6f); break;
        case LBPreview_MissingAsset: api->Gizmos_SetColor(0.5f, 0.5f,  0.5f,  0.6f); break;
        default: api->Gizmos_SetColor(1.0f, 1.0f, 1.0f, 0.4f); break;
        }
        float radius = 0.5f * (scl.x + scl.y + scl.z) * 0.333f;
        if (radius < 0.05f) radius = 0.5f;
        api->Gizmos_DrawWireSphere(pos.x, pos.y, pos.z, radius);
        api->Gizmos_ResetState();
    }

    // ---- Implementation of the viewport-mode tick (declared above) ----
    // This is everything DrawViewportPreview used to do BEFORE the gizmo
    // render: mouse poll, raycast, preview-ghost drive, click dispatch,
    // and the active-brush TickEditor.
    static void DoViewportTick(PolyphaseEngineAPI* api,
                               EditorUIHooks* hooks,
                               LevelBuilderRegistry& reg,
                               LevelBuilderContext& ctx)
    {
        // ---- 1. Poll viewport mouse state (v5 hooks) ----
        bool hovered = false;
        bool leftClicked = false;
        float mx = 0.0f, my = 0.0f;
        if (hooks && hooks->Viewport_GetMouseState)
        {
            int iHov = 0, iClk = 0, iDown = 0, iRClk = 0;
            hooks->Viewport_GetMouseState(nullptr, nullptr, nullptr, nullptr,
                                          &mx, &my,
                                          &iHov, &iClk, &iDown, &iRClk);
            hovered = (iHov != 0);
            leftClicked = (iClk != 0);
            // v7: stash drag state for the Paint brush to poll via
            // Viewport_IsLmbDown / Viewport_IsRmbDown.
            ctx.mLmbDown = (iDown != 0) && hovered;
            ctx.mRmbDown = (iRClk != 0) && hovered;
        }
        else
        {
            ctx.mLmbDown = false;
            ctx.mRmbDown = false;
        }

        // ---- 2. Raycast under mouse when hovered ----
        if (hovered && hooks && hooks->Viewport_RaycastUnderMouse)
        {
            float hx=0, hy=0, hz=0, nrmX=0, nrmY=1, nrmZ=0;
            void* hitNode = nullptr;
            bool ok = hooks->Viewport_RaycastUnderMouse(mx, my, /*fallbackPlaneY=*/0.0f,
                                                        &hx, &hy, &hz,
                                                        &nrmX, &nrmY, &nrmZ,
                                                        &hitNode);
            ctx.mLastRaycastValid  = ok;
            ctx.mLastRaycastHit    = LBVec3{hx, hy, hz};
            ctx.mLastRaycastNormal = LBVec3{nrmX, nrmY, nrmZ};
            ctx.mLastRaycastNode   = hitNode;
        }
        else
        {
            ctx.mLastRaycastValid = false;
        }

        // ---- 3. Drive preview ghost from hover (only when armed) ----
        const bool previewArmed = !reg.GetPreviewAsset().empty();

        // Suppress the editor's click-selects-the-hit-node behavior on
        // the NEXT click so our placement click doesn't double-act.
        if (hooks && hooks->Viewport_SuppressNextSelectionClick
            && hovered && previewArmed)
        {
            hooks->Viewport_SuppressNextSelectionClick();
        }

        if (hovered && ctx.mLastRaycastValid && previewArmed)
        {
            LBVec3 pos, scl; LBQuat rot;
            reg.GetPreviewTransform(pos, rot, scl);
            pos = ctx.mLastRaycastHit;

            if (LevelBuilderSnapProvider* sp = reg.FindSnapProvider(reg.GetActiveSnapProviderName().c_str()))
            {
                LBVec3 snappedPos; LBQuat snappedRot;
                if (sp->GetSnapTransform(pos, snappedPos, snappedRot))
                {
                    pos = snappedPos;
                    rot = snappedRot;
                }
            }
            reg.SetPreviewTransform(pos, rot, scl);
            reg.SetPreviewState(LBPreview_Valid);
        }
        // (Manual-positioned preview left intact when cursor wanders off.)

        // ---- 4. Dispatch click to active tool's subscriber ----
        if (leftClicked && previewArmed && ctx.mLastRaycastValid)
        {
            const std::string& active = reg.GetActiveToolName();
            if (!active.empty())
            {
                if (const LevelBuilderRegistry::ToolViewportSub* sub =
                        reg.FindToolViewportSub(active.c_str()))
                {
                    if (sub->cb)
                    {
                        sub->cb(&ctx.mLastRaycastHit, &ctx.mLastRaycastNormal,
                                ctx.mLastRaycastNode, /*button=*/0, sub->userData);
                    }
                }
            }
        }

        // ---- 5. Per-frame TickEditor on the active brush (v7) ----
        // Drag-input brushes (Paint) override this to stamp/erase per
        // frame; the default impl is a no-op so other brushes pay nothing.
        if (LevelBuilderBrush* brush = reg.FindBrush(reg.GetActiveBrushName().c_str()))
        {
            float dt = ImGui::GetIO().DeltaTime;
            if (dt <= 0.0f) dt = 1.0f / 60.0f;
            brush->TickEditor(dt);
        }
        (void)api;   // reserved for future per-frame API calls
    }
}

namespace LevelBuilderEditorUI
{
    void Register(EditorUIHooks* hooks, uint64_t hookId)
    {
        if (!hooks) return;
        sHooksForMenu = hooks;

        // Dockable window — still registered so the panel content has a
        // host to render into. The window opens via OnLevelBuilderActivate
        // (mode dropdown → "Level Builder") and closes via Deactivate.
        hooks->RegisterWindow(hookId, kWindowName, kWindowId, &DrawLevelBuilderWindow, nullptr);

        // Viewport overlay for the placement-preview gizmo. Tick happens
        // elsewhere (OnLevelBuilderTick); this overlay only draws the
        // wire-sphere ghost so it can stay always-on (no-ops when there's
        // no armed preview asset).
        if (hooks->RegisterViewportOverlay)
            hooks->RegisterViewportOverlay(hookId, "level_builder_preview",
                                           &DrawViewportPreview, nullptr);

        // Viewport-mode dropdown entry (engine v7+ hook). Replaces the
        // older "Addons > Level Builder > Open Level Builder" menu item —
        // the user enters Level Builder mode from the viewport's mode
        // dropdown; OnActivate opens the panel + arms the tick path.
        if (hooks->AddViewportMode)
        {
            hooks->AddViewportMode(
                hookId,
                "com.polyphase.editor.levelbuilder",   // stable mode id
                "Level Builder",                        // dropdown label
                100,                                    // sortOrder
                nullptr,                                // CanActivate — always available
                &OnLevelBuilderActivate,
                &OnLevelBuilderDeactivate,
                &OnLevelBuilderTick,
                &OnLevelBuilderDrawPanel,
                nullptr);
        }
        else if (hooks->AddAddonsMenuItem)
        {
            // Pre-AddViewportMode engine: fall back to the Addons menu
            // entry so the user can still open the panel manually.
            hooks->AddAddonsMenuItem(hookId, "Level Builder/Open Level Builder",
                                     &OnOpenLevelBuilderMenu, nullptr);
        }
    }

    void Unregister(EditorUIHooks* hooks, uint64_t hookId)
    {
        // The engine calls RemoveAllHooks(hookId) for us before the DLL
        // unloads, so we don't need to unregister individually. Just drop
        // cached pointers so we don't dereference them post-unload.
        (void)hooks;
        (void)hookId;
        sHooksForMenu = nullptr;
    }

    void OpenWindow()
    {
        if (sHooksForMenu) sHooksForMenu->OpenWindow(kWindowId);
    }
    void CloseWindow()
    {
        if (sHooksForMenu) sHooksForMenu->CloseWindow(kWindowId);
    }
    bool IsWindowOpen()
    {
        return sHooksForMenu ? sHooksForMenu->IsWindowOpen(kWindowId) : false;
    }
}

#endif // EDITOR
