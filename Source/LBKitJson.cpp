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
#include <fstream>
#include <sstream>

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
}
