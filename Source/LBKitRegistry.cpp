#include "LBKitRegistry.h"

#include "LBKitJson.h"
#include "LBKitShare.h"
#include "LevelBuilderCoreAPI.h"

#include <algorithm>
#include <cstdlib>
#include <filesystem>

#ifdef _WIN32
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #include <windows.h>
#else
    #include <dlfcn.h>
    #include <limits.h>
    #include <unistd.h>
#endif

// -----------------------------------------------------------------------------
// Project root resolver — moved from modular's ModularKit.cpp anonymous
// namespace. The file-scope statics for the cache are intentional: this
// is the canonical project root for the whole editor process.
// -----------------------------------------------------------------------------

namespace
{
    namespace fs = std::filesystem;

    bool LooksLikeProjectRoot(const fs::path& p)
    {
        std::error_code ec;
        if (fs::is_regular_file(p / "PolyphaseConfig.cmake", ec)) return true;
        if (fs::is_regular_file(p / "Config.ini", ec) &&
            fs::is_directory(p / "Assets", ec))                   return true;
        if (fs::is_directory(p / "Packages", ec))                 return true;
        for (auto it = fs::directory_iterator(p, ec);
             !ec && it != fs::directory_iterator(); ++it)
        {
            if (it->is_regular_file(ec) && it->path().extension() == ".octp")
                return true;
        }
        return false;
    }

    fs::path WalkUpToProjectRoot(fs::path start)
    {
        for (int i = 0; i < 16; ++i)
        {
            if (start.empty()) break;
            if (LooksLikeProjectRoot(start)) return start;
            if (!start.has_parent_path()) break;
            fs::path next = start.parent_path();
            if (next == start) break;
            start = next;
        }
        return {};
    }

    fs::path GetSelfDllPath()
    {
#ifdef _WIN32
        HMODULE hSelf = nullptr;
        GetModuleHandleExA(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            (LPCSTR)&GetSelfDllPath,
            &hSelf);
        if (!hSelf) return {};
        char buf[MAX_PATH] = {0};
        DWORD n = GetModuleFileNameA(hSelf, buf, MAX_PATH);
        if (n == 0 || n >= MAX_PATH) return {};
        return fs::path(buf);
#else
        Dl_info info{};
        if (!dladdr((void*)&GetSelfDllPath, &info) || !info.dli_fname) return {};
        return fs::path(info.dli_fname);
#endif
    }

    std::string sProjectRootCached;
    std::string sKitsFolderCached;   // <root>/Kits, cached to avoid path concat each call
    bool        sProjectRootResolved = false;
    std::string sProjectRootOverride;

    void ResolveProjectRoot()
    {
        if (sProjectRootResolved) return;
        sProjectRootResolved = true;
        sProjectRootCached.clear();

        std::error_code ec;

        // Strategy 0 — explicit in-process override.
        if (!sProjectRootOverride.empty() && fs::is_directory(sProjectRootOverride, ec))
        {
            sProjectRootCached = sProjectRootOverride;
        }
        // Strategy 1 — env var.
        else if (const char* env = std::getenv("POLYPHASE_PROJECT"))
        {
            if (env[0] && fs::is_directory(env, ec))
                sProjectRootCached = fs::path(env).string();
        }

        // Strategy 2 — walk up from the running core DLL.
        if (sProjectRootCached.empty())
        {
            fs::path dll = GetSelfDllPath();
            if (!dll.empty())
            {
                fs::path root = WalkUpToProjectRoot(dll.parent_path());
                if (!root.empty()) sProjectRootCached = root.string();
            }
        }

        // Strategy 3 — walk up from CWD.
        if (sProjectRootCached.empty())
        {
            fs::path cwd = fs::current_path(ec);
            if (!ec && !cwd.empty())
            {
                fs::path root = WalkUpToProjectRoot(cwd);
                if (!root.empty()) sProjectRootCached = root.string();
            }
        }

        sKitsFolderCached.clear();
        if (!sProjectRootCached.empty())
            sKitsFolderCached = (fs::path(sProjectRootCached) / "Kits").string();
    }
}

// -----------------------------------------------------------------------------
// LBKitRegistry — instance + lifecycle
// -----------------------------------------------------------------------------

LBKitRegistry& LBKitRegistry::Get()
{
    static LBKitRegistry sInstance;
    return sInstance;
}

void LBKitRegistry::Clear()
{
    mKits.clear();
    mOrder.clear();
    mActive.clear();
}

LBKit* LBKitRegistry::CreateKit(const std::string& name)
{
    if (name.empty()) return nullptr;
    auto it = mKits.find(name);
    if (it != mKits.end()) return it->second.get();
    auto k = std::make_unique<LBKit>();
    k->name = name;
    LBKit* raw = k.get();
    mKits[name] = std::move(k);
    if (std::find(mOrder.begin(), mOrder.end(), name) == mOrder.end())
        mOrder.push_back(name);
    return raw;
}

LBKit* LBKitRegistry::FindKit(const std::string& name)
{
    auto it = mKits.find(name);
    return it == mKits.end() ? nullptr : it->second.get();
}

LBKit* LBKitRegistry::FindKitByIndex(int idx)
{
    if (idx < 0 || idx >= (int)mOrder.size()) return nullptr;
    return FindKit(mOrder[idx]);
}

void LBKitRegistry::RemoveKit(const std::string& name)
{
    mKits.erase(name);
    mOrder.erase(std::remove(mOrder.begin(), mOrder.end(), name), mOrder.end());
    if (mActive == name) mActive.clear();
}

bool LBKitRegistry::RenameKit(const std::string& oldName, const std::string& newName)
{
    if (oldName.empty() || newName.empty() || oldName == newName) return false;
    auto it = mKits.find(oldName);
    if (it == mKits.end()) return false;
    if (mKits.find(newName) != mKits.end()) return false;

    std::unique_ptr<LBKit> moved = std::move(it->second);
    moved->name = newName;
    mKits.erase(it);
    mKits[newName] = std::move(moved);

    for (auto& n : mOrder)
        if (n == oldName) { n = newName; break; }

    if (mActive == oldName) mActive = newName;
    return true;
}

int LBKitRegistry::FindKitIndex(const std::string& name) const
{
    for (int i = 0; i < (int)mOrder.size(); ++i)
        if (mOrder[i] == name) return i;
    return -1;
}

const std::string& LBKitRegistry::GetKitNameAt(int idx) const
{
    static const std::string sEmpty;
    if (idx < 0 || idx >= (int)mOrder.size()) return sEmpty;
    return mOrder[idx];
}

void LBKitRegistry::SetActiveKit(const std::string& name)
{
    if (FindKit(name)) mActive = name;
    else               mActive.clear();
}

LBKit* LBKitRegistry::GetActiveKit()
{
    return FindKit(mActive);
}

// -----------------------------------------------------------------------------
// Built-in starter kit (moved from modular's EnsureBuiltinKit)
// -----------------------------------------------------------------------------

void LBKitRegistry::EnsureBuiltinKit()
{
    if (FindKit("Default Kit")) return;

    LBKit* kit = CreateKit("Default Kit");
    if (!kit) return;

    auto makeSocket = [](const char* name, const char* type,
                         float x, float y, float z, float yawDeg)
    {
        LBSocket s;
        s.name = name;
        s.type = type;
        s.localPos[0] = x; s.localPos[1] = y; s.localPos[2] = z;
        s.localEulerDeg[0] = 0; s.localEulerDeg[1] = yawDeg; s.localEulerDeg[2] = 0;
        return s;
    };

    {
        LBPiece p;
        p.name = "Wall_4m";
        p.assetName = "SM_Wall_4m";
        p.category  = "Walls";
        p.size[0] = 4; p.size[1] = 3; p.size[2] = 0.5f;
        p.sockets.push_back(makeSocket("Left",  "Wall", -2.0f, 0.0f, 0.0f,   0.0f));
        p.sockets.push_back(makeSocket("Right", "Wall",  2.0f, 0.0f, 0.0f, 180.0f));
        kit->pieces.push_back(std::move(p));
    }
    {
        LBPiece p;
        p.name = "Floor_4x4";
        p.assetName = "SM_Floor_4x4";
        p.category  = "Floors";
        p.size[0] = 4; p.size[1] = 0.2f; p.size[2] = 4;
        p.sockets.push_back(makeSocket("N",  "Floor",  0.0f, 0.0f, -2.0f,   0.0f));
        p.sockets.push_back(makeSocket("S",  "Floor",  0.0f, 0.0f,  2.0f, 180.0f));
        p.sockets.push_back(makeSocket("E",  "Floor",  2.0f, 0.0f,  0.0f,  90.0f));
        p.sockets.push_back(makeSocket("W",  "Floor", -2.0f, 0.0f,  0.0f, -90.0f));
        kit->pieces.push_back(std::move(p));
    }
    {
        LBPiece p;
        p.name = "DoorFrame";
        p.assetName = "SM_DoorFrame";
        p.category  = "Doors";
        p.size[0] = 1.2f; p.size[1] = 2.4f; p.size[2] = 0.5f;
        LBSocket left  = makeSocket("Left",  "Wall", -0.6f, 0.0f, 0.0f,   0.0f);
        LBSocket right = makeSocket("Right", "Wall",  0.6f, 0.0f, 0.0f, 180.0f);
        LBSocket door  = makeSocket("Door",  "Door",  0.0f, 0.0f, 0.0f,   0.0f);
        door.compatibleTypes = { "Door", "DoorFrame" };
        p.sockets.push_back(std::move(left));
        p.sockets.push_back(std::move(right));
        p.sockets.push_back(std::move(door));
        kit->pieces.push_back(std::move(p));
    }

    SetActiveKit("Default Kit");
}

// -----------------------------------------------------------------------------
// I/O
// -----------------------------------------------------------------------------

LBKit* LBKitRegistry::LoadKitFromFile(const std::string& path, std::string* outError)
{
    LBKit parsed;
    std::string err;

    // Folder-mode dispatch (Phase 3 of KitAuthoringUI): when the kit.json
    // at `path` carries an indexed `pieces` array (entries with "file"
    // and no inline "asset"), load the kit via LBKitJson::LoadFromFolder
    // so each pieces/<name>.json gets read. Single-file kits — including
    // legacy `<project>/Kits/*.json` and the inline-pieces kit.json that
    // Phase 5.5a's Kenney synthesizer still writes — keep going through
    // LoadFromFile.
    namespace fs = std::filesystem;
    fs::path p(path);
    bool isKitJson = (p.filename() == "kit.json");
    bool loaded = false;
    if (isKitJson && LBKitJson::IsFolderModeKitJson(path))
    {
        loaded = LBKitJson::LoadFromFolder(p.parent_path().string(), parsed, err);
    }
    if (!loaded)
    {
        loaded = LBKitJson::LoadFromFile(path, parsed, err);
    }
    if (!loaded)
    {
        if (outError) *outError = err;
        return nullptr;
    }
    if (parsed.name.empty())
    {
        if (outError) *outError = path + ": kit has empty 'kitName'";
        return nullptr;
    }

    LBKit* kit = CreateKit(parsed.name);
    if (!kit)
    {
        if (outError) *outError = path + ": failed to register kit";
        return nullptr;
    }
    kit->pieces        = std::move(parsed.pieces);
    kit->sourceFile    = path;
    kit->kitId                   = std::move(parsed.kitId);
    kit->kitVersion              = std::move(parsed.kitVersion);
    kit->author                  = std::move(parsed.author);
    kit->authorUrl               = std::move(parsed.authorUrl);
    kit->license                 = std::move(parsed.license);
    kit->licenseUrl              = std::move(parsed.licenseUrl);
    kit->description             = std::move(parsed.description);
    kit->previewImage            = std::move(parsed.previewImage);
    kit->homepage                = std::move(parsed.homepage);
    kit->minimumPolyphaseVersion = std::move(parsed.minimumPolyphaseVersion);
    kit->tags                    = std::move(parsed.tags);
    kit->dependencies            = std::move(parsed.dependencies);
    kit->kitIdSynthesized        = parsed.kitIdSynthesized;
    return kit;
}

// -----------------------------------------------------------------------------
// Phase 5.5a — Kenney-style pack auto-recognition (Polyphase format only)
//
// A directory under <project>/Kits/ is treated as a Kenney pack if it has
// a Models/ subdir AND at least one of:
//   - Models/Polyphase/  with *.oct files                (← handled here)
//   - Previews/          with per-piece thumbnails
//   - Preview.png at the kit root
//   - format-flavored Models/<NAME> format/ subdirs       (5.5b — needs Engine Gap 6)
//
// For 5.5a we only synthesize from Models/Polyphase/*.oct because those
// load through Polyphase's existing AssetManager — no asset-import bridge
// needed. The other format subdirs (GLB / FBX / OBJ) come in 5.5b.
//
// Layered customization: if the same directory also has a kit.json, that
// wins (artist's hand-curated metadata takes priority over auto-synth).
// -----------------------------------------------------------------------------

namespace
{
    // Prettify a hyphen-separated kebab-case name into a display label.
    // "button-floor-round-small" → "Button Floor Round Small"
    std::string PrettifyKebab(const std::string& kebab)
    {
        std::string out;
        out.reserve(kebab.size());
        bool capitalizeNext = true;
        for (char c : kebab)
        {
            if (c == '-' || c == '_')
            {
                if (!out.empty() && out.back() != ' ') out.push_back(' ');
                capitalizeNext = true;
                continue;
            }
            if (capitalizeNext)
            {
                out.push_back((char)std::toupper((unsigned char)c));
                capitalizeNext = false;
            }
            else
            {
                out.push_back(c);
            }
        }
        return out;
    }

    // Category = the first hyphen segment of the basename, prettified.
    // "button-floor-round" → "Button"   "wall-corner-low" → "Wall"
    // No hyphen → empty category.
    std::string CategoryFromKebab(const std::string& kebab)
    {
        auto pos = kebab.find('-');
        if (pos == std::string::npos) return {};
        std::string head = kebab.substr(0, pos);
        if (head.empty()) return {};
        head[0] = (char)std::toupper((unsigned char)head[0]);
        return head;
    }

    // Synthesize a kit from a Kenney-style folder. Returns true and fills
    // outKit on success; false if the folder doesn't look like a Kenney
    // pack (no Models/Polyphase/*.oct found). outKit.sourceFile is left
    // empty — there's no kit.json to save back to, so Kit_Save no-ops on
    // these. Future Phase R4 work can add "Export to kit.json" if needed.
    bool TrySynthesizeKenneyKit(const fs::path& dir,
                                const fs::path& projectRoot,
                                LBKit& outKit)
    {
        std::error_code ec;
        fs::path modelsPoly = dir / "Models" / "Polyphase";
        if (!fs::is_directory(modelsPoly, ec)) return false;

        // Gather .oct files. Skip subdirs (Textures/ etc.).
        std::vector<fs::path> octFiles;
        for (auto& e : fs::directory_iterator(modelsPoly, ec))
        {
            if (ec) break;
            if (!e.is_regular_file(ec)) continue;
            std::string ext = e.path().extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(),
                           [](unsigned char c){ return (char)std::tolower(c); });
            if (ext == ".oct") octFiles.push_back(e.path());
        }
        if (octFiles.empty()) return false;

        // Sort so kit ordering is deterministic across machines / runs.
        std::sort(octFiles.begin(), octFiles.end());

        // Kit name = prettified directory name.
        std::string dirName = dir.filename().string();
        outKit.name = PrettifyKebab(dirName);
        if (outKit.name.empty()) outKit.name = dirName;
        outKit.sourceFile.clear();  // Filled in by the caller after kit.json write.
        outKit.pieces.clear();
        outKit.pieces.reserve(octFiles.size());

        // Previews/<basename>.png — Kenney convention. Icon paths are
        // stored relative to the PROJECT ROOT (not to the kit folder)
        // so the existing ThumbnailCache resolution path joins them
        // correctly regardless of how deep the kit folder lives
        // (e.g. <root>/Kits/<pack>/  vs.  <root>/Assets/Kits/<pack>/).
        fs::path previewsDir = dir / "Previews";
        bool hasPreviews = fs::is_directory(previewsDir, ec);

        for (const auto& oct : octFiles)
        {
            std::string basename = oct.stem().string();   // "wall-corner"
            LBPiece p;
            p.name      = PrettifyKebab(basename);
            p.assetName = basename;                       // engine asset name
            p.category  = CategoryFromKebab(basename);
            p.size[0] = p.size[1] = p.size[2] = 1.0f;     // placeholder; Clean Up Bounding Boxes fills in real values

            if (hasPreviews)
            {
                fs::path png = previewsDir / (basename + ".png");
                if (fs::is_regular_file(png, ec))
                {
                    std::error_code rel_ec;
                    fs::path rel;
                    if (!projectRoot.empty())
                        rel = fs::relative(png, projectRoot, rel_ec);
                    if (!rel_ec && !rel.empty())
                        p.iconPath = rel.generic_string();
                    else
                        p.iconPath = png.generic_string();
                }
            }
            outKit.pieces.push_back(std::move(p));
        }
        return true;
    }
}

int LBKitRegistry::ScanKitsFolder(const std::string& folderPath,
                                  std::vector<std::string>* outErrors)
{
    namespace fs = std::filesystem;
    std::error_code ec;
    if (folderPath.empty() || !fs::is_directory(folderPath, ec))
        return 0;

    // Compute the project root once so per-kit icon-path resolution
    // and kit.json materialization don't all have to redo it.
    fs::path projectRoot;
    {
        std::string rootStr = GetProjectRoot();
        if (!rootStr.empty()) projectRoot = fs::path(rootStr);
    }

    int loaded = 0;
    for (const auto& entry : fs::directory_iterator(folderPath, ec))
    {
        if (ec) break;

        // ---- Directory entries (folder kits) ----
        if (entry.is_directory(ec))
        {
            const fs::path& dir = entry.path();

            // Rule #1: Polyphase-native folder — <dir>/kit.json exists.
            // Delegate to the existing single-file loader.
            fs::path kitJson = dir / "kit.json";
            if (fs::is_regular_file(kitJson, ec))
            {
                std::string err;
                if (LoadKitFromFile(kitJson.string(), &err))
                    ++loaded;
                else if (outErrors)
                    outErrors->push_back(err);
                continue;
            }

            // Rule #2 (Phase 5.5a): Kenney-style pack with Polyphase
            // format. Synthesize the kit, then materialize it as a
            // kit.json next to Models/Polyphase/ so:
            //   - Clean Up Bounding Boxes (which calls Kit_Save) works.
            //   - Subsequent reloads hit Rule #1 instead of re-synthesizing.
            //   - The artist can hand-edit metadata / sockets afterwards.
            LBKit synth;
            if (TrySynthesizeKenneyKit(dir, projectRoot, synth) && !synth.name.empty())
            {
                // Write the kit.json now. On failure we still load the
                // in-memory kit so the user can place pieces immediately;
                // we just won't be able to Kit_Save until they retry.
                synth.sourceFile = kitJson.string();
                std::string writeErr;
                if (!LBKitJson::SaveToFile(kitJson.string(), synth, writeErr))
                {
                    synth.sourceFile.clear();   // Kit_Save will no-op until next reload retries.
                    if (outErrors)
                        outErrors->push_back("auto-materialize kit.json: " + writeErr);
                }

                if (mKits.find(synth.name) == mKits.end())
                    mOrder.push_back(synth.name);
                auto unique = std::make_unique<LBKit>(std::move(synth));
                mKits[unique->name] = std::move(unique);
                ++loaded;
            }
            continue;
        }

        // ---- File entries (legacy loose-format kits + Phase S2 .kit) ----
        if (!entry.is_regular_file(ec)) continue;
        auto ext = entry.path().extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(),
                       [](unsigned char c){ return (char)std::tolower(c); });

        // .kit shared archive (Phase S2): inline-import on encounter so
        // dropping a `MyKit.kit` into Kits/ "just works" on next reload.
        // The original .kit file moves to .kit-cache/ so we don't
        // re-import it on every Reload Kits click. Failures keep the
        // .kit in place for the user to investigate.
        if (ext == ".kit")
        {
            std::string err;
            std::string dstJson, importedId;
            if (LBKitShare::ImportKit(entry.path().string(),
                                      LBKitShare::ConflictMode::RenameNew,
                                      dstJson, importedId, err))
            {
                if (LoadKitFromFile(dstJson, &err))
                    ++loaded;
                else if (outErrors)
                    outErrors->push_back(err);

                // Move the source .kit to a sibling .kit-cache/ folder so
                // ScanProjectKits doesn't re-import it next time.
                std::error_code mvEc;
                fs::path cacheDir = fs::path(folderPath) / ".kit-cache";
                fs::create_directories(cacheDir, mvEc);
                fs::rename(entry.path(),
                           cacheDir / entry.path().filename(),
                           mvEc);
            }
            else if (outErrors)
            {
                outErrors->push_back(err);
            }
            continue;
        }

        if (ext != ".json") continue;

        std::string err;
        if (LoadKitFromFile(entry.path().string(), &err))
            ++loaded;
        else if (outErrors)
            outErrors->push_back(err);
    }
    return loaded;
}

int LBKitRegistry::ScanProjectKits()
{
    // Snapshot the previously active kit so a reload doesn't clobber
    // the user's selection if the same name still exists after the scan.
    std::string prevActive = mActive;

    Clear();
    EnsureBuiltinKit();

    mLastReloadErrors.clear();
    mLastReloadKitCount = 0;

    // Primary scan location: <project>/Kits/  — where loose-format
    // *.json kits live, and where authored Polyphase-native folder kits
    // (with kit.json) typically end up.
    mLastReloadKitCount += ScanKitsFolder(GetProjectKitsFolder(), &mLastReloadErrors);

    // Secondary scan: <project>/Assets/Kits/  — required for Kenney-style
    // auto-recognized packs whose Models/Polyphase/*.oct files must live
    // under Assets/ to be picked up by the engine's AssetManager. We
    // scan this AFTER <project>/Kits/ so any kit appearing in both with
    // the same name lets the Assets/Kits/ copy (with real .oct files
    // registered as assets) win over a loose-file mirror.
    std::string root = GetProjectRoot();
    if (!root.empty())
    {
        std::filesystem::path assetsKits = std::filesystem::path(root) / "Assets" / "Kits";
        mLastReloadKitCount += ScanKitsFolder(assetsKits.string(), &mLastReloadErrors);
    }

    if (!prevActive.empty() && FindKit(prevActive))
        SetActiveKit(prevActive);

    return GetKitCount();
}

// -----------------------------------------------------------------------------
// Project root accessors
// -----------------------------------------------------------------------------

const std::string& LBKitRegistry::GetProjectRoot()
{
    ResolveProjectRoot();
    return sProjectRootCached;
}

const std::string& LBKitRegistry::GetProjectKitsFolder()
{
    ResolveProjectRoot();
    return sKitsFolderCached;
}

void LBKitRegistry::SetProjectRootOverride(const char* path)
{
    sProjectRootOverride.assign(path ? path : "");
    ClearProjectRootCache();
}

void LBKitRegistry::ClearProjectRootCache()
{
    sProjectRootCached.clear();
    sKitsFolderCached.clear();
    sProjectRootResolved = false;
}
