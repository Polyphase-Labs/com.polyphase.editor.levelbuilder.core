#include "LBKitZip.h"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>

namespace
{
    // ---- CRC32 ----

    uint32_t Crc32(const uint8_t* data, size_t len)
    {
        // Reflected polynomial 0xEDB88320. Table built lazily once.
        static uint32_t sTable[256];
        static bool sTableInit = false;
        if (!sTableInit)
        {
            for (uint32_t i = 0; i < 256; ++i)
            {
                uint32_t c = i;
                for (int k = 0; k < 8; ++k)
                    c = (c & 1u) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
                sTable[i] = c;
            }
            sTableInit = true;
        }

        uint32_t c = 0xFFFFFFFFu;
        for (size_t i = 0; i < len; ++i)
            c = sTable[(c ^ data[i]) & 0xFFu] ^ (c >> 8);
        return c ^ 0xFFFFFFFFu;
    }

    // ---- Little-endian writers ----

    void WriteLE16(std::vector<uint8_t>& buf, uint16_t v)
    {
        buf.push_back((uint8_t)(v & 0xFFu));
        buf.push_back((uint8_t)((v >> 8) & 0xFFu));
    }
    void WriteLE32(std::vector<uint8_t>& buf, uint32_t v)
    {
        buf.push_back((uint8_t)(v & 0xFFu));
        buf.push_back((uint8_t)((v >> 8) & 0xFFu));
        buf.push_back((uint8_t)((v >> 16) & 0xFFu));
        buf.push_back((uint8_t)((v >> 24) & 0xFFu));
    }

    // ---- Little-endian readers (over a flat byte buffer) ----

    uint16_t ReadLE16(const uint8_t* p) { return (uint16_t)(p[0] | (p[1] << 8)); }
    uint32_t ReadLE32(const uint8_t* p)
    {
        return (uint32_t)p[0]
             | ((uint32_t)p[1] << 8)
             | ((uint32_t)p[2] << 16)
             | ((uint32_t)p[3] << 24);
    }

    // ---- PKZIP signatures ----

    constexpr uint32_t kSigLocal   = 0x04034b50u;   // local file header
    constexpr uint32_t kSigCentral = 0x02014b50u;   // central directory entry
    constexpr uint32_t kSigEOCD    = 0x06054b50u;   // end of central directory record

    // Read a whole file into a vector. Returns false on open / read fail.
    bool ReadFileAll(const std::string& path, std::vector<uint8_t>& out)
    {
        std::ifstream f(path, std::ios::binary | std::ios::ate);
        if (!f) return false;
        const std::streamsize sz = f.tellg();
        if (sz < 0) return false;
        out.resize((size_t)sz);
        f.seekg(0, std::ios::beg);
        if (sz == 0) return true;
        f.read(reinterpret_cast<char*>(out.data()), sz);
        return (bool)f;
    }

    // Search for the End-of-Central-Directory record by scanning the
    // last 64K of the file (max EOCD comment is 0xFFFF bytes).
    bool FindEOCD(const std::vector<uint8_t>& zip, size_t& outEocdOffset)
    {
        if (zip.size() < 22) return false;
        const size_t maxScan = zip.size() < (22 + 0xFFFFu)
                               ? zip.size()
                               : (22 + 0xFFFFu);
        for (size_t i = zip.size() - 22; i + 22 <= zip.size(); )
        {
            if (ReadLE32(&zip[i]) == kSigEOCD)
            {
                outEocdOffset = i;
                return true;
            }
            if (i == 0) break;
            if (zip.size() - i >= maxScan) break;
            --i;
        }
        return false;
    }

    // Normalize a slash-style path and reject zip-slip patterns (.. or
    // absolute paths). Returns empty string on rejection.
    std::string SafeRelativePath(const std::string& entryName)
    {
        if (entryName.empty()) return {};
        // Reject absolute paths (Windows drive letters and POSIX root).
        if (entryName.size() >= 2 && entryName[1] == ':') return {};
        if (entryName[0] == '/' || entryName[0] == '\\') return {};
        // Reject any `..` segment.
        std::string out;
        out.reserve(entryName.size());
        size_t i = 0;
        while (i < entryName.size())
        {
            size_t j = i;
            while (j < entryName.size() && entryName[j] != '/' && entryName[j] != '\\')
                ++j;
            const std::string seg = entryName.substr(i, j - i);
            if (seg == "..") return {};
            if (!seg.empty() && seg != ".")
            {
                if (!out.empty()) out += '/';
                out += seg;
            }
            i = (j < entryName.size()) ? j + 1 : j;
        }
        return out;
    }
}

namespace LBKitZip
{
    // ---- Writer ----

    bool Write(const std::string& dstPath,
               const std::vector<Entry>& entries,
               std::string& outError)
    {
        std::ofstream out(dstPath, std::ios::binary | std::ios::trunc);
        if (!out.is_open())
        {
            outError = "Zip write: could not open file: " + dstPath;
            return false;
        }

        // Build local-file-header + data for each entry, remembering the
        // offset at which each entry started for the central directory.
        struct CDEntry { uint32_t localOffset; uint32_t crc; uint32_t size; std::string name; };
        std::vector<CDEntry> cd;
        cd.reserve(entries.size());

        uint32_t offset = 0;
        for (const Entry& e : entries)
        {
            if (e.name.empty())
            {
                outError = "Zip write: entry with empty name";
                return false;
            }
            const uint32_t crc  = Crc32(e.data.data(), e.data.size());
            const uint32_t size = (uint32_t)e.data.size();

            std::vector<uint8_t> hdr;
            hdr.reserve(30 + e.name.size());
            WriteLE32(hdr, kSigLocal);
            WriteLE16(hdr, 20);             // version needed
            WriteLE16(hdr, 0);              // general purpose bit flag
            WriteLE16(hdr, 0);              // compression method (0 = stored)
            WriteLE16(hdr, 0);              // mod time (zeroed — we don't track per-entry timestamps)
            WriteLE16(hdr, 0);              // mod date
            WriteLE32(hdr, crc);
            WriteLE32(hdr, size);           // compressed size = size (stored)
            WriteLE32(hdr, size);           // uncompressed size
            WriteLE16(hdr, (uint16_t)e.name.size());
            WriteLE16(hdr, 0);              // extra-field length
            for (char c : e.name) hdr.push_back((uint8_t)c);

            out.write(reinterpret_cast<const char*>(hdr.data()), (std::streamsize)hdr.size());
            if (size > 0)
                out.write(reinterpret_cast<const char*>(e.data.data()), (std::streamsize)size);
            if (!out.good())
            {
                outError = "Zip write: stream error writing " + e.name;
                return false;
            }

            CDEntry ce;
            ce.localOffset = offset;
            ce.crc         = crc;
            ce.size        = size;
            ce.name        = e.name;
            cd.push_back(std::move(ce));

            offset += (uint32_t)hdr.size() + size;
        }

        // Central directory
        const uint32_t cdOffset = offset;
        uint32_t cdSize = 0;
        for (const CDEntry& ce : cd)
        {
            std::vector<uint8_t> hdr;
            hdr.reserve(46 + ce.name.size());
            WriteLE32(hdr, kSigCentral);
            WriteLE16(hdr, 20);             // version made by
            WriteLE16(hdr, 20);             // version needed
            WriteLE16(hdr, 0);              // bit flag
            WriteLE16(hdr, 0);              // compression
            WriteLE16(hdr, 0);              // mod time
            WriteLE16(hdr, 0);              // mod date
            WriteLE32(hdr, ce.crc);
            WriteLE32(hdr, ce.size);        // compressed = size
            WriteLE32(hdr, ce.size);        // uncompressed = size
            WriteLE16(hdr, (uint16_t)ce.name.size());
            WriteLE16(hdr, 0);              // extra
            WriteLE16(hdr, 0);              // comment
            WriteLE16(hdr, 0);              // disk number
            WriteLE16(hdr, 0);              // internal attrs
            WriteLE32(hdr, 0);              // external attrs
            WriteLE32(hdr, ce.localOffset);
            for (char c : ce.name) hdr.push_back((uint8_t)c);

            out.write(reinterpret_cast<const char*>(hdr.data()), (std::streamsize)hdr.size());
            cdSize += (uint32_t)hdr.size();
        }

        // End of central directory record
        std::vector<uint8_t> eocd;
        eocd.reserve(22);
        WriteLE32(eocd, kSigEOCD);
        WriteLE16(eocd, 0);                       // disk number
        WriteLE16(eocd, 0);                       // disk with CD
        WriteLE16(eocd, (uint16_t)cd.size());     // CD records on this disk
        WriteLE16(eocd, (uint16_t)cd.size());     // total CD records
        WriteLE32(eocd, cdSize);
        WriteLE32(eocd, cdOffset);
        WriteLE16(eocd, 0);                       // comment length
        out.write(reinterpret_cast<const char*>(eocd.data()), (std::streamsize)eocd.size());

        if (!out.good())
        {
            outError = "Zip write: stream error finalizing " + dstPath;
            return false;
        }
        return true;
    }

    // ---- Reader ----

    // Internal: walk the central directory; for each entry, invoke `cb`
    // with (entryName, localOffset, uncompressedSize). cb returns true to
    // continue, false to stop early. Returns false on parse error.
    template<typename CB>
    bool WalkCentralDirectory(const std::vector<uint8_t>& zip,
                              std::string& outError,
                              CB cb)
    {
        size_t eocd = 0;
        if (!FindEOCD(zip, eocd))
        {
            outError = "Zip read: end-of-central-directory record not found";
            return false;
        }
        const uint16_t numEntries = ReadLE16(&zip[eocd + 10]);
        const uint32_t cdOffset   = ReadLE32(&zip[eocd + 16]);

        size_t p = cdOffset;
        for (uint16_t i = 0; i < numEntries; ++i)
        {
            if (p + 46 > zip.size() || ReadLE32(&zip[p]) != kSigCentral)
            {
                outError = "Zip read: malformed central directory entry";
                return false;
            }
            const uint32_t uncompressedSize = ReadLE32(&zip[p + 24]);
            const uint16_t nameLen          = ReadLE16(&zip[p + 28]);
            const uint16_t extraLen         = ReadLE16(&zip[p + 30]);
            const uint16_t commentLen       = ReadLE16(&zip[p + 32]);
            const uint32_t localOffset      = ReadLE32(&zip[p + 42]);
            if (p + 46 + nameLen > zip.size())
            {
                outError = "Zip read: name overruns CD entry";
                return false;
            }
            std::string name((const char*)&zip[p + 46], nameLen);
            if (!cb(name, localOffset, uncompressedSize)) return true;
            p += 46u + nameLen + extraLen + commentLen;
        }
        return true;
    }

    // Internal: given a local file header offset and expected uncompressed
    // size, copy the entry bytes into `out`. Assumes stored (uncompressed).
    bool ReadEntryAtOffset(const std::vector<uint8_t>& zip,
                           uint32_t localOffset, uint32_t uncompressedSize,
                           std::vector<uint8_t>& out, std::string& outError)
    {
        if (localOffset + 30 > zip.size() || ReadLE32(&zip[localOffset]) != kSigLocal)
        {
            outError = "Zip read: malformed local file header";
            return false;
        }
        const uint16_t method  = ReadLE16(&zip[localOffset + 8]);
        const uint16_t nameLen = ReadLE16(&zip[localOffset + 26]);
        const uint16_t extraLen= ReadLE16(&zip[localOffset + 28]);
        if (method != 0)
        {
            outError = "Zip read: compressed entries unsupported (got method "
                     + std::to_string(method) + ")";
            return false;
        }
        const size_t dataStart = (size_t)localOffset + 30 + nameLen + extraLen;
        if (dataStart + uncompressedSize > zip.size())
        {
            outError = "Zip read: data overruns archive";
            return false;
        }
        out.assign(zip.begin() + dataStart,
                   zip.begin() + dataStart + uncompressedSize);
        return true;
    }

    bool List(const std::string& srcPath,
              std::vector<std::string>& outNames,
              std::string& outError)
    {
        std::vector<uint8_t> zip;
        if (!ReadFileAll(srcPath, zip))
        {
            outError = "Zip read: could not read file: " + srcPath;
            return false;
        }
        outNames.clear();
        return WalkCentralDirectory(zip, outError,
            [&](const std::string& name, uint32_t /*lo*/, uint32_t /*sz*/) {
                outNames.push_back(name);
                return true;
            });
    }

    bool ReadEntry(const std::string& srcPath,
                   const std::string& entryName,
                   std::vector<uint8_t>& outBytes,
                   std::string& outError)
    {
        std::vector<uint8_t> zip;
        if (!ReadFileAll(srcPath, zip))
        {
            outError = "Zip read: could not read file: " + srcPath;
            return false;
        }
        bool found = false;
        bool ok = WalkCentralDirectory(zip, outError,
            [&](const std::string& name, uint32_t lo, uint32_t sz) {
                if (name == entryName)
                {
                    found = ReadEntryAtOffset(zip, lo, sz, outBytes, outError);
                    return false;     // stop walking
                }
                return true;
            });
        if (!ok) return false;
        if (!found)
        {
            outError = "Zip read: entry not found: " + entryName;
            return false;
        }
        return true;
    }

    bool ExtractAll(const std::string& srcPath,
                    const std::string& dstDir,
                    std::string& outError)
    {
        namespace fs = std::filesystem;
        std::vector<uint8_t> zip;
        if (!ReadFileAll(srcPath, zip))
        {
            outError = "Zip read: could not read file: " + srcPath;
            return false;
        }
        std::error_code ec;
        fs::create_directories(dstDir, ec);

        return WalkCentralDirectory(zip, outError,
            [&](const std::string& name, uint32_t lo, uint32_t sz) {
                // Skip directory entries (PKZIP convention: trailing slash).
                if (!name.empty() && (name.back() == '/' || name.back() == '\\'))
                    return true;

                const std::string safe = SafeRelativePath(name);
                if (safe.empty())
                {
                    outError = "Zip extract: refused path (zip-slip / absolute): " + name;
                    return false;
                }

                std::vector<uint8_t> bytes;
                if (!ReadEntryAtOffset(zip, lo, sz, bytes, outError))
                    return false;

                fs::path outPath = fs::path(dstDir) / safe;
                fs::create_directories(outPath.parent_path(), ec);
                std::ofstream out(outPath, std::ios::binary | std::ios::trunc);
                if (!out.is_open())
                {
                    outError = "Zip extract: could not open " + outPath.string();
                    return false;
                }
                if (!bytes.empty())
                    out.write(reinterpret_cast<const char*>(bytes.data()),
                              (std::streamsize)bytes.size());
                if (!out.good())
                {
                    outError = "Zip extract: write failed for " + outPath.string();
                    return false;
                }
                return true;
            });
    }
}
