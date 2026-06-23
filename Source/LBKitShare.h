/**
 * @file LBKitShare.h
 * @brief Phase S2 — high-level export and import of `.kit` archives.
 *
 * Wraps LBKitZip with kit-aware semantics: an export pulls the kit's
 * JSON + piece thumbnails + preview image into a single archive; an
 * import places everything back where the kit references it.
 */

#pragma once

#include <string>

struct LBKit;

namespace LBKitShare
{
    // Bundle the kit at `kitIdx` into a fresh `.kit` zip at `dstPath`.
    // Pulls in any thumbnails / preview image referenced by the kit IF
    // those files exist on disk. Returns true on success.
    bool ExportKit(int kitIdx, const std::string& dstPath, std::string& outError);

    // Read just `kit.json` from a `.kit` archive into `outKit` (does NOT
    // touch disk anywhere else). Used for the import confirmation dialog.
    bool PreviewKitFile(const std::string& srcPath,
                        LBKit& outKit,
                        std::string& outError);

    // Conflict-resolution modes for ImportKit when a kit with the same
    // kitId is already registered.
    enum class ConflictMode
    {
        Replace = 0,      // overwrite the existing Kits/*.json
        RenameNew = 1,    // install with a numeric suffix on the file name
        Abort = 2         // refuse to import; fill outError
    };

    // Extract `.kit` archive into the project's Kits/ folder. On success
    // returns the destination JSON path (for UI feedback) AND the kit id
    // we imported. The caller is expected to call Kit_Reload() afterwards.
    bool ImportKit(const std::string& srcPath,
                   ConflictMode conflictMode,
                   std::string& outDstPath,
                   std::string& outKitId,
                   std::string& outError);
}
