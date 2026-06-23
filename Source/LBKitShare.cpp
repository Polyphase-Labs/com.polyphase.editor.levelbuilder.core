#include "LBKitShare.h"

#include "LBKitJson.h"
#include "LBKitRegistry.h"
#include "LBKitTypes.h"
#include "LBKitZip.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace
{
    namespace fs = std::filesystem;

    // Slurp a file into a byte vector. Returns false on open / read fail.
    bool ReadFileBytes(const fs::path& p, std::vector<uint8_t>& out)
    {
        std::ifstream f(p, std::ios::binary | std::ios::ate);
        if (!f) return false;
        const std::streamsize sz = f.tellg();
        if (sz < 0) return false;
        out.resize((size_t)sz);
        f.seekg(0, std::ios::beg);
        if (sz == 0) return true;
        f.read(reinterpret_cast<char*>(out.data()), sz);
        return (bool)f;
    }

    // Try to resolve a kit-relative or project-relative path to a real
    // file on disk. Searches:
    //   1. Project root + path (matches the iconPath convention).
    //   2. Kit's source-file folder + path (matches the previewImage
    //      convention when path is relative to the kit folder).
    // Returns empty path on miss.
    fs::path ResolveKitAssetPath(const LBKit& kit, const std::string& relPath)
    {
        if (relPath.empty()) return {};
        std::error_code ec;
        const std::string& root = LBKitRegistry::GetProjectRoot();
        if (!root.empty())
        {
            fs::path p = fs::path(root) / relPath;
            if (fs::is_regular_file(p, ec)) return p;
        }
        if (!kit.sourceFile.empty())
        {
            fs::path p = fs::path(kit.sourceFile).parent_path() / relPath;
            if (fs::is_regular_file(p, ec)) return p;
        }
        return {};
    }

    // Pick a filesystem-safe basename for the destination JSON file.
    // Prefers `kit.kitId` (last reverse-DNS segment), falls back to
    // `kit.name`, then "imported-kit". Strips characters that break
    // common filesystems.
    std::string ChooseBaseName(const LBKit& kit)
    {
        std::string base;
        if (!kit.kitId.empty())
        {
            auto pos = kit.kitId.find_last_of('.');
            base = (pos == std::string::npos) ? kit.kitId : kit.kitId.substr(pos + 1);
        }
        if (base.empty()) base = kit.name;
        if (base.empty()) base = "imported-kit";

        std::string safe;
        safe.reserve(base.size());
        for (char c : base)
        {
            const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')
                         || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.';
            safe += ok ? c : '_';
        }
        if (safe.empty()) safe = "imported-kit";
        return safe;
    }
}

namespace LBKitShare
{
    // ---- Export ---------------------------------------------------------

    bool ExportKit(int kitIdx, const std::string& dstPath, std::string& outError)
    {
        LBKit* kit = LBKitRegistry::Get().FindKitByIndex(kitIdx);
        if (!kit)
        {
            outError = "ExportKit: kit index out of range";
            return false;
        }
        if (dstPath.empty())
        {
            outError = "ExportKit: destination path is empty";
            return false;
        }

        // 1. kit.json (regenerated, NOT a verbatim copy of sourceFile —
        //    so any in-memory edits the user hasn't saved get bundled).
        //    Lean on LBKitJson::SaveToFile by routing through a temp.
        std::vector<LBKitZip::Entry> entries;

        std::string kitJsonStr;
        {
            std::error_code ec;
            fs::path tmp = fs::temp_directory_path(ec) /
                           ("lb_export_" + ChooseBaseName(*kit) + ".json");
            std::string err;
            if (!LBKitJson::SaveToFile(tmp.string(), *kit, err))
            {
                outError = "ExportKit: rendering kit.json failed: " + err;
                return false;
            }
            std::ifstream in(tmp, std::ios::binary);
            std::ostringstream oss;
            oss << in.rdbuf();
            kitJsonStr = oss.str();
            fs::remove(tmp, ec);
        }
        if (kitJsonStr.empty())
        {
            outError = "ExportKit: empty kit.json after render";
            return false;
        }
        LBKitZip::Entry kitEntry;
        kitEntry.name = "kit.json";
        kitEntry.data.assign(kitJsonStr.begin(), kitJsonStr.end());
        entries.push_back(std::move(kitEntry));

        // 2. Preview image (optional).
        if (!kit->previewImage.empty())
        {
            fs::path src = ResolveKitAssetPath(*kit, kit->previewImage);
            if (!src.empty())
            {
                LBKitZip::Entry e;
                e.name = kit->previewImage;
                if (ReadFileBytes(src, e.data))
                    entries.push_back(std::move(e));
            }
        }

        // 3. Piece thumbnails (deduplicated — multiple pieces may share an icon).
        std::vector<std::string> seenIcons;
        for (const LBPiece& p : kit->pieces)
        {
            if (p.iconPath.empty()) continue;
            bool already = false;
            for (const auto& s : seenIcons) if (s == p.iconPath) { already = true; break; }
            if (already) continue;
            seenIcons.push_back(p.iconPath);

            fs::path src = ResolveKitAssetPath(*kit, p.iconPath);
            if (src.empty()) continue;   // silently skip missing icons

            LBKitZip::Entry e;
            e.name = p.iconPath;
            if (ReadFileBytes(src, e.data))
                entries.push_back(std::move(e));
        }

        return LBKitZip::Write(dstPath, entries, outError);
    }

    // ---- Preview --------------------------------------------------------

    bool PreviewKitFile(const std::string& srcPath, LBKit& outKit, std::string& outError)
    {
        std::vector<uint8_t> kitJson;
        if (!LBKitZip::ReadEntry(srcPath, "kit.json", kitJson, outError))
            return false;
        return LBKitJson::LoadFromString(srcPath, (const char*)kitJson.data(),
                                         kitJson.size(), outKit, outError);
    }

    // ---- Import ---------------------------------------------------------

    bool ImportKit(const std::string& srcPath,
                   ConflictMode conflictMode,
                   std::string& outDstPath,
                   std::string& outKitId,
                   std::string& outError)
    {
        // 1. Peek the manifest so we know how to name the destination.
        LBKit preview;
        if (!PreviewKitFile(srcPath, preview, outError))
            return false;
        outKitId = preview.kitId;

        // 2. Resolve destination paths.
        const std::string& kitsFolder = LBKitRegistry::GetProjectKitsFolder();
        if (kitsFolder.empty())
        {
            outError = "ImportKit: project Kits/ folder couldn't be resolved";
            return false;
        }
        std::error_code ec;
        fs::create_directories(kitsFolder, ec);

        const std::string& projectRoot = LBKitRegistry::GetProjectRoot();
        const std::string  baseName    = ChooseBaseName(preview);

        fs::path dstJson = fs::path(kitsFolder) / (baseName + ".json");
        if (fs::exists(dstJson, ec))
        {
            switch (conflictMode)
            {
            case ConflictMode::Abort:
                outError = "ImportKit: kit JSON already exists at " + dstJson.string();
                return false;
            case ConflictMode::RenameNew:
            {
                int suffix = 2;
                fs::path candidate;
                do {
                    candidate = fs::path(kitsFolder) /
                                (baseName + "-" + std::to_string(suffix) + ".json");
                    ++suffix;
                } while (fs::exists(candidate, ec) && suffix < 1000);
                dstJson = candidate;
                break;
            }
            case ConflictMode::Replace:
                // fall through — we'll overwrite.
                break;
            }
        }
        outDstPath = dstJson.string();

        // 3. List the zip contents so we can extract non-kit.json entries
        //    into the project root (which is where kit.json's relative
        //    paths — icon / previewImage — resolve to).
        std::vector<std::string> entryNames;
        if (!LBKitZip::List(srcPath, entryNames, outError))
            return false;

        // 4. Write kit.json to its destination.
        {
            std::vector<uint8_t> kitJsonBytes;
            if (!LBKitZip::ReadEntry(srcPath, "kit.json", kitJsonBytes, outError))
                return false;
            std::ofstream out(dstJson, std::ios::binary | std::ios::trunc);
            if (!out.is_open())
            {
                outError = "ImportKit: could not open destination " + dstJson.string();
                return false;
            }
            if (!kitJsonBytes.empty())
                out.write(reinterpret_cast<const char*>(kitJsonBytes.data()),
                          (std::streamsize)kitJsonBytes.size());
            if (!out.good())
            {
                outError = "ImportKit: write failed for " + dstJson.string();
                return false;
            }
        }

        // 5. Extract every OTHER entry under the project root (so the
        //    iconPath / previewImage relative-to-project-root paths
        //    inside kit.json resolve naturally). Skip kit.json (already
        //    handled) and directory placeholders.
        if (!projectRoot.empty())
        {
            std::vector<uint8_t> bytes;
            for (const std::string& name : entryNames)
            {
                if (name == "kit.json") continue;
                if (name.empty() || name.back() == '/' || name.back() == '\\') continue;

                bytes.clear();
                std::string fetchErr;
                if (!LBKitZip::ReadEntry(srcPath, name, bytes, fetchErr))
                {
                    // Non-fatal — log via outError on the way out so the
                    // UI can show what was skipped, but still complete
                    // the import for the JSON.
                    outError += (outError.empty() ? "" : "\n");
                    outError += "ImportKit: skipped " + name + ": " + fetchErr;
                    continue;
                }
                fs::path outPath = fs::path(projectRoot) / name;
                fs::create_directories(outPath.parent_path(), ec);
                std::ofstream out(outPath, std::ios::binary | std::ios::trunc);
                if (!out.is_open()) continue;
                if (!bytes.empty())
                    out.write(reinterpret_cast<const char*>(bytes.data()),
                              (std::streamsize)bytes.size());
            }
        }

        return true;
    }
}
