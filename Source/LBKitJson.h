/**
 * @file LBKitJson.h
 * @brief JSON I/O for kits (moved from modular's ModularKitJson.h).
 *
 * Parses + writes the artist-facing kit schema documented in
 * Documentation/Artists/GettingStarted.md.
 */

#pragma once

#include <string>

struct LBKit;

namespace LBKitJson
{
    // Parse a kit JSON file at `path` into outKit. On failure fills
    // outError with a human-readable message and returns false.
    bool LoadFromFile(const std::string& path, LBKit& outKit, std::string& outError);

    // In-memory parse. `sourceName` is used only in error messages.
    bool LoadFromString(const std::string& sourceName,
                        const char* jsonText, size_t length,
                        LBKit& outKit, std::string& outError);

    // Pretty-print the kit back to disk. Round-trips byte-identically for
    // the fields this module knows about; unknown fields are dropped.
    bool SaveToFile(const std::string& path, const LBKit& kit, std::string& outError);

    // ---- Phase 3 of KitAuthoringUI: folder-mode kit layout ----------
    //
    // The folder-mode layout splits a kit across many files for
    // git-friendliness and conflict-free multi-artist edits (see
    // Documentation/Developers/KitAuthoringUI.md §4):
    //
    //   <KitFolder>/
    //     kit.json               -- kit metadata + pieces index
    //                                ("pieces": [{ "id": str, "file": str }])
    //     pieces/<PieceName>.json -- one piece per file
    //
    // SaveToFolder writes kit.json + pieces/*.json from `kit`. Stale
    // per-piece files (pieces no longer in the kit) are removed.
    // LoadFromFolder reads them back into outKit. Both round-trip
    // through the same JSON shape as the single-file format, just split
    // — so a kit can move between layouts without losing data.
    bool SaveToFolder(const std::string& folderPath, const LBKit& kit, std::string& outError);
    bool LoadFromFolder(const std::string& folderPath, LBKit& outKit, std::string& outError);

    // True when the JSON document at `path` looks like a folder-mode
    // kit.json (its "pieces" array entries reference external files
    // via { "id":..., "file":... } instead of containing the piece
    // inline). Cheap — opens and parses only the kit.json. Returns
    // false on any parse error or for legacy single-file kits.
    bool IsFolderModeKitJson(const std::string& path);
}
