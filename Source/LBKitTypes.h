/**
 * @file LBKitTypes.h
 * @brief Shared kit data types — moved from modular's ModularKit.h.
 *
 * Owned by core. Siblings access these through the C ABI's snapshot
 * structs (LBKitInfo / LBPieceInfo / LBSocketInfo) — they should not
 * include this header directly.
 *
 * Naming intentionally drops the "Modular" prefix: these are generic
 * kit concepts shared by modular, grid, and any future sibling that
 * consumes the kit registry.
 */

#pragma once

#include <string>
#include <vector>

struct LBVec3;   // from LevelBuilderCoreAPI.h
struct LBQuat;

struct LBSocket
{
    std::string              name;
    std::string              type;
    float                    localPos[3]      = {0, 0, 0};
    float                    localEulerDeg[3] = {0, 0, 0};   // YXZ
    std::vector<std::string> compatibleTypes;                 // empty = same-type only
};

struct LBPiece
{
    std::string             name;          // "Wall_01"
    std::string             assetName;     // "SM_Dungeon_Wall_01"
    std::string             category;      // "Walls"
    std::string             iconPath;      // optional; relative to project root
    float                   size[3] = {1, 1, 1};
    std::vector<LBSocket>   sockets;
};

// Single dependency entry inside an LBKit.dependencies vector.
// Both fields are optional in the JSON; `version` empty means "any version".
struct LBKitDependency
{
    std::string kitId;
    std::string version;       // free-text semver constraint, e.g. ">=1.0.0"
};

struct LBKit
{
    std::string             name;
    std::vector<LBPiece>    pieces;

    // Absolute path of the JSON file this kit was loaded from. Empty for
    // in-memory kits (built-in starter, Kit_CreateKit, …).
    std::string             sourceFile;

    // ----- Phase S1 sharing metadata (see KitSharing.md §3) ----------
    // All optional; loader synthesizes `kitId` from a content hash when
    // missing so legacy kits acquire a stable identity on first load.
    std::string             kitId;                       // reverse-DNS preferred
    std::string             kitVersion;                  // semver
    std::string             author;
    std::string             authorUrl;
    std::string             license;                     // SPDX id
    std::string             licenseUrl;
    std::string             description;
    std::string             previewImage;                // path relative to kit root
    std::string             homepage;
    std::string             minimumPolyphaseVersion;
    std::vector<std::string>      tags;
    std::vector<LBKitDependency>  dependencies;

    // Tracks whether `kitId` was synthesized (true) or loaded from JSON
    // (false). The Save path writes a synthesized kitId out as an explicit
    // field so subsequent loads don't recompute — but we want to know
    // the first-load case so the UI can surface "this kit needs a real id".
    bool                    kitIdSynthesized = false;

    const LBPiece* FindPiece(const std::string& displayName) const;
    const LBPiece* FindPieceByAsset(const std::string& assetName) const;
    LBPiece*       FindPieceByAssetMutable(const std::string& assetName);
};

// -----------------------------------------------------------------------------
// Socket compatibility + math helpers (moved from ModularKit.cpp).
// -----------------------------------------------------------------------------

bool LBSocketsAreCompatible(const LBSocket& a, const LBSocket& b);

void LBEulerDegToQuat(const float eulerDeg[3], LBQuat& outQuat);

void LBComposeSocketWorld(
    const LBVec3& pieceWorldPos,
    const LBQuat& pieceWorldRot,
    const LBSocket& socket,
    LBVec3& outSocketWorldPos,
    LBQuat& outSocketWorldRot);
