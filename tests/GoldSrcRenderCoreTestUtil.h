#pragma once

#include "GoldSrcRenderBSP.h"

#include <algorithm>
#include <cstring>
#include <vector>

//Helpers of the tests that build and compare BSP worlds from the generated test files.
namespace GoldSrcRenderTest
{
using namespace GoldSrcRender;

inline BSPAssetView TestBSPAsset(const std::vector<std::uint8_t>& file, const std::vector<TextureBinding>& bindings)
{
    BSPAssetView asset;
    asset.pFile        = file.data();
    asset.fileBytes    = static_cast<std::uint32_t>(file.size());
    asset.pTextures    = bindings.data();
    asset.textureCount = static_cast<std::uint32_t>(bindings.size());

    return asset;
}

//Element-wise equality of plain records, compared byte for byte.
template <typename T>
inline bool SameRecords(const std::vector<T>& a, const std::vector<T>& b)
{
    return a.size() == b.size() && (a.empty() || std::memcmp(a.data(), b.data(), sizeof(T) * a.size()) == 0);
}

inline bool SameBSPWorld(const BSPWorld& a, const BSPWorld& b)
{
    if (!SameRecords(a.positions, b.positions) || !SameRecords(a.normals, b.normals) || !SameRecords(a.sTangents, b.sTangents) || !SameRecords(a.tTangents, b.tTangents))
        return false;

    if (!SameRecords(a.texcoords, b.texcoords) || !SameRecords(a.lightmapTexcoords, b.lightmapTexcoords) || !SameRecords(a.indices, b.indices))
        return false;

    if (!SameRecords(a.surfaces, b.surfaces) || !SameRecords(a.textureGroups, b.textureGroups) || a.lightmapPages != b.lightmapPages || a.lightmaps != b.lightmaps)
        return false;

    if (!SameRecords(a.textureFrames, b.textureFrames))
        return false;

    if (a.usedStyleLayers != b.usedStyleLayers || a.bFullbright != b.bFullbright || std::memcmp(&a.skipped, &b.skipped, sizeof(a.skipped)) != 0)
        return false;

    //Water groups have padding, so they are compared field by field.
    const auto sameWaterGroup = [](const BSPWaterGroup& x, const BSPWaterGroup& y) {
        return x.slot == y.slot && x.binding == y.binding && x.normal.x == y.normal.x && x.normal.y == y.normal.y && x.normal.z == y.normal.z && x.dist == y.dist && x.bBack == y.bBack && x.bHorizontal == y.bHorizontal && x.firstSurface == y.firstSurface && x.surfaceCount == y.surfaceCount;
    };

    if (!SameRecords(a.waterSurfaces, b.waterSurfaces) || !std::equal(a.waterGroups.begin(), a.waterGroups.end(), b.waterGroups.begin(), b.waterGroups.end(), sameWaterGroup))
        return false;

    if (a.markWaterSurfaces != b.markWaterSurfaces || a.waterSurfaceNode != b.waterSurfaceNode)
        return false;

    return a.rootNode == b.rootNode && SameRecords(a.nodes, b.nodes) && SameRecords(a.leaves, b.leaves) && a.markSurfaces == b.markSurfaces && a.surfaceNode == b.surfaceNode && a.visibleLeafCount == b.visibleLeafCount && a.visibility == b.visibility;
}

//A rectangle on z = 0 from (left, bottom) to (right, top) with its corners in the order of a face's edges.
inline std::vector<Vector3> MakeWaterRectangle(float left, float bottom, float right, float top)
{
    return {{left, bottom, 0}, {right, bottom, 0}, {right, top, 0}, {left, top, 0}};
}

inline std::vector<Vector3> MakeWaterRectangle(float width, float height)
{
    return MakeWaterRectangle(0, 0, width, height);
}
} // namespace GoldSrcRenderTest
