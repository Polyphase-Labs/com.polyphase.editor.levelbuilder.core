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
}
