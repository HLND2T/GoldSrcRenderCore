#include "GoldSrcRenderMDL.h"
#include "GoldSrcRenderAssetUtil.h"

#include <algorithm>
#include <bitset>
#include <cctype>
#include <cmath>
#include <cstring>

namespace GoldSrcRender
{
//Header fields (studiohdr_t).
constexpr std::uint32_t MDLFlagsOffset            = 136;
constexpr std::uint32_t MDLBoneCountOffset        = 140;
constexpr std::uint32_t MDLBoneIndexOffset        = 144;
constexpr std::uint32_t MDLTextureCountOffset     = 180;
constexpr std::uint32_t MDLTextureIndexOffset     = 184;
constexpr std::uint32_t MDLTextureDataIndexOffset = 188;
constexpr std::uint32_t MDLSkinRefCountOffset     = 192;
constexpr std::uint32_t MDLSkinFamilyCountOffset  = 196;
constexpr std::uint32_t MDLSkinIndexOffset        = 200;
constexpr std::uint32_t MDLBodyPartCountOffset    = 204;
constexpr std::uint32_t MDLBodyPartIndexOffset    = 208;

//Record sizes and field offsets (mstudiobone_t, mstudiobodyparts_t, mstudiotexture_t, mstudiomodel_t, mstudiomesh_t).
constexpr std::uint32_t MDLBoneBytes                = 112;
constexpr std::uint32_t MDLBoneNameBytes            = 32;
constexpr std::uint32_t MDLBoneParentOffset         = 32;
constexpr std::uint32_t MDLBoneFlagsOffset          = 36;
constexpr std::uint32_t MDLBodyPartBytes            = 76;
constexpr std::uint32_t MDLBodyPartNameBytes        = 64;
constexpr std::uint32_t MDLBodyPartModelCountOffset = 64;
constexpr std::uint32_t MDLBodyPartModelIndexOffset = 72;
constexpr std::uint32_t MDLTextureBytes             = 80;
constexpr std::uint32_t MDLTextureNameBytes         = 64;
constexpr std::uint32_t MDLTextureFlagsOffset       = 64;
constexpr std::uint32_t MDLTextureWidthOffset       = 68;
constexpr std::uint32_t MDLTextureHeightOffset      = 72;
constexpr std::uint32_t MDLTextureIndexFieldOffset  = 76;
constexpr std::uint32_t MDLSubmodelBytes            = 112;
constexpr std::uint32_t MDLSubmodelNameBytes        = 64;
constexpr std::uint32_t MDLSubmodelMeshCountOffset  = 72;
constexpr std::uint32_t MDLSubmodelMeshIndexOffset  = 76;
constexpr std::uint32_t MDLSubmodelVertCountOffset  = 80;
constexpr std::uint32_t MDLSubmodelVertInfoOffset   = 84;
constexpr std::uint32_t MDLSubmodelVertIndexOffset  = 88;
constexpr std::uint32_t MDLSubmodelNormCountOffset  = 92;
constexpr std::uint32_t MDLSubmodelNormInfoOffset   = 96;
constexpr std::uint32_t MDLSubmodelNormIndexOffset  = 100;
constexpr std::uint32_t MDLMeshBytes                = 20;
constexpr std::uint32_t MDLMeshTriangleIndexOffset  = 4;
constexpr std::uint32_t MDLMeshSkinRefOffset        = 8;
constexpr std::uint32_t MDLVectorBytes              = 12;
constexpr std::uint32_t MDLBoneInfoBytes            = 1;
constexpr std::uint32_t MDLSkinRefBytes             = 2;
constexpr std::uint32_t MDLPaletteBytes             = 768;
constexpr std::int32_t  MDLNoParentBone             = -1;

//A triangle command starts with its point count, negative for a fan, zero ending the mesh; every point holds its vertex
//index, normal index and texel coordinates.
constexpr std::uint32_t MDLCommandCountBytes    = 2;
constexpr std::uint32_t MDLCommandPointBytes    = 8;
constexpr std::uint32_t MDLPointVertexOffset    = 0;
constexpr std::uint32_t MDLPointNormalOffset    = 2;
constexpr std::uint32_t MDLPointTexcoordSOffset = 4;
constexpr std::uint32_t MDLPointTexcoordTOffset = 6;

//Packed texture names (see MDLPackedTexture): the prefix, and the parts of up to four images after it, each an
//underscore, an image letter and a digit, the first one starting at the prefix's underscore.
constexpr const char*   MDLPackedPrefix          = "Packed_";
constexpr std::size_t   MDLPackedPrefixLength    = 7;
constexpr std::size_t   MDLPackedFirstPartOffset = 6;
constexpr std::size_t   MDLPackedPartLength      = 3;
constexpr std::uint32_t MDLPackedMaxParts        = 4;

static std::int16_t ReadInt16(const std::uint8_t* pBytes, std::size_t offset)
{
    return static_cast<std::int16_t>(pBytes[offset] | pBytes[offset + 1] << 8);
}

static std::int32_t ReadInt32(const std::uint8_t* pBytes, std::size_t offset)
{
    return static_cast<std::int32_t>(ReadUInt32(pBytes, offset));
}

static float ReadFloat(const std::uint8_t* pBytes, std::size_t offset)
{
    const auto bits = ReadUInt32(pBytes, offset);

    float value;
    std::memcpy(&value, &bits, sizeof(value));

    return value;
}

static Vector3 ReadVector(const std::uint8_t* pBytes, std::size_t offset)
{
    return {ReadFloat(pBytes, offset), ReadFloat(pBytes, offset + sizeof(float)), ReadFloat(pBytes, offset + 2 * sizeof(float))};
}

//A name field of nameBytes bytes, null terminated within it.
static bool ValidName(const std::uint8_t* pBytes, std::size_t offset, std::uint32_t nameBytes)
{
    return std::memchr(pBytes + offset, 0, nameBytes) != nullptr;
}

//Up to nameBytes bytes, to the first null.
static std::string ReadName(const std::uint8_t* pBytes, std::size_t offset, std::uint32_t nameBytes)
{
    const auto pName = reinterpret_cast<const char*>(pBytes + offset);

    return std::string(pName, std::find(pName, pName + nameBytes, '\0'));
}

//A table of count records of stride bytes at offset, read as signed fields, within size bytes. A negative count or
//offset, or a count above maxCount, is invalid.
static bool ValidTable(std::size_t size, std::int32_t offset, std::int32_t count, std::uint32_t stride, std::uint32_t maxCount)
{
    if (offset < 0 || count < 0 || static_cast<std::uint32_t>(count) > maxCount)
        return false;

    return ValidRange(size, static_cast<std::uint32_t>(offset), static_cast<std::uint32_t>(count), stride);
}

//The header of an MDL file: its identifier and version, a length equal to the file size within the asset limit.
static bool ValidMDLHeader(const std::uint8_t* pFile, std::uint32_t fileBytes)
{
    if (!pFile || fileBytes < MDLHeaderBytes || fileBytes > MaxAssetBytes)
        return false;

    if (ReadUInt32(pFile, MDLIdentifierOffset) != MDLIdentifier || ReadUInt32(pFile, MDLVersionOffset) != MDLVersion)
        return false;

    return ReadUInt32(pFile, MDLLengthOffset) == fileBytes;
}

//Bytes of the part of a file GoldSrc keeps in memory, where every table but the texture pixels lies: up to the texture
//data when the file has a texture table, otherwise the whole file. Zero when the texture data starts outside the file.
static std::uint32_t GetMDLStructureBytes(const std::uint8_t* pFile, std::uint32_t fileBytes)
{
    if (!ReadInt32(pFile, MDLTextureIndexOffset))
        return fileBytes;

    const auto textureData = ReadInt32(pFile, MDLTextureDataIndexOffset);

    if (textureData < 0 || static_cast<std::uint32_t>(textureData) > fileBytes)
        return 0;

    return static_cast<std::uint32_t>(textureData);
}

static bool ValidMDLTextures(const std::uint8_t* pFile, std::uint32_t fileBytes, std::uint32_t structureBytes)
{
    const auto count  = ReadInt32(pFile, MDLTextureCountOffset);
    const auto offset = ReadInt32(pFile, MDLTextureIndexOffset);

    if (!count)
        return true;

    if (!ValidTable(structureBytes, offset, count, MDLTextureBytes, MDLMaxTextures))
        return false;

    for (std::int32_t i = 0; i < count; ++i)
    {
        const auto record = static_cast<std::size_t>(offset) + static_cast<std::size_t>(i) * MDLTextureBytes;
        const auto width  = ReadInt32(pFile, record + MDLTextureWidthOffset);
        const auto height = ReadInt32(pFile, record + MDLTextureHeightOffset);
        const auto pixels = ReadInt32(pFile, record + MDLTextureIndexFieldOffset);

        if (!ValidName(pFile, record, MDLTextureNameBytes))
            return false;

        if (width < 0 || height < 0 || static_cast<std::uint32_t>(width) > MaxTextureDimension || static_cast<std::uint32_t>(height) > MaxTextureDimension)
            return false;

        //The palette follows the pixels.
        const auto textureBytes = std::uint64_t(width) * std::uint64_t(height) + MDLPaletteBytes;

        if (pixels < 0 || static_cast<std::uint64_t>(pixels) + textureBytes > fileBytes)
            return false;
    }

    return true;
}

//Every texture reference of every family names a texture of the header. A header without families or references has
//no skin table.
static bool ValidMDLSkins(const std::uint8_t* pFile, std::uint32_t structureBytes)
{
    const auto refCount    = ReadInt32(pFile, MDLSkinRefCountOffset);
    const auto familyCount = ReadInt32(pFile, MDLSkinFamilyCountOffset);
    const auto offset      = ReadInt32(pFile, MDLSkinIndexOffset);
    const auto textures    = ReadInt32(pFile, MDLTextureCountOffset);

    if (refCount < 0 || familyCount < 0)
        return false;

    if (!refCount || !familyCount)
        return true;

    const auto entries = std::uint64_t(refCount) * std::uint64_t(familyCount);

    if (entries > MaxTableEntries || !ValidTable(structureBytes, offset, static_cast<std::int32_t>(entries), MDLSkinRefBytes, MaxTableEntries))
        return false;

    for (std::uint64_t i = 0; i < entries; ++i)
    {
        const auto texture = ReadInt16(pFile, static_cast<std::size_t>(offset) + static_cast<std::size_t>(i) * MDLSkinRefBytes);

        if (texture < 0 || texture >= textures)
            return false;
    }

    return true;
}

static bool ValidMDLBones(const std::uint8_t* pFile, std::uint32_t structureBytes)
{
    const auto count  = ReadInt32(pFile, MDLBoneCountOffset);
    const auto offset = ReadInt32(pFile, MDLBoneIndexOffset);

    if (!count)
        return true;

    if (!ValidTable(structureBytes, offset, count, MDLBoneBytes, MDLMaxBones))
        return false;

    for (std::int32_t i = 0; i < count; ++i)
    {
        const auto record = static_cast<std::size_t>(offset) + static_cast<std::size_t>(i) * MDLBoneBytes;
        const auto parent = ReadInt32(pFile, record + MDLBoneParentOffset);

        if (!ValidName(pFile, record, MDLBoneNameBytes) || parent < MDLNoParentBone || parent >= count)
            return false;
    }

    return true;
}

//The vertices, normals and their bone tables of a submodel record, which its triangle commands index.
struct MDLSubmodelTables
{
    std::uint32_t vertexCount{};
    std::uint32_t vertices{};
    std::uint32_t vertexBones{};
    std::uint32_t normalCount{};
    std::uint32_t normals{};
    std::uint32_t normalBones{};
};

static bool ReadMDLSubmodelTables(const std::uint8_t* pFile, std::uint32_t structureBytes, std::size_t record, MDLSubmodelTables& tables)
{
    const auto vertexCount = ReadInt32(pFile, record + MDLSubmodelVertCountOffset);
    const auto vertices    = ReadInt32(pFile, record + MDLSubmodelVertIndexOffset);
    const auto vertexBones = ReadInt32(pFile, record + MDLSubmodelVertInfoOffset);
    const auto normalCount = ReadInt32(pFile, record + MDLSubmodelNormCountOffset);
    const auto normals     = ReadInt32(pFile, record + MDLSubmodelNormIndexOffset);
    const auto normalBones = ReadInt32(pFile, record + MDLSubmodelNormInfoOffset);

    if (!ValidTable(structureBytes, vertices, vertexCount, MDLVectorBytes, MaxVertices) || !ValidTable(structureBytes, vertexBones, vertexCount, MDLBoneInfoBytes, MaxVertices))
        return false;

    if (!ValidTable(structureBytes, normals, normalCount, MDLVectorBytes, MaxVertices) || !ValidTable(structureBytes, normalBones, normalCount, MDLBoneInfoBytes, MaxVertices))
        return false;

    tables = {static_cast<std::uint32_t>(vertexCount), static_cast<std::uint32_t>(vertices), static_cast<std::uint32_t>(vertexBones), static_cast<std::uint32_t>(normalCount), static_cast<std::uint32_t>(normals), static_cast<std::uint32_t>(normalBones)};

    return true;
}

//Walks the triangle commands of a mesh to their end, which must lie within structureBytes; every point must index a
//vertex and a normal of the submodel whose bones are bones of the model. pointCount counts the points of every mesh
//checked so far, at most MaxVertices.
static ErrorCode ValidateMDLCommands(const std::uint8_t* pFile, std::uint32_t structureBytes, std::int32_t commands, const MDLSubmodelTables& tables, std::uint32_t boneCount, std::uint32_t& pointCount)
{
    if (commands < 0)
        return ErrorCode::InvalidData;

    auto offset = static_cast<std::uint32_t>(commands);

    for (;;)
    {
        if (!ValidRange(structureBytes, offset, 1, MDLCommandCountBytes))
            return ErrorCode::InvalidData;

        const auto count = ReadInt16(pFile, offset);
        offset += MDLCommandCountBytes;

        if (!count)
            return ErrorCode::None;

        const auto points = static_cast<std::uint32_t>(std::abs(count));

        if (!ValidRange(structureBytes, offset, points, MDLCommandPointBytes))
            return ErrorCode::InvalidData;

        if (points > MaxVertices - pointCount)
            return ErrorCode::LimitExceeded;

        pointCount += points;

        for (std::uint32_t i = 0; i < points; ++i, offset += MDLCommandPointBytes)
        {
            const auto vertex = ReadInt16(pFile, offset + MDLPointVertexOffset);
            const auto normal = ReadInt16(pFile, offset + MDLPointNormalOffset);

            if (vertex < 0 || static_cast<std::uint32_t>(vertex) >= tables.vertexCount || normal < 0 || static_cast<std::uint32_t>(normal) >= tables.normalCount)
                return ErrorCode::InvalidData;

            if (pFile[tables.vertexBones + vertex] >= boneCount || pFile[tables.normalBones + normal] >= boneCount)
                return ErrorCode::InvalidData;
        }
    }
}

static ErrorCode ValidateMDLSubmodel(const std::uint8_t* pFile, std::uint32_t structureBytes, std::size_t record, std::uint32_t boneCount, std::uint32_t& pointCount)
{
    MDLSubmodelTables tables;

    if (!ValidName(pFile, record, MDLSubmodelNameBytes) || !ReadMDLSubmodelTables(pFile, structureBytes, record, tables))
        return ErrorCode::InvalidData;

    const auto meshCount = ReadInt32(pFile, record + MDLSubmodelMeshCountOffset);
    const auto meshes    = ReadInt32(pFile, record + MDLSubmodelMeshIndexOffset);

    if (!ValidTable(structureBytes, meshes, meshCount, MDLMeshBytes, MDLMaxMeshes))
        return ErrorCode::InvalidData;

    for (std::int32_t i = 0; i < meshCount; ++i)
    {
        const auto mesh = static_cast<std::size_t>(meshes) + static_cast<std::size_t>(i) * MDLMeshBytes;

        const auto code = ValidateMDLCommands(pFile, structureBytes, ReadInt32(pFile, mesh + MDLMeshTriangleIndexOffset), tables, boneCount, pointCount);
        if (!Succeeded(code))
            return code;
    }

    return ErrorCode::None;
}

static ErrorCode ValidateMDLBodyParts(const std::uint8_t* pFile, std::uint32_t structureBytes)
{
    const auto count     = ReadInt32(pFile, MDLBodyPartCountOffset);
    const auto offset    = ReadInt32(pFile, MDLBodyPartIndexOffset);
    const auto boneCount = static_cast<std::uint32_t>(ReadInt32(pFile, MDLBoneCountOffset));

    if (!count)
        return ErrorCode::None;

    if (!ValidTable(structureBytes, offset, count, MDLBodyPartBytes, MDLMaxBodyParts))
        return ErrorCode::InvalidData;

    std::uint32_t pointCount{};

    for (std::int32_t i = 0; i < count; ++i)
    {
        const auto record    = static_cast<std::size_t>(offset) + static_cast<std::size_t>(i) * MDLBodyPartBytes;
        const auto submodels = ReadInt32(pFile, record + MDLBodyPartModelIndexOffset);
        const auto models    = ReadInt32(pFile, record + MDLBodyPartModelCountOffset);

        if (!ValidName(pFile, record, MDLBodyPartNameBytes) || !ValidTable(structureBytes, submodels, models, MDLSubmodelBytes, MDLMaxSubmodels))
            return ErrorCode::InvalidData;

        for (std::int32_t j = 0; j < models; ++j)
        {
            const auto submodel = static_cast<std::size_t>(submodels) + static_cast<std::size_t>(j) * MDLSubmodelBytes;

            const auto code = ValidateMDLSubmodel(pFile, structureBytes, submodel, boneCount, pointCount);
            if (!Succeeded(code))
                return code;
        }
    }

    return ErrorCode::None;
}

//The header and the texture header of a file; structureBytes receives the size of its kept part.
static bool ValidMDLTextureHeader(const std::uint8_t* pFile, std::uint32_t fileBytes, std::uint32_t& structureBytes)
{
    if (!ValidMDLHeader(pFile, fileBytes))
        return false;

    structureBytes = GetMDLStructureBytes(pFile, fileBytes);

    if (structureBytes < MDLHeaderBytes)
        return false;

    return ValidMDLTextures(pFile, fileBytes, structureBytes) && ValidMDLSkins(pFile, structureBytes);
}

ErrorCode ValidateMDLFile(const std::uint8_t* pFile, std::uint32_t fileBytes)
{
    std::uint32_t structureBytes{};

    if (!ValidMDLTextureHeader(pFile, fileBytes, structureBytes) || !ValidMDLBones(pFile, structureBytes))
        return ErrorCode::InvalidData;

    return ValidateMDLBodyParts(pFile, structureBytes);
}

ErrorCode ValidateMDLTextureFile(const std::uint8_t* pFile, std::uint32_t fileBytes)
{
    std::uint32_t structureBytes{};

    return ValidMDLTextureHeader(pFile, fileBytes, structureBytes) ? ErrorCode::None : ErrorCode::InvalidData;
}

static bool ValidVector2(const Vector2& vector)
{
    return std::isfinite(vector.x) && std::isfinite(vector.y);
}

bool ValidStudioShadingParams(const StudioShadingParams& params)
{
    const auto bValidArray = [](const std::array<float, 4>& values) {
        return std::all_of(values.begin(), values.end(), [](float value) { return std::isfinite(value); });
    };

    const bool bValidScalars = std::isfinite(params.celshadeMidpoint) && std::isfinite(params.celshadeSoftness) && std::isfinite(params.outlineSize) && std::isfinite(params.outlineDark) && std::isfinite(params.rimlightPower) && std::isfinite(params.rimlightSmooth) && std::isfinite(params.rimdarkPower) && std::isfinite(params.rimdarkSmooth) && std::isfinite(params.hairSpecularExp) && std::isfinite(params.hairSpecularExp2);

    const bool bValidVectors = ValidVector(params.celshadeShadowColor) && ValidVector(params.celshadeHeadOffset) && ValidVector2(params.celshadeLightdirAdjust) && ValidVector2(params.rimlightSmooth2) && ValidVector(params.rimlightColor) && ValidVector2(params.rimdarkSmooth2) && ValidVector(params.rimdarkColor) && ValidVector(params.hairSpecularIntensity) && ValidVector(params.hairSpecularIntensity2) && ValidVector2(params.hairSpecularSmooth) && ValidVector2(params.hairShadowOffset);

    return bValidScalars && bValidVectors && bValidArray(params.celshadeSpecular) && bValidArray(params.hairSpecularNoise) && bValidArray(params.hairSpecularNoise2);
}

//Texture count of the texture header of a validated asset.
static std::uint32_t CountMDLHeaderTextures(const MDLAssetView& asset)
{
    const auto pHeader = asset.textureFileBytes ? asset.pTextureFile : asset.pFile;

    return static_cast<std::uint32_t>(ReadInt32(pHeader, MDLTextureCountOffset));
}

ErrorCode ValidateMDLAssetFiles(const MDLAssetView& asset)
{
    auto code = ValidateMDLFile(asset.pFile, asset.fileBytes);
    if (!Succeeded(code))
        return code;

    //A T.mdl replaces only an empty texture header.
    if (asset.textureFileBytes)
    {
        if (ReadInt32(asset.pFile, MDLTextureCountOffset))
            return ErrorCode::InvalidData;

        code = ValidateMDLTextureFile(asset.pTextureFile, asset.textureFileBytes);
        if (!Succeeded(code))
            return code;
    }

    const auto textureCount = CountMDLHeaderTextures(asset);

    for (std::uint32_t i = 0; i < asset.textureCount; ++i)
    {
        if (asset.pTextures[i].slot >= textureCount)
            return ErrorCode::InvalidData;
    }

    if (asset.materialCount > MDLMaxTextures || !ValidArray(asset.pMaterials, asset.materialCount))
        return ErrorCode::InvalidData;

    std::bitset<MDLMaxTextures> materialTextures;

    for (std::uint32_t i = 0; i < asset.materialCount; ++i)
    {
        const auto& material = asset.pMaterials[i];

        if (material.textureIndex >= textureCount || materialTextures.test(material.textureIndex))
            return ErrorCode::InvalidData;

        if (!ValidVector2(material.replaceScale) || !ValidVector2(material.baseSpecular))
            return ErrorCode::InvalidData;

        materialTextures.set(material.textureIndex);
    }

    const auto boneCount = static_cast<std::uint32_t>(ReadInt32(asset.pFile, MDLBoneCountOffset));

    if ((asset.boneFlagCount && asset.boneFlagCount != boneCount) || !ValidArray(asset.pBoneFlags, asset.boneFlagCount))
        return ErrorCode::InvalidData;

    constexpr auto ShadingParamCount = static_cast<std::uint32_t>(StudioShadingParam::Count);

    if (asset.shading.mask >> ShadingParamCount || !ValidStudioShadingParams(asset.shading.values))
        return ErrorCode::InvalidData;

    return ErrorCode::None;
}

ErrorCode ValidateMDLSubmodelReference(const MDLAssetView& asset, std::uint32_t bodyPart, std::uint32_t subModel, std::uint32_t boneCount, const TextureBinding* pTextureOverrides, std::uint32_t textureOverrideCount)
{
    //The asset was checked when it was created, but a recorded frame may come from elsewhere: every read is bounded.
    if (!ValidMDLHeader(asset.pFile, asset.fileBytes))
        return ErrorCode::InvalidData;

    const auto structureBytes = GetMDLStructureBytes(asset.pFile, asset.fileBytes);
    const auto bodyPartCount  = ReadInt32(asset.pFile, MDLBodyPartCountOffset);
    const auto bodyParts      = ReadInt32(asset.pFile, MDLBodyPartIndexOffset);
    const auto fileBoneCount  = ReadInt32(asset.pFile, MDLBoneCountOffset);

    if (!ValidTable(structureBytes, bodyParts, bodyPartCount, MDLBodyPartBytes, MDLMaxBodyParts) || bodyPart >= static_cast<std::uint32_t>(bodyPartCount))
        return ErrorCode::InvalidData;

    if (fileBoneCount < 0 || boneCount != static_cast<std::uint32_t>(fileBoneCount))
        return ErrorCode::InvalidData;

    const auto record = static_cast<std::size_t>(bodyParts) + std::size_t(bodyPart) * MDLBodyPartBytes;

    if (subModel >= static_cast<std::uint32_t>(std::max(ReadInt32(asset.pFile, record + MDLBodyPartModelCountOffset), 0)))
        return ErrorCode::InvalidData;

    const auto pHeader     = asset.textureFileBytes ? asset.pTextureFile : asset.pFile;
    const auto headerBytes = asset.textureFileBytes ? asset.textureFileBytes : asset.fileBytes;

    if (!ValidMDLHeader(pHeader, headerBytes))
        return ErrorCode::InvalidData;

    const auto textureCount = static_cast<std::uint32_t>(std::max(ReadInt32(pHeader, MDLTextureCountOffset), 0));

    for (std::uint32_t i = 0; i < textureOverrideCount; ++i)
    {
        if (pTextureOverrides[i].slot >= textureCount)
            return ErrorCode::InvalidData;
    }

    return ErrorCode::None;
}

MDLRecordingMetadata ReadMDLRecordingMetadata(const MDLAssetView& asset)
{
    MDLRecordingMetadata metadata;
    metadata.boneCount       = static_cast<std::uint32_t>(ReadInt32(asset.pFile, MDLBoneCountOffset));
    const auto bodyParts     = ReadInt32(asset.pFile, MDLBodyPartIndexOffset);
    const auto bodyPartCount = ReadInt32(asset.pFile, MDLBodyPartCountOffset);
    for (std::int32_t i = 0; i < bodyPartCount; ++i)
        metadata.bodySubmodels.push_back(static_cast<std::uint32_t>(ReadInt32(asset.pFile, bodyParts + i * MDLBodyPartBytes + MDLBodyPartModelCountOffset)));
    const auto pHeader    = asset.textureFileBytes ? asset.pTextureFile : asset.pFile;
    metadata.textureCount = static_cast<std::uint32_t>(ReadInt32(pHeader, MDLTextureCountOffset));
    return metadata;
}

//Appends the triangles of a mesh's commands to model, as GoldSrc draws them: fans around their first point and strips
//turning every other triangle around, so that every triangle keeps the winding of the first.
static void BuildMDLMeshTriangles(const std::uint8_t* pFile, std::uint32_t commands, const MDLSubmodelTables& tables, MDLModel& model)
{
    auto offset = commands;

    for (auto count = ReadInt16(pFile, offset); count; count = ReadInt16(pFile, offset))
    {
        offset += MDLCommandCountBytes;

        const bool bFan   = count < 0;
        const auto points = static_cast<std::uint32_t>(std::abs(count));
        const auto first  = static_cast<std::uint32_t>(model.vertices.size());

        for (std::uint32_t i = 0; i < points; ++i, offset += MDLCommandPointBytes)
        {
            const auto vertex = static_cast<std::uint32_t>(ReadInt16(pFile, offset + MDLPointVertexOffset));
            const auto normal = static_cast<std::uint32_t>(ReadInt16(pFile, offset + MDLPointNormalOffset));

            MDLVertex point;
            point.position     = ReadVector(pFile, tables.vertices + vertex * MDLVectorBytes);
            point.normal       = ReadVector(pFile, tables.normals + normal * MDLVectorBytes);
            point.texcoord     = {static_cast<float>(ReadInt16(pFile, offset + MDLPointTexcoordSOffset)), static_cast<float>(ReadInt16(pFile, offset + MDLPointTexcoordTOffset))};
            point.positionBone = pFile[tables.vertexBones + vertex];
            point.normalBone   = pFile[tables.normalBones + normal];

            const auto current = static_cast<std::uint32_t>(model.vertices.size());

            model.vertices.push_back(point);

            //Every point after the second ends a triangle with the two before it: a fan's with its first point and the
            //previous one, a strip's with the previous two, in turned order for every other triangle.
            if (i >= 2)
            {
                if (bFan)
                {
                    model.indices.insert(model.indices.end(), {first, current - 1, current});
                }
                else if (i % 2)
                {
                    model.indices.insert(model.indices.end(), {current - 1, current - 2, current});
                }
                else
                {
                    model.indices.insert(model.indices.end(), {current - 2, current - 1, current});
                }
            }
        }
    }
}

static void BuildMDLSubmodel(const std::uint8_t* pFile, std::size_t record, MDLModel& model)
{
    MDLSubmodelTables tables;
    ReadMDLSubmodelTables(pFile, MaxAssetBytes, record, tables);

    const auto meshCount = static_cast<std::uint32_t>(ReadInt32(pFile, record + MDLSubmodelMeshCountOffset));
    const auto meshes    = static_cast<std::size_t>(ReadInt32(pFile, record + MDLSubmodelMeshIndexOffset));

    MDLSubmodel submodel;
    submodel.name      = ReadName(pFile, record, MDLSubmodelNameBytes);
    submodel.firstMesh = static_cast<std::uint32_t>(model.meshes.size());
    submodel.meshCount = meshCount;

    for (std::uint32_t i = 0; i < meshCount; ++i)
    {
        const auto meshRecord = meshes + std::size_t(i) * MDLMeshBytes;

        MDLMesh mesh;
        mesh.firstIndex = static_cast<std::uint32_t>(model.indices.size());
        mesh.skinref    = ReadInt32(pFile, meshRecord + MDLMeshSkinRefOffset);

        BuildMDLMeshTriangles(pFile, static_cast<std::uint32_t>(ReadInt32(pFile, meshRecord + MDLMeshTriangleIndexOffset)), tables, model);

        mesh.indexCount = static_cast<std::uint32_t>(model.indices.size()) - mesh.firstIndex;

        model.meshes.push_back(mesh);
    }

    model.submodels.push_back(std::move(submodel));
}

//The textures and skin table of a validated texture header.
static void BuildMDLTextureHeader(const std::uint8_t* pHeader, MDLModel& model)
{
    const auto textureCount = static_cast<std::uint32_t>(ReadInt32(pHeader, MDLTextureCountOffset));
    const auto textures     = static_cast<std::size_t>(ReadInt32(pHeader, MDLTextureIndexOffset));

    for (std::uint32_t i = 0; i < textureCount; ++i)
    {
        const auto record = textures + std::size_t(i) * MDLTextureBytes;

        MDLTexture texture;
        texture.name   = ReadName(pHeader, record, MDLTextureNameBytes);
        texture.flags  = ReadUInt32(pHeader, record + MDLTextureFlagsOffset);
        texture.width  = ReadUInt32(pHeader, record + MDLTextureWidthOffset);
        texture.height = ReadUInt32(pHeader, record + MDLTextureHeightOffset);

        model.textures.push_back(std::move(texture));
    }

    //GoldSrc reads skins only from a header with a texture table and a skin table.
    const auto skinRefs = ReadInt32(pHeader, MDLSkinIndexOffset);

    if (!textures || !skinRefs)
        return;

    model.skinRefCount    = static_cast<std::uint32_t>(ReadInt32(pHeader, MDLSkinRefCountOffset));
    model.skinFamilyCount = static_cast<std::uint32_t>(ReadInt32(pHeader, MDLSkinFamilyCountOffset));

    const auto entries = std::size_t(model.skinRefCount) * model.skinFamilyCount;

    for (std::size_t i = 0; i < entries; ++i)
        model.skinRefs.push_back(ReadInt16(pHeader, static_cast<std::size_t>(skinRefs) + i * MDLSkinRefBytes));
}

ErrorCode BuildMDLModel(const MDLAssetView& asset, MDLModel& model)
{
    model = {};

    auto code = ValidateMDLAssetFiles(asset);
    if (!Succeeded(code))
        return code;

    const auto pFile = asset.pFile;

    model.flags     = ReadUInt32(pFile, MDLFlagsOffset) | asset.extraModelFlags;
    model.boneCount = static_cast<std::uint32_t>(ReadInt32(pFile, MDLBoneCountOffset));

    if (asset.boneFlagCount)
    {
        model.boneFlags.assign(asset.pBoneFlags, asset.pBoneFlags + asset.boneFlagCount);
    }
    else
    {
        const auto bones = static_cast<std::size_t>(ReadInt32(pFile, MDLBoneIndexOffset));

        for (std::uint32_t i = 0; i < model.boneCount; ++i)
            model.boneFlags.push_back(ReadUInt32(pFile, bones + std::size_t(i) * MDLBoneBytes + MDLBoneFlagsOffset));
    }

    const auto bodyPartCount = static_cast<std::uint32_t>(ReadInt32(pFile, MDLBodyPartCountOffset));
    const auto bodyParts     = static_cast<std::size_t>(ReadInt32(pFile, MDLBodyPartIndexOffset));

    for (std::uint32_t i = 0; i < bodyPartCount; ++i)
    {
        const auto record    = bodyParts + std::size_t(i) * MDLBodyPartBytes;
        const auto submodels = static_cast<std::size_t>(ReadInt32(pFile, record + MDLBodyPartModelIndexOffset));

        MDLBodyPart bodyPart;
        bodyPart.firstSubmodel = static_cast<std::uint32_t>(model.submodels.size());
        bodyPart.submodelCount = static_cast<std::uint32_t>(ReadInt32(pFile, record + MDLBodyPartModelCountOffset));

        for (std::uint32_t j = 0; j < bodyPart.submodelCount; ++j)
            BuildMDLSubmodel(pFile, submodels + std::size_t(j) * MDLSubmodelBytes, model);

        model.bodyParts.push_back(bodyPart);
    }

    BuildMDLTextureHeader(asset.textureFileBytes ? asset.pTextureFile : pFile, model);

    return ErrorCode::None;
}

std::int32_t ResolveMDLTexture(const MDLModel& model, std::uint32_t skin, std::int32_t skinref)
{
    if (!model.skinRefCount || !model.skinFamilyCount || model.textures.empty())
        return -1;

    if (skinref < 0 || static_cast<std::uint32_t>(skinref) >= model.skinRefCount)
        return -1;

    const auto family  = skin < model.skinFamilyCount ? skin : 0;
    const auto texture = model.skinRefs[std::size_t(family) * model.skinRefCount + static_cast<std::uint32_t>(skinref)];

    if (texture < 0 || static_cast<std::size_t>(texture) >= model.textures.size())
        return -1;

    return texture;
}

MDLPackedTexture ParseMDLPackedTextureName(const std::string& name)
{
    MDLPackedTexture packed;

    const bool bPacked = name.size() >= MDLPackedPrefixLength && std::equal(MDLPackedPrefix, MDLPackedPrefix + MDLPackedPrefixLength, name.begin(), [](char a, char b) {
                             return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b));
                         });

    if (!bPacked)
        return packed;

    //Each part is read once a whole part fits in the name; a letter seen before keeps its first image.
    for (std::uint32_t i = 0; i < MDLPackedMaxParts; ++i)
    {
        const auto part = MDLPackedFirstPartOffset + i * MDLPackedPartLength;

        if (name.size() < part + MDLPackedPartLength || name[part] != '_')
            continue;

        const auto digit = static_cast<unsigned char>(name[part + 2]);
        const auto image = std::isdigit(digit) ? static_cast<std::int32_t>(digit - '0') : 0;

        std::int32_t* pImage = nullptr;

        switch (name[part + 1])
        {
            case 'D': pImage = &packed.diffuse; break;
            case 'N': pImage = &packed.normal; break;
            case 'P': pImage = &packed.parallax; break;
            case 'S': pImage = &packed.specular; break;
            default: break;
        }

        if (pImage && *pImage < 0)
        {
            *pImage = image;
            ++packed.count;
        }
    }

    return packed;
}
} // namespace GoldSrcRender
