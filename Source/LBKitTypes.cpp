#include "LBKitTypes.h"

#include "LevelBuilderCoreAPI.h"

#include <cmath>

// -----------------------------------------------------------------------------
// LBKit lookups
// -----------------------------------------------------------------------------

const LBPiece* LBKit::FindPiece(const std::string& displayName) const
{
    for (const auto& p : pieces)
        if (p.name == displayName) return &p;
    return nullptr;
}

const LBPiece* LBKit::FindPieceByAsset(const std::string& assetName) const
{
    for (const auto& p : pieces)
        if (p.assetName == assetName) return &p;
    return nullptr;
}

LBPiece* LBKit::FindPieceByAssetMutable(const std::string& assetName)
{
    for (auto& p : pieces)
        if (p.assetName == assetName) return &p;
    return nullptr;
}

// -----------------------------------------------------------------------------
// Socket compatibility
// -----------------------------------------------------------------------------

bool LBSocketsAreCompatible(const LBSocket& a, const LBSocket& b)
{
    auto matches = [](const std::vector<std::string>& list, const std::string& t)
    {
        for (const auto& s : list) if (s == t) return true;
        return false;
    };

    if (a.compatibleTypes.empty() && b.compatibleTypes.empty())
        return a.type == b.type;

    if (!a.compatibleTypes.empty() && !matches(a.compatibleTypes, b.type))
        return false;
    if (!b.compatibleTypes.empty() && !matches(b.compatibleTypes, a.type))
        return false;
    return true;
}

// -----------------------------------------------------------------------------
// Math helpers
// -----------------------------------------------------------------------------

void LBEulerDegToQuat(const float eulerDeg[3], LBQuat& out)
{
    // YXZ intrinsic (yaw, pitch, roll) — matches the artist-facing schema
    // documented in Artists/GettingStarted.md.
    const float DEG2RAD = 3.14159265358979323846f / 180.0f;
    float yaw   = eulerDeg[1] * DEG2RAD * 0.5f;
    float pitch = eulerDeg[0] * DEG2RAD * 0.5f;
    float roll  = eulerDeg[2] * DEG2RAD * 0.5f;
    float cy = std::cos(yaw),   sy = std::sin(yaw);
    float cp = std::cos(pitch), sp = std::sin(pitch);
    float cr = std::cos(roll),  sr = std::sin(roll);

    out.w = cy * cp * cr + sy * sp * sr;
    out.x = cy * sp * cr + sy * cp * sr;
    out.y = sy * cp * cr - cy * sp * sr;
    out.z = cy * cp * sr - sy * sp * cr;
}

static LBQuat Quat_Mul(const LBQuat& a, const LBQuat& b)
{
    LBQuat r;
    r.w = a.w*b.w - a.x*b.x - a.y*b.y - a.z*b.z;
    r.x = a.w*b.x + a.x*b.w + a.y*b.z - a.z*b.y;
    r.y = a.w*b.y - a.x*b.z + a.y*b.w + a.z*b.x;
    r.z = a.w*b.z + a.x*b.y - a.y*b.x + a.z*b.w;
    return r;
}

static LBVec3 Quat_RotateVec(const LBQuat& q, const LBVec3& v)
{
    float xx = q.x*q.x, yy = q.y*q.y, zz = q.z*q.z;
    float xy = q.x*q.y, xz = q.x*q.z, yz = q.y*q.z;
    float wx = q.w*q.x, wy = q.w*q.y, wz = q.w*q.z;
    LBVec3 r;
    r.x = v.x*(1 - 2*(yy+zz)) + v.y*(2*(xy - wz))     + v.z*(2*(xz + wy));
    r.y = v.x*(2*(xy + wz))   + v.y*(1 - 2*(xx+zz))   + v.z*(2*(yz - wx));
    r.z = v.x*(2*(xz - wy))   + v.y*(2*(yz + wx))     + v.z*(1 - 2*(xx+yy));
    return r;
}

void LBComposeSocketWorld(
    const LBVec3& pieceWorldPos,
    const LBQuat& pieceWorldRot,
    const LBSocket& socket,
    LBVec3& outSocketWorldPos,
    LBQuat& outSocketWorldRot)
{
    LBVec3 lp{socket.localPos[0], socket.localPos[1], socket.localPos[2]};
    LBVec3 wp = Quat_RotateVec(pieceWorldRot, lp);
    outSocketWorldPos.x = pieceWorldPos.x + wp.x;
    outSocketWorldPos.y = pieceWorldPos.y + wp.y;
    outSocketWorldPos.z = pieceWorldPos.z + wp.z;

    LBQuat localRot;
    LBEulerDegToQuat(socket.localEulerDeg, localRot);
    outSocketWorldRot = Quat_Mul(pieceWorldRot, localRot);
}
