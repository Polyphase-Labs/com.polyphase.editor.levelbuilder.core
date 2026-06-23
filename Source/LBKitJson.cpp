#include "LBKitJson.h"

#include "LBKitTypes.h"

// rapidjson lives under External/Assimp/contrib/rapidjson; we qualify the
// include relative to the existing External/Assimp include path so the
// build doesn't need a rapidjson-specific include directory.
#include "contrib/rapidjson/document.h"
#include "contrib/rapidjson/error/en.h"
#include "contrib/rapidjson/prettywriter.h"
#include "contrib/rapidjson/stringbuffer.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <unordered_set>

namespace
{
    bool ReadString(const rapidjson::Value& obj, const char* key, std::string& out)
    {
        if (!obj.IsObject()) return false;
        auto it = obj.FindMember(key);
        if (it == obj.MemberEnd() || !it->value.IsString()) return false;
        out.assign(it->value.GetString(), it->value.GetStringLength());
        return true;
    }

    bool ReadFloatArray(const rapidjson::Value& obj, const char* key, float* out, int count)
    {
        if (!obj.IsObject()) return false;
        auto it = obj.FindMember(key);
        if (it == obj.MemberEnd() || !it->value.IsArray()) return false;
        const auto& arr = it->value;
        if ((int)arr.Size() != count) return false;
        for (int i = 0; i < count; ++i)
        {
            const auto& v = arr[i];
            if (!v.IsNumber()) return false;
            out[i] = (float)v.GetDouble();
        }
        return true;
    }

    bool ReadStringArray(const rapidjson::Value& obj, const char* key, std::vector<std::string>& out)
    {
        if (!obj.IsObject()) return true;     // optional — no key means OK
        auto it = obj.FindMember(key);
        if (it == obj.MemberEnd()) return true;
        if (!it->value.IsArray()) return false;
        for (auto& v : it->value.GetArray())
        {
            if (!v.IsString()) return false;
            out.emplace_back(v.GetString(), v.GetStringLength());
        }
        return true;
    }

    bool ParseSocket(const rapidjson::Value& src, LBSocket& s, std::string& err)
    {
        if (!ReadString(src, "name", s.name))
        {
            err = "socket missing required string 'name'";
            return false;
        }
        if (!ReadString(src, "type", s.type))
        {
            err = "socket '" + s.name + "' missing required string 'type'";
            return false;
        }
        if (!ReadFloatArray(src, "position", s.localPos, 3))
        {
            err = "socket '" + s.name + "' needs 'position': [x,y,z]";
            return false;
        }
        if (src.HasMember("rotation"))
        {
            if (!ReadFloatArray(src, "rotation", s.localEulerDeg, 3))
            {
                err = "socket '" + s.name + "' has invalid 'rotation' (need [pitch,yaw,roll] in degrees)";
                return false;
            }
        }
        if (!ReadStringArray(src, "compatibleTypes", s.compatibleTypes))
        {
            err = "socket '" + s.name + "' has invalid 'compatibleTypes' (need array of strings)";
            return false;
        }
        return true;
    }

    // FNV-1a 64-bit hash, hex-encoded to 12 chars. Used as a stable
    // synthesized kitId when the kit JSON has none. Not cryptographic —
    // we just need a deterministic short ID derived from kit content.
    std::string SynthesizeKitId(const LBKit& kit)
    {
        uint64_t h = 0xcbf29ce484222325ull;
        auto hashStr = [&](const std::string& s) {
            for (unsigned char c : s) { h ^= c; h *= 0x100000001b3ull; }
        };
        hashStr(kit.name);
        hashStr(kit.author);
        if (!kit.pieces.empty()) hashStr(kit.pieces.front().name);

        char buf[32];
        std::snprintf(buf, sizeof(buf), "synth.%012llx", (unsigned long long)h);
        return std::string(buf);
    }

    bool ParseDependency(const rapidjson::Value& src, LBKitDependency& d, std::string& err)
    {
        if (!ReadString(src, "kitId", d.kitId))
        {
            err = "dependency missing required string 'kitId'";
            return false;
        }
        ReadString(src, "version", d.version);   // optional
        return true;
    }

    // Filesystem-safe piece-file basename. Lowercases ASCII, replaces any
    // char that isn't [a-z0-9_.-] with '_'. Used by folder-mode write to
    // derive pieces/<basename>.json from each piece's display name.
    std::string SanitizePieceFile(const std::string& name)
    {
        std::string out;
        out.reserve(name.size());
        for (char c : name)
        {
            unsigned char u = (unsigned char)c;
            bool keep =
                (u >= '0' && u <= '9') ||
                (u >= 'a' && u <= 'z') ||
                (u >= 'A' && u <= 'Z') ||
                u == '_' || u == '-' || u == '.';
            out.push_back(keep ? (char)u : '_');
        }
        if (out.empty()) out = "piece";
        return out;
    }

    // Write a single LBPiece's body as a JSON object using the given
    // rapidjson PrettyWriter. Shared by SaveToFile (single-file embedded
    // pieces array) and SaveToFolder (per-piece files). Caller controls
    // StartObject / EndObject framing so SaveToFolder can write a piece
    // as the top-level object of pieces/<name>.json with no outer wrapper.
    template <class W>
    void WritePieceBody(W& w, const LBPiece& p)
    {
        w.Key("name");  w.String(p.name.c_str(),      (rapidjson::SizeType)p.name.size());
        w.Key("asset"); w.String(p.assetName.c_str(), (rapidjson::SizeType)p.assetName.size());
        if (!p.category.empty())
        {
            w.Key("category");
            w.String(p.category.c_str(), (rapidjson::SizeType)p.category.size());
        }
        if (!p.iconPath.empty())
        {
            w.Key("icon");
            w.String(p.iconPath.c_str(), (rapidjson::SizeType)p.iconPath.size());
        }
        w.Key("size");
        w.StartArray();
        w.Double(p.size[0]); w.Double(p.size[1]); w.Double(p.size[2]);
        w.EndArray();
        if (!p.sockets.empty())
        {
            w.Key("sockets");
            w.StartArray();
            for (const LBSocket& s : p.sockets)
            {
                w.StartObject();
                w.Key("name"); w.String(s.name.c_str(), (rapidjson::SizeType)s.name.size());
                w.Key("type"); w.String(s.type.c_str(), (rapidjson::SizeType)s.type.size());
                w.Key("position");
                w.StartArray();
                w.Double(s.localPos[0]); w.Double(s.localPos[1]); w.Double(s.localPos[2]);
                w.EndArray();
                if (s.localEulerDeg[0] != 0.0f
                    || s.localEulerDeg[1] != 0.0f
                    || s.localEulerDeg[2] != 0.0f)
                {
                    w.Key("rotation");
                    w.StartArray();
                    w.Double(s.localEulerDeg[0]);
                    w.Double(s.localEulerDeg[1]);
                    w.Double(s.localEulerDeg[2]);
                    w.EndArray();
                }
                if (!s.compatibleTypes.empty())
                {
                    w.Key("compatibleTypes");
                    w.StartArray();
                    for (const std::string& ct : s.compatibleTypes)
                        w.String(ct.c_str(), (rapidjson::SizeType)ct.size());
                    w.EndArray();
                }
                w.EndObject();
            }
            w.EndArray();
        }
    }

    bool ParsePiece(const rapidjson::Value& src, LBPiece& p, std::string& err)
    {
        if (!ReadString(src, "name", p.name))
        {
            err = "piece missing required string 'name'";
            return false;
        }
        if (!ReadString(src, "asset", p.assetName))
        {
            err = "piece '" + p.name + "' missing required string 'asset'";
            return false;
        }
        ReadString(src, "category", p.category);   // optional
        ReadString(src, "icon",     p.iconPath);   // optional; relative to project root
        if (src.HasMember("size"))
        {
            if (!ReadFloatArray(src, "size", p.size, 3))
            {
                err = "piece '" + p.name + "' has invalid 'size' (need [x,y,z])";
                return false;
            }
        }
        if (src.HasMember("sockets"))
        {
            const auto& socketsVal = src["sockets"];
            if (!socketsVal.IsArray())
            {
                err = "piece '" + p.name + "' has 'sockets' that isn't an array";
                return false;
            }
            for (auto& sv : socketsVal.GetArray())
            {
                LBSocket sock;
                if (!ParseSocket(sv, sock, err))
                {
                    err = "in piece '" + p.name + "': " + err;
                    return false;
                }
                p.sockets.push_back(std::move(sock));
            }
        }
        return true;
    }
}

namespace LBKitJson
{
    bool LoadFromString(const std::string& sourceName,
                        const char* jsonText, size_t length,
                        LBKit& outKit, std::string& outError)
    {
        rapidjson::Document doc;
        doc.Parse(jsonText, length);
        if (doc.HasParseError())
        {
            char buf[256];
            std::snprintf(buf, sizeof(buf),
                          "%s: JSON parse error at offset %zu: %s",
                          sourceName.c_str(),
                          (size_t)doc.GetErrorOffset(),
                          rapidjson::GetParseError_En(doc.GetParseError()));
            outError = buf;
            return false;
        }
        if (!doc.IsObject())
        {
            outError = sourceName + ": top-level JSON value must be an object";
            return false;
        }
        if (!ReadString(doc, "kitName", outKit.name))
        {
            outError = sourceName + ": missing required string 'kitName'";
            return false;
        }

        // ----- S1 sharing metadata (all optional) -----
        ReadString(doc, "kitVersion",              outKit.kitVersion);
        ReadString(doc, "author",                  outKit.author);
        ReadString(doc, "authorUrl",               outKit.authorUrl);
        ReadString(doc, "license",                 outKit.license);
        ReadString(doc, "licenseUrl",              outKit.licenseUrl);
        ReadString(doc, "description",             outKit.description);
        ReadString(doc, "previewImage",            outKit.previewImage);
        ReadString(doc, "homepage",                outKit.homepage);
        ReadString(doc, "minimumPolyphaseVersion", outKit.minimumPolyphaseVersion);
        if (!ReadStringArray(doc, "tags", outKit.tags))
        {
            outError = sourceName + ": 'tags' must be an array of strings";
            return false;
        }

        if (doc.HasMember("dependencies"))
        {
            const auto& deps = doc["dependencies"];
            if (!deps.IsArray())
            {
                outError = sourceName + ": 'dependencies' must be an array";
                return false;
            }
            for (auto& dv : deps.GetArray())
            {
                LBKitDependency d;
                std::string err;
                if (!ParseDependency(dv, d, err))
                {
                    outError = sourceName + ": " + err;
                    return false;
                }
                outKit.dependencies.push_back(std::move(d));
            }
        }

        // kitId is the only metadata field that requires special handling:
        // if absent, synthesize one from a content hash (and remember we
        // did so, so the UI can flag it as "unstable id"). Save will then
        // write it out explicitly.
        if (!ReadString(doc, "kitId", outKit.kitId))
        {
            outKit.kitIdSynthesized = false;   // pieces aren't loaded yet
        }
        else
        {
            outKit.kitIdSynthesized = false;
        }

        if (!doc.HasMember("pieces") || !doc["pieces"].IsArray())
        {
            outError = sourceName + ": missing required array 'pieces'";
            return false;
        }
        const auto& pieces = doc["pieces"];
        outKit.pieces.clear();
        outKit.pieces.reserve(pieces.Size());
        for (auto& pv : pieces.GetArray())
        {
            LBPiece p;
            std::string err;
            if (!ParsePiece(pv, p, err))
            {
                outError = sourceName + ": " + err;
                return false;
            }
            outKit.pieces.push_back(std::move(p));
        }

        // Synthesize a kitId now that pieces are loaded (the hash uses the
        // first piece's name as one of its inputs).
        if (outKit.kitId.empty())
        {
            outKit.kitId = SynthesizeKitId(outKit);
            outKit.kitIdSynthesized = true;
        }
        return true;
    }

    bool LoadFromFile(const std::string& path, LBKit& outKit, std::string& outError)
    {
        std::ifstream f(path, std::ios::binary);
        if (!f.is_open())
        {
            outError = "could not open file: " + path;
            return false;
        }
        std::ostringstream ss;
        ss << f.rdbuf();
        std::string contents = ss.str();
        return LoadFromString(path, contents.c_str(), contents.size(), outKit, outError);
    }

    bool SaveToFile(const std::string& path, const LBKit& kit, std::string& outError)
    {
        if (path.empty())
        {
            outError = "cannot save kit '" + kit.name + "': no source file path";
            return false;
        }

        rapidjson::StringBuffer sb;
        rapidjson::PrettyWriter<rapidjson::StringBuffer> w(sb);
        w.SetIndent(' ', 4);

        w.StartObject();
        w.Key("kitName"); w.String(kit.name.c_str(), (rapidjson::SizeType)kit.name.size());

        // ----- S1 metadata (write only non-empty fields to keep small
        // kit files clean). kitId is always written so synthesized ids
        // persist as explicit values after the first save.
        auto writeStr = [&](const char* key, const std::string& v)
        {
            if (v.empty()) return;
            w.Key(key); w.String(v.c_str(), (rapidjson::SizeType)v.size());
        };
        if (!kit.kitId.empty()) { w.Key("kitId"); w.String(kit.kitId.c_str(), (rapidjson::SizeType)kit.kitId.size()); }
        writeStr("kitVersion",              kit.kitVersion);
        writeStr("author",                  kit.author);
        writeStr("authorUrl",               kit.authorUrl);
        writeStr("license",                 kit.license);
        writeStr("licenseUrl",              kit.licenseUrl);
        writeStr("description",             kit.description);
        writeStr("previewImage",            kit.previewImage);
        writeStr("homepage",                kit.homepage);
        writeStr("minimumPolyphaseVersion", kit.minimumPolyphaseVersion);
        if (!kit.tags.empty())
        {
            w.Key("tags");
            w.StartArray();
            for (const std::string& t : kit.tags)
                w.String(t.c_str(), (rapidjson::SizeType)t.size());
            w.EndArray();
        }
        if (!kit.dependencies.empty())
        {
            w.Key("dependencies");
            w.StartArray();
            for (const LBKitDependency& d : kit.dependencies)
            {
                w.StartObject();
                w.Key("kitId");
                w.String(d.kitId.c_str(), (rapidjson::SizeType)d.kitId.size());
                if (!d.version.empty())
                {
                    w.Key("version");
                    w.String(d.version.c_str(), (rapidjson::SizeType)d.version.size());
                }
                w.EndObject();
            }
            w.EndArray();
        }

        w.Key("pieces");
        w.StartArray();
        for (const LBPiece& p : kit.pieces)
        {
            w.StartObject();

            w.Key("name");  w.String(p.name.c_str(),      (rapidjson::SizeType)p.name.size());
            w.Key("asset"); w.String(p.assetName.c_str(), (rapidjson::SizeType)p.assetName.size());
            if (!p.category.empty())
            {
                w.Key("category");
                w.String(p.category.c_str(), (rapidjson::SizeType)p.category.size());
            }
            if (!p.iconPath.empty())
            {
                w.Key("icon");
                w.String(p.iconPath.c_str(), (rapidjson::SizeType)p.iconPath.size());
            }

            w.Key("size");
            w.StartArray();
            w.Double(p.size[0]);
            w.Double(p.size[1]);
            w.Double(p.size[2]);
            w.EndArray();

            if (!p.sockets.empty())
            {
                w.Key("sockets");
                w.StartArray();
                for (const LBSocket& s : p.sockets)
                {
                    w.StartObject();
                    w.Key("name"); w.String(s.name.c_str(), (rapidjson::SizeType)s.name.size());
                    w.Key("type"); w.String(s.type.c_str(), (rapidjson::SizeType)s.type.size());

                    w.Key("position");
                    w.StartArray();
                    w.Double(s.localPos[0]);
                    w.Double(s.localPos[1]);
                    w.Double(s.localPos[2]);
                    w.EndArray();

                    if (s.localEulerDeg[0] != 0.0f
                        || s.localEulerDeg[1] != 0.0f
                        || s.localEulerDeg[2] != 0.0f)
                    {
                        w.Key("rotation");
                        w.StartArray();
                        w.Double(s.localEulerDeg[0]);
                        w.Double(s.localEulerDeg[1]);
                        w.Double(s.localEulerDeg[2]);
                        w.EndArray();
                    }

                    if (!s.compatibleTypes.empty())
                    {
                        w.Key("compatibleTypes");
                        w.StartArray();
                        for (const std::string& ct : s.compatibleTypes)
                            w.String(ct.c_str(), (rapidjson::SizeType)ct.size());
                        w.EndArray();
                    }
                    w.EndObject();
                }
                w.EndArray();
            }
            w.EndObject();
        }
        w.EndArray();
        w.EndObject();

        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        if (!out.is_open())
        {
            outError = "could not open file for write: " + path;
            return false;
        }
        out.write(sb.GetString(), (std::streamsize)sb.GetSize());
        if (!out.good())
        {
            outError = "write failed: " + path;
            return false;
        }
        return true;
    }

    // ===== Phase 3: folder-mode kit layout =================================
    //
    // kit.json (metadata + index) + pieces/<basename>.json (per piece).
    // See Documentation/Developers/KitAuthoringUI.md §4 for the schema.

    bool IsFolderModeKitJson(const std::string& path)
    {
        std::ifstream f(path, std::ios::binary);
        if (!f.is_open()) return false;
        std::ostringstream ss;
        ss << f.rdbuf();
        std::string contents = ss.str();

        rapidjson::Document doc;
        doc.Parse(contents.c_str(), contents.size());
        if (doc.HasParseError() || !doc.IsObject()) return false;
        auto it = doc.FindMember("pieces");
        if (it == doc.MemberEnd() || !it->value.IsArray()) return false;
        const auto& arr = it->value;
        if (arr.Empty()) return false;       // ambiguous — treat as legacy
        const auto& first = arr[0];
        if (!first.IsObject()) return false;
        // Folder-mode entries have a "file" string and NO "asset" string;
        // legacy entries have "name" + "asset" inline.
        return first.HasMember("file")
            && first["file"].IsString()
            && !first.HasMember("asset");
    }

    bool SaveToFolder(const std::string& folderPath, const LBKit& kit, std::string& outError)
    {
        namespace fs = std::filesystem;
        if (folderPath.empty())
        {
            outError = "cannot save kit '" + kit.name + "': empty folder path";
            return false;
        }

        std::error_code ec;
        fs::create_directories(folderPath, ec);
        fs::path piecesDir = fs::path(folderPath) / "pieces";
        fs::create_directories(piecesDir, ec);
        if (ec)
        {
            outError = "could not create folder: " + folderPath + " (" + ec.message() + ")";
            return false;
        }

        // Track which piece files we're writing this round so stale ones
        // (pieces that were renamed or removed since the last save) can
        // be cleaned out at the end. Lower-cased for case-insensitive
        // diff vs. the directory listing on Windows.
        std::unordered_set<std::string> liveFiles;

        // ---- Write each pieces/<basename>.json ----
        // Disambiguate basename collisions (two pieces sanitizing to the
        // same filename) by appending _2, _3, ... in registration order.
        std::unordered_set<std::string> usedBasenames;
        std::vector<std::string> pieceFileRels;   // "pieces/<basename>.json" parallel to kit.pieces
        pieceFileRels.reserve(kit.pieces.size());

        for (const LBPiece& p : kit.pieces)
        {
            std::string base = SanitizePieceFile(p.name);
            std::string candidate = base;
            int suffix = 2;
            while (usedBasenames.count(candidate))
            {
                char buf[16];
                std::snprintf(buf, sizeof(buf), "_%d", suffix++);
                candidate = base + buf;
            }
            usedBasenames.insert(candidate);

            std::string rel = "pieces/" + candidate + ".json";
            pieceFileRels.push_back(rel);
            liveFiles.insert(candidate + ".json");

            // Write the piece file.
            rapidjson::StringBuffer sb;
            rapidjson::PrettyWriter<rapidjson::StringBuffer> w(sb);
            w.SetIndent(' ', 4);
            w.StartObject();
            WritePieceBody(w, p);
            w.EndObject();

            fs::path dst = fs::path(folderPath) / rel;
            std::ofstream out(dst, std::ios::binary | std::ios::trunc);
            if (!out.is_open())
            {
                outError = "could not open for write: " + dst.string();
                return false;
            }
            out.write(sb.GetString(), (std::streamsize)sb.GetSize());
            if (!out.good())
            {
                outError = "write failed: " + dst.string();
                return false;
            }
        }

        // ---- Write kit.json (metadata + pieces index) ----
        rapidjson::StringBuffer sb;
        rapidjson::PrettyWriter<rapidjson::StringBuffer> w(sb);
        w.SetIndent(' ', 4);

        w.StartObject();
        w.Key("kitName"); w.String(kit.name.c_str(), (rapidjson::SizeType)kit.name.size());

        auto writeStr = [&](const char* key, const std::string& v)
        {
            if (v.empty()) return;
            w.Key(key); w.String(v.c_str(), (rapidjson::SizeType)v.size());
        };
        if (!kit.kitId.empty()) { w.Key("kitId"); w.String(kit.kitId.c_str(), (rapidjson::SizeType)kit.kitId.size()); }
        writeStr("kitVersion",              kit.kitVersion);
        writeStr("author",                  kit.author);
        writeStr("authorUrl",               kit.authorUrl);
        writeStr("license",                 kit.license);
        writeStr("licenseUrl",              kit.licenseUrl);
        writeStr("description",             kit.description);
        writeStr("previewImage",            kit.previewImage);
        writeStr("homepage",                kit.homepage);
        writeStr("minimumPolyphaseVersion", kit.minimumPolyphaseVersion);
        if (!kit.tags.empty())
        {
            w.Key("tags");
            w.StartArray();
            for (const std::string& t : kit.tags)
                w.String(t.c_str(), (rapidjson::SizeType)t.size());
            w.EndArray();
        }
        if (!kit.dependencies.empty())
        {
            w.Key("dependencies");
            w.StartArray();
            for (const LBKitDependency& d : kit.dependencies)
            {
                w.StartObject();
                w.Key("kitId"); w.String(d.kitId.c_str(), (rapidjson::SizeType)d.kitId.size());
                if (!d.version.empty())
                {
                    w.Key("version");
                    w.String(d.version.c_str(), (rapidjson::SizeType)d.version.size());
                }
                w.EndObject();
            }
            w.EndArray();
        }

        w.Key("pieces");
        w.StartArray();
        for (size_t i = 0; i < kit.pieces.size(); ++i)
        {
            const LBPiece& p   = kit.pieces[i];
            const std::string& rel = pieceFileRels[i];
            w.StartObject();
            w.Key("id");   w.String(p.name.c_str(), (rapidjson::SizeType)p.name.size());
            w.Key("file"); w.String(rel.c_str(),    (rapidjson::SizeType)rel.size());
            w.EndObject();
        }
        w.EndArray();
        w.EndObject();

        fs::path kitJson = fs::path(folderPath) / "kit.json";
        std::ofstream out(kitJson, std::ios::binary | std::ios::trunc);
        if (!out.is_open())
        {
            outError = "could not open for write: " + kitJson.string();
            return false;
        }
        out.write(sb.GetString(), (std::streamsize)sb.GetSize());
        if (!out.good())
        {
            outError = "write failed: " + kitJson.string();
            return false;
        }

        // ---- Sweep stale pieces/*.json that weren't part of this save ----
        // (e.g. a piece was renamed; the old file would linger otherwise.)
        if (fs::is_directory(piecesDir, ec))
        {
            for (const auto& e : fs::directory_iterator(piecesDir, ec))
            {
                if (ec) break;
                if (!e.is_regular_file(ec)) continue;
                auto ext = e.path().extension().string();
                if (ext != ".json" && ext != ".JSON") continue;
                std::string fname = e.path().filename().string();
                if (!liveFiles.count(fname))
                {
                    std::error_code rmEc;
                    fs::remove(e.path(), rmEc);
                }
            }
        }
        return true;
    }

    bool LoadFromFolder(const std::string& folderPath, LBKit& outKit, std::string& outError)
    {
        namespace fs = std::filesystem;
        fs::path kitJson = fs::path(folderPath) / "kit.json";
        std::error_code ec;
        if (!fs::is_regular_file(kitJson, ec))
        {
            outError = "folder kit missing kit.json: " + folderPath;
            return false;
        }

        std::ifstream f(kitJson, std::ios::binary);
        if (!f.is_open())
        {
            outError = "could not open: " + kitJson.string();
            return false;
        }
        std::ostringstream ss; ss << f.rdbuf();
        std::string contents = ss.str();

        rapidjson::Document doc;
        doc.Parse(contents.c_str(), contents.size());
        if (doc.HasParseError())
        {
            char buf[256];
            std::snprintf(buf, sizeof(buf),
                          "%s: JSON parse error at offset %zu: %s",
                          kitJson.string().c_str(),
                          (size_t)doc.GetErrorOffset(),
                          rapidjson::GetParseError_En(doc.GetParseError()));
            outError = buf;
            return false;
        }
        if (!doc.IsObject() || !ReadString(doc, "kitName", outKit.name))
        {
            outError = kitJson.string() + ": missing required string 'kitName'";
            return false;
        }

        // Optional metadata mirrors the single-file loader.
        ReadString(doc, "kitVersion",              outKit.kitVersion);
        ReadString(doc, "author",                  outKit.author);
        ReadString(doc, "authorUrl",               outKit.authorUrl);
        ReadString(doc, "license",                 outKit.license);
        ReadString(doc, "licenseUrl",              outKit.licenseUrl);
        ReadString(doc, "description",             outKit.description);
        ReadString(doc, "previewImage",            outKit.previewImage);
        ReadString(doc, "homepage",                outKit.homepage);
        ReadString(doc, "minimumPolyphaseVersion", outKit.minimumPolyphaseVersion);
        ReadStringArray(doc, "tags",               outKit.tags);
        if (doc.HasMember("dependencies") && doc["dependencies"].IsArray())
        {
            for (auto& dv : doc["dependencies"].GetArray())
            {
                LBKitDependency d; std::string err;
                if (ParseDependency(dv, d, err)) outKit.dependencies.push_back(std::move(d));
            }
        }
        if (!ReadString(doc, "kitId", outKit.kitId))
            outKit.kitIdSynthesized = false;

        if (!doc.HasMember("pieces") || !doc["pieces"].IsArray())
        {
            outError = kitJson.string() + ": missing required array 'pieces'";
            return false;
        }

        outKit.pieces.clear();
        outKit.pieces.reserve(doc["pieces"].Size());
        for (auto& entry : doc["pieces"].GetArray())
        {
            if (!entry.IsObject())
            {
                outError = kitJson.string() + ": each pieces[] entry must be an object";
                return false;
            }
            std::string relFile;
            if (!ReadString(entry, "file", relFile))
            {
                outError = kitJson.string() + ": pieces[] entry missing string 'file'";
                return false;
            }
            fs::path pieceJson = fs::path(folderPath) / relFile;
            std::ifstream pf(pieceJson, std::ios::binary);
            if (!pf.is_open())
            {
                outError = "could not open piece file: " + pieceJson.string();
                return false;
            }
            std::ostringstream pss; pss << pf.rdbuf();
            std::string pieceText = pss.str();

            rapidjson::Document pieceDoc;
            pieceDoc.Parse(pieceText.c_str(), pieceText.size());
            if (pieceDoc.HasParseError() || !pieceDoc.IsObject())
            {
                outError = "piece JSON parse failed: " + pieceJson.string();
                return false;
            }
            LBPiece p; std::string err;
            if (!ParsePiece(pieceDoc, p, err))
            {
                outError = pieceJson.string() + ": " + err;
                return false;
            }
            outKit.pieces.push_back(std::move(p));
        }

        if (outKit.kitId.empty())
        {
            outKit.kitId = SynthesizeKitId(outKit);
            outKit.kitIdSynthesized = true;
        }
        return true;
    }
}
