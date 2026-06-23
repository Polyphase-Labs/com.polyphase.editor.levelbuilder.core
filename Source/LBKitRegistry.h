/**
 * @file LBKitRegistry.h
 * @brief Process-wide kit registry — single source of truth for all
 *        kit data, used by every sibling addon through the C ABI.
 *
 * Moved from modular's ModularKitRegistry as part of Phase R1 of the
 * core-kits refactor (see Documentation/Developers/CoreKitsRefactor.md).
 */

#pragma once

#include "LBKitTypes.h"

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

class LBKitRegistry
{
public:
    static LBKitRegistry& Get();

    // Construct a default starter kit if no kits exist. Idempotent.
    // Called from core's OnLoad so siblings see a populated registry.
    void EnsureBuiltinKit();

    // Drop every kit + active selection (does NOT reset the project-root
    // cache or override — those persist across reloads).
    void Clear();

    // Mutation
    LBKit* CreateKit(const std::string& name);
    LBKit* FindKit(const std::string& name);
    LBKit* FindKitByIndex(int idx);
    void   RemoveKit(const std::string& name);

    // R4: rename a kit in-place. Updates the by-name map, the order
    // vector, and the active-kit pointer if it matched the old name.
    // Returns false if `oldName` doesn't exist or `newName` collides
    // with an existing kit. Leaves sourceFile alone — the caller decides
    // whether to also rename the on-disk JSON file.
    bool RenameKit(const std::string& oldName, const std::string& newName);

    int    GetKitCount() const { return (int)mOrder.size(); }
    int    FindKitIndex(const std::string& name) const;
    const  std::string& GetKitNameAt(int idx) const;

    void               SetActiveKit(const std::string& name);
    const std::string& GetActiveKitName() const { return mActive; }
    LBKit*             GetActiveKit();

    // Load a single kit JSON. Replaces any existing kit with the same name.
    LBKit* LoadKitFromFile(const std::string& path, std::string* outError);

    // Walk a directory for *.json and load each one. Non-recursive.
    int ScanKitsFolder(const std::string& folderPath,
                       std::vector<std::string>* outErrors = nullptr);

    // Discard the registry then re-scan <project>/Kits/. Returns the kit
    // count after reload. Errors are stashed in mLastReloadErrors for the
    // UI to surface via Kit_GetLastReload*.
    int ScanProjectKits();

    int                              GetLastReloadKitCount()   const { return mLastReloadKitCount; }
    int                              GetLastReloadErrorCount() const { return (int)mLastReloadErrors.size(); }
    const std::vector<std::string>&  GetLastReloadErrors()     const { return mLastReloadErrors; }

    // Project root resolver — strategies (in priority order):
    //   0. Explicit override via SetProjectRootOverride().
    //   1. POLYPHASE_PROJECT environment variable.
    //   2. Walk up from the running core DLL's parent dir looking for
    //      a project marker (PolyphaseConfig.cmake, *.octp, Config.ini +
    //      Assets/, or a Packages/ subdirectory).
    //   3. Same walk from the editor's current working directory.
    static const std::string& GetProjectRoot();
    static const std::string& GetProjectKitsFolder();
    static void SetProjectRootOverride(const char* path);
    static void ClearProjectRootCache();

private:
    LBKitRegistry() = default;

    std::unordered_map<std::string, std::unique_ptr<LBKit>> mKits;
    std::vector<std::string>                                mOrder;
    std::string                                             mActive;

    int                                                     mLastReloadKitCount = 0;
    std::vector<std::string>                                mLastReloadErrors;
};
