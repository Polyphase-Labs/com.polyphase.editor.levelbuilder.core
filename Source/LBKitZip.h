/**
 * @file LBKitZip.h
 * @brief Minimal store-only PKZIP read/write for the `.kit` share format.
 *
 * The `.kit` archive layout:
 *
 *     kit.json              ← canonical kit JSON (LBKitJson schema)
 *     thumbs/<piece>.png    ← optional piece thumbnails (matches iconPath)
 *     Preview.png           ← optional kit-level hero image
 *     README.md             ← optional artist notes
 *
 * Compression is intentionally disabled: kits are small, PNG payloads
 * don't compress meaningfully, and a stored-only zip lets us ship the
 * code as a self-contained ~250-line TU instead of pulling miniz in.
 *
 * Phase S2 of the sharing system (see Documentation/Developers/KitSharing.md).
 */

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace LBKitZip
{
    // One entry in a `.kit` archive, ready to feed Write() or yielded by
    // List() / Read(). `data` is the file's raw bytes; `name` uses forward
    // slashes and a stable relative-to-archive-root path.
    struct Entry
    {
        std::string             name;
        std::vector<uint8_t>    data;
    };

    // ---- Writer ---------------------------------------------------------

    // Pack `entries` into a fresh zip file at `dstPath`. Truncates if the
    // destination exists. Returns true on success; on failure fills
    // outError with a human-readable message.
    bool Write(const std::string& dstPath,
               const std::vector<Entry>& entries,
               std::string& outError);

    // ---- Reader ---------------------------------------------------------

    // Enumerate the zip's central directory. `outNames` is filled with one
    // entry per archived file (forward-slash relative paths).
    bool List(const std::string& srcPath,
              std::vector<std::string>& outNames,
              std::string& outError);

    // Read a single entry by name into `outBytes`. Returns false if the
    // entry doesn't exist or the archive is corrupt.
    bool ReadEntry(const std::string& srcPath,
                   const std::string& entryName,
                   std::vector<uint8_t>& outBytes,
                   std::string& outError);

    // Extract every entry into `dstDir`. Refuses any entry whose
    // normalized path would escape `dstDir` (zip-slip mitigation).
    bool ExtractAll(const std::string& srcPath,
                    const std::string& dstDir,
                    std::string& outError);
}
