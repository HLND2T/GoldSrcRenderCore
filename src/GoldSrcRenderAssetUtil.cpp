#include "GoldSrcRenderAssetUtil.h"
#include "GoldSrcRenderBSP.h"
#include "GoldSrcRenderMDL.h"

#include <cmath>

namespace GoldSrcRender
{
bool ValidVector(const Vector3& vector)
{
    return std::isfinite(vector.x) && std::isfinite(vector.y) && std::isfinite(vector.z);
}

//A borrowed array may be null only when it is empty.
bool ValidArray(const void* pData, std::uint32_t count)
{
    return !count || pData;
}

//Whether szString ends within maxLength bytes, reading no byte past the limit.
bool IsTerminatedWithin(const char* szString, std::size_t maxLength)
{
    for (std::size_t i = 0; i <= maxLength; ++i)
    {
        if (!szString[i])
            return true;
    }

    return false;
}

bool ValidIdentifier(const char* szIdentifier)
{
    return !szIdentifier || IsTerminatedWithin(szIdentifier, MaxIdentifierLength);
}

//Returns true if [offset, offset + count * stride) fits in a buffer of size bytes.
bool ValidRange(std::size_t size, std::uint32_t offset, std::uint32_t count, std::uint32_t stride)
{
    return offset <= size && stride != 0 && count <= (size - offset) / stride;
}

//Little-endian read; the caller must ensure that 4 bytes are readable at pBytes + offset.
std::uint32_t ReadUInt32(const std::uint8_t* pBytes, std::size_t offset)
{
    return std::uint32_t(pBytes[offset]) |
        (std::uint32_t(pBytes[offset + 1]) << 8) |
        (std::uint32_t(pBytes[offset + 2]) << 16) |
        (std::uint32_t(pBytes[offset + 3]) << 24);
}

std::uint32_t ReadBSPLumpOffset(const std::uint8_t* pBytes, std::uint32_t lump)
{
    return ReadUInt32(pBytes, BSPLumpTableOffset + lump * BSPLumpEntryBytes);
}

std::uint32_t ReadBSPLumpLength(const std::uint8_t* pBytes, std::uint32_t lump)
{
    return ReadUInt32(pBytes, BSPLumpTableOffset + lump * BSPLumpEntryBytes + BSPLumpLengthOffset);
}

std::uint32_t CountBSPSubmodels(const std::uint8_t* pBytes, std::uint32_t size)
{
    if (!pBytes || size < BSPHeaderBytes)
        return 0;

    return ReadBSPLumpLength(pBytes, BSPModelLump) / BSPModelBytes;
}

bool ReadBSPSubmodelFaces(const std::uint8_t* pBytes, std::uint32_t size, std::uint32_t submodel, std::uint32_t& firstFace, std::uint32_t& faceCount)
{
    if (submodel >= CountBSPSubmodels(pBytes, size))
        return false;

    const auto record = std::uint64_t(ReadBSPLumpOffset(pBytes, BSPModelLump)) + std::uint64_t(submodel) * BSPModelBytes;

    if (record + BSPModelBytes > size)
        return false;

    const auto first = static_cast<std::int32_t>(ReadUInt32(pBytes, static_cast<std::size_t>(record) + BSPModelFirstFaceOffset));
    const auto count = static_cast<std::int32_t>(ReadUInt32(pBytes, static_cast<std::size_t>(record) + BSPModelFaceCountOffset));

    if (first < 0 || count < 0)
        return false;

    firstFace = static_cast<std::uint32_t>(first);
    faceCount = static_cast<std::uint32_t>(count);

    return true;
}

ErrorCode ValidateBSPAsset(const BSPAssetView& asset)
{
    if (!ValidIdentifier(asset.szIdentifier))
        return ErrorCode::InvalidData;

    if (asset.textureCount > MaxTableEntries || asset.detailTextureCount > MaxTableEntries)
        return ErrorCode::InvalidData;

    if (!ValidArray(asset.pDetailTextures, asset.detailTextureCount))
        return ErrorCode::InvalidData;

    for (std::uint32_t i = 0; i < asset.detailTextureCount; ++i)
    {
        const auto& binding = asset.pDetailTextures[i];

        if (!std::isfinite(binding.scaleS) || binding.scaleS <= 0 || !std::isfinite(binding.scaleT) || binding.scaleT <= 0)
            return ErrorCode::InvalidData;
    }

    return ValidateBSPFile(asset.pFile, asset.fileBytes, asset.layout, asset.pTextures, asset.textureCount, asset.pDetailTextures, asset.detailTextureCount);
}

ErrorCode ValidateMDLAsset(const MDLAssetView& asset)
{
    if (!ValidIdentifier(asset.szIdentifier))
        return ErrorCode::InvalidData;

    if (asset.textureCount > MaxTableEntries || !ValidArray(asset.pTextures, asset.textureCount))
        return ErrorCode::InvalidData;

    return ValidateMDLAssetFiles(asset);
}
} // namespace GoldSrcRender
