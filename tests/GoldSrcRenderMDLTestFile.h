#pragma once

#include "GoldSrcRenderBSPTestFile.h"
#include "GoldSrcRenderMDL.h"

#include <array>
#include <cstdint>
#include <vector>

//A small studio model (MDL version 10) for tests:
//
//  bone  name   parent      bodypart  submodels
//  0     root   none        0 body    0 main: mesh 0 (a strip of 4 points, skinref 0), mesh 1 (a fan of 4, skinref 1)
//  1     child  0                     1 blank: no meshes
//                           1 head    0 head: mesh 0 (a fan of 3 points, skinref 0)
//
//The texture header holds two 4x4 textures, skin_a and Packed_D0_N1 (chrome), and two skin families: {0, 1} and
//{1, 0}. Its texture data, every texture's pixels and palette, ends the file. A model with TestMDLParts::Model only
//has an empty texture header; a TestMDLParts::Textures file is its T.mdl, with no bones or bodyparts.
namespace GoldSrcRenderTest
{
using namespace GoldSrcRender;

constexpr std::uint32_t TestMDLBoneCount       = 2;
constexpr std::uint32_t TestMDLBodyPartCount   = 2;
constexpr std::uint32_t TestMDLSubmodelCount   = 3;
constexpr std::uint32_t TestMDLMeshCount       = 3;
constexpr std::uint32_t TestMDLTextureCount    = 2;
constexpr std::uint32_t TestMDLSkinRefCount    = 2;
constexpr std::uint32_t TestMDLSkinFamilyCount = 2;
constexpr std::uint32_t TestMDLTextureSize     = 4;
constexpr std::uint32_t TestMDLPaletteBytes    = 768;

//Bodypart 0 has the main and blank submodels, bodypart 1 the head.
constexpr std::uint32_t TestMDLBodySubmodels = 2;
constexpr std::uint32_t TestMDLHeadSubmodels = 1;

//Points of the triangle commands: the main submodel's strip and fan, the head's fan. A negative count is a fan.
constexpr std::int16_t  TestMDLStripPoints   = 4;
constexpr std::int16_t  TestMDLFanPoints     = -4;
constexpr std::int16_t  TestMDLHeadFanPoints = -3;
constexpr std::uint32_t TestMDLMainVertices  = 4;
constexpr std::uint32_t TestMDLHeadVertices  = 3;

//The skin families, family by family.
constexpr std::array<std::int16_t, TestMDLSkinRefCount * TestMDLSkinFamilyCount> TestMDLSkinRefs = {0, 1, 1, 0};

//Header fields and record layouts the tests patch (studiohdr_t, mstudiobone_t, mstudiobodyparts_t, mstudiomodel_t,
//mstudiomesh_t, mstudiotexture_t).
constexpr std::uint32_t TestMDLNameBytes                = 64;
constexpr std::uint32_t TestMDLFlagsOffset              = 136;
constexpr std::uint32_t TestMDLBoneCountOffset          = 140;
constexpr std::uint32_t TestMDLBoneIndexOffset          = 144;
constexpr std::uint32_t TestMDLTextureCountOffset       = 180;
constexpr std::uint32_t TestMDLTextureIndexOffset       = 184;
constexpr std::uint32_t TestMDLTextureDataIndexOffset   = 188;
constexpr std::uint32_t TestMDLSkinRefCountOffset       = 192;
constexpr std::uint32_t TestMDLSkinFamilyCountOffset    = 196;
constexpr std::uint32_t TestMDLSkinIndexOffset          = 200;
constexpr std::uint32_t TestMDLBodyPartCountOffset      = 204;
constexpr std::uint32_t TestMDLBodyPartIndexOffset      = 208;
constexpr std::uint32_t TestMDLBoneBytes                = 112;
constexpr std::uint32_t TestMDLBoneNameBytes            = 32;
constexpr std::uint32_t TestMDLBoneParentOffset         = 32;
constexpr std::uint32_t TestMDLBoneFlagsOffset          = 36;
constexpr std::uint32_t TestMDLBodyPartBytes            = 76;
constexpr std::uint32_t TestMDLBodyPartModelCountOffset = 64;
constexpr std::uint32_t TestMDLBodyPartModelIndexOffset = 72;
constexpr std::uint32_t TestMDLSubmodelBytes            = 112;
constexpr std::uint32_t TestMDLSubmodelMeshCountOffset  = 72;
constexpr std::uint32_t TestMDLSubmodelVertCountOffset  = 80;
constexpr std::uint32_t TestMDLSubmodelNormCountOffset  = 92;
constexpr std::uint32_t TestMDLMeshBytes                = 20;
constexpr std::uint32_t TestMDLMeshCommandsOffset       = 4;
constexpr std::uint32_t TestMDLTextureBytes             = 80;
constexpr std::uint32_t TestMDLTextureWidthOffset       = 68;
constexpr std::uint32_t TestMDLTexturePixelsOffset      = 76;
constexpr std::uint32_t TestMDLVectorBytes              = 12;
constexpr std::uint32_t TestMDLCommandPointBytes        = 8;
constexpr std::uint32_t TestMDLCommandCountBytes        = 2;

enum class TestMDLParts
{
    Complete,
    Model,
    Textures
};

//A test file and where its tables start; a table the file does not have starts at 0.
struct TestMDLFile
{
    std::vector<std::uint8_t> bytes;
    std::uint32_t             bones{};
    std::uint32_t             bodyParts{};
    std::uint32_t             submodels{};
    std::uint32_t             meshes{};
    std::uint32_t             mainVertexBones{};
    std::uint32_t             mainCommands{};
    std::uint32_t             textures{};
    std::uint32_t             skins{};
    std::uint32_t             textureData{};
};

//One submodel's vertices and normals, one normal per vertex, with the bone of each.
struct TestMDLSubmodelData
{
    std::uint32_t               vertexCount{};
    std::uint32_t               vertices{};
    std::uint32_t               vertexBones{};
    std::uint32_t               normals{};
    std::uint32_t               normalBones{};
    std::array<std::uint8_t, 4> bones{};
};

//Position of point i of a submodel: the corners of a unit square in the z = 0 plane.
inline std::array<float, 3> TestMDLPosition(std::uint32_t i)
{
    return {static_cast<float>(i % 2), static_cast<float>(i / 2), 0};
}

//Texel coordinates of point i of a command: s = 10 + i, t = 20 + i.
constexpr std::int16_t TestMDLTexcoordS = 10;
constexpr std::int16_t TestMDLTexcoordT = 20;

inline TestMDLFile MakeTestMDL(TestMDLParts parts = TestMDLParts::Complete, std::uint32_t textureCount = TestMDLTextureCount)
{
    const bool bModel    = parts != TestMDLParts::Textures;
    const bool bTextures = parts != TestMDLParts::Model;

    TestMDLFile file;

    //Lay out every table after the header, the texture data last.
    std::uint32_t offset = MDLHeaderBytes;

    const auto take = [&offset](std::uint32_t bytes) {
        const auto start = offset;
        offset += bytes;
        return start;
    };

    TestMDLSubmodelData main;
    TestMDLSubmodelData head;
    std::uint32_t       fanCommands{};
    std::uint32_t       headCommands{};

    main.vertexCount = TestMDLMainVertices;
    main.bones       = {0, 1, 0, 1};
    head.vertexCount = TestMDLHeadVertices;
    head.bones       = {1, 1, 1, 0};

    const auto commandBytes = [](std::int16_t points) {
        return TestMDLCommandCountBytes + static_cast<std::uint32_t>(points < 0 ? -points : points) * TestMDLCommandPointBytes + TestMDLCommandCountBytes;
    };

    if (bModel)
    {
        file.bones     = take(TestMDLBoneCount * TestMDLBoneBytes);
        file.bodyParts = take(TestMDLBodyPartCount * TestMDLBodyPartBytes);
        file.submodels = take(TestMDLSubmodelCount * TestMDLSubmodelBytes);
        file.meshes    = take(TestMDLMeshCount * TestMDLMeshBytes);

        for (auto* pData : {&main, &head})
        {
            pData->vertices    = take(pData->vertexCount * TestMDLVectorBytes);
            pData->vertexBones = take(pData->vertexCount);
            pData->normals     = take(pData->vertexCount * TestMDLVectorBytes);
            pData->normalBones = take(pData->vertexCount);
        }

        file.mainVertexBones = main.vertexBones;
        file.mainCommands    = take(commandBytes(TestMDLStripPoints));
        fanCommands          = take(commandBytes(TestMDLFanPoints));
        headCommands         = take(commandBytes(TestMDLHeadFanPoints));
    }

    if (bTextures)
    {
        file.textures    = take(textureCount * TestMDLTextureBytes);
        file.skins       = take(static_cast<std::uint32_t>(TestMDLSkinRefs.size() * sizeof(std::int16_t)));
        file.textureData = take(textureCount * (TestMDLTextureSize * TestMDLTextureSize + TestMDLPaletteBytes));
    }

    const auto fileBytes = offset;

    CBSPWriter writer;

    //Header.
    writer.UInt32(MDLIdentifier);
    writer.UInt32(MDLVersion);
    writer.Name(bModel ? "test.mdl" : "testT.mdl", TestMDLNameBytes);
    writer.UInt32(fileBytes);

    while (writer.Bytes().size() < MDLHeaderBytes)
        writer.UInt8(0);

    auto& bytes = writer.Bytes();

    PatchUInt32(bytes, TestMDLBoneCountOffset, bModel ? TestMDLBoneCount : 0);
    PatchUInt32(bytes, TestMDLBoneIndexOffset, file.bones);
    PatchUInt32(bytes, TestMDLTextureCountOffset, bTextures ? textureCount : 0);
    PatchUInt32(bytes, TestMDLTextureIndexOffset, file.textures);
    PatchUInt32(bytes, TestMDLTextureDataIndexOffset, file.textureData);
    PatchUInt32(bytes, TestMDLSkinRefCountOffset, bTextures ? TestMDLSkinRefCount : 0);
    PatchUInt32(bytes, TestMDLSkinFamilyCountOffset, bTextures ? TestMDLSkinFamilyCount : 0);
    PatchUInt32(bytes, TestMDLSkinIndexOffset, file.skins);
    PatchUInt32(bytes, TestMDLBodyPartCountOffset, bModel ? TestMDLBodyPartCount : 0);
    PatchUInt32(bytes, TestMDLBodyPartIndexOffset, file.bodyParts);

    if (bModel)
    {
        //Bones: a root and its child.
        for (std::uint32_t i = 0; i < TestMDLBoneCount; ++i)
        {
            writer.Name(i ? "child" : "root", TestMDLBoneNameBytes);
            writer.Int32(i ? 0 : -1);

            while (writer.Bytes().size() < file.bones + (i + 1) * TestMDLBoneBytes)
                writer.UInt8(0);
        }

        //Bodyparts: their submodel counts and tables (the base field is not read).
        const std::uint32_t submodelCounts[TestMDLBodyPartCount] = {TestMDLBodySubmodels, TestMDLHeadSubmodels};

        for (std::uint32_t i = 0, first = 0; i < TestMDLBodyPartCount; first += submodelCounts[i], ++i)
        {
            writer.Name(i ? "head" : "body", TestMDLNameBytes);
            writer.UInt32(submodelCounts[i]);
            writer.UInt32(1);
            writer.UInt32(file.submodels + first * TestMDLSubmodelBytes);
        }

        //Submodels: main, blank and head.
        const auto writeSubmodel = [&writer](const char* szName, std::uint32_t meshCount, std::uint32_t meshes, const TestMDLSubmodelData* pData) {
            writer.Name(szName, TestMDLNameBytes);
            writer.UInt32(0); //type
            writer.Float(0);  //bounding radius
            writer.UInt32(meshCount);
            writer.UInt32(meshes);
            writer.UInt32(pData ? pData->vertexCount : 0);
            writer.UInt32(pData ? pData->vertexBones : 0);
            writer.UInt32(pData ? pData->vertices : 0);
            writer.UInt32(pData ? pData->vertexCount : 0);
            writer.UInt32(pData ? pData->normalBones : 0);
            writer.UInt32(pData ? pData->normals : 0);
            writer.UInt32(0); //groups
            writer.UInt32(0);
        };

        writeSubmodel("main", 2, file.meshes, &main);
        writeSubmodel("blank", 0, 0, nullptr);
        writeSubmodel("head", 1, file.meshes + 2 * TestMDLMeshBytes, &head);

        //Meshes: triangle count, commands, skinref, normal count and table (not read).
        const std::uint32_t meshCommands[TestMDLMeshCount] = {file.mainCommands, fanCommands, headCommands};
        const std::int32_t  meshSkinrefs[TestMDLMeshCount] = {0, 1, 0};

        for (std::uint32_t i = 0; i < TestMDLMeshCount; ++i)
        {
            writer.UInt32(2);
            writer.UInt32(meshCommands[i]);
            writer.Int32(meshSkinrefs[i]);
            writer.UInt32(0);
            writer.UInt32(0);
        }

        //Vertices and normals: the normal of point i is (0, 0, 1 + i).
        for (const auto* pData : {&main, &head})
        {
            for (std::uint32_t i = 0; i < pData->vertexCount; ++i)
            {
                const auto position = TestMDLPosition(i);
                writer.Vector(position[0], position[1], position[2]);
            }

            for (std::uint32_t i = 0; i < pData->vertexCount; ++i)
                writer.UInt8(pData->bones[i]);

            for (std::uint32_t i = 0; i < pData->vertexCount; ++i)
                writer.Vector(0, 0, static_cast<float>(1 + i));

            for (std::uint32_t i = 0; i < pData->vertexCount; ++i)
                writer.UInt8(pData->bones[i]);
        }

        //Triangle commands: point i uses vertex and normal i.
        for (const auto points : {TestMDLStripPoints, TestMDLFanPoints, TestMDLHeadFanPoints})
        {
            writer.Int16(points);

            for (std::int16_t i = 0; i < (points < 0 ? -points : points); ++i)
            {
                writer.Int16(i);
                writer.Int16(i);
                writer.Int16(static_cast<std::int16_t>(TestMDLTexcoordS + i));
                writer.Int16(static_cast<std::int16_t>(TestMDLTexcoordT + i));
            }

            writer.Int16(0);
        }
    }

    if (bTextures)
    {
        const char*         names[TestMDLTextureCount] = {"skin_a", "Packed_D0_N1"};
        const std::uint32_t flags[TestMDLTextureCount] = {0, MDLTextureChrome};

        for (std::uint32_t i = 0; i < textureCount; ++i)
        {
            writer.Name(names[i % TestMDLTextureCount], TestMDLNameBytes);
            writer.UInt32(flags[i % TestMDLTextureCount]);
            writer.UInt32(TestMDLTextureSize);
            writer.UInt32(TestMDLTextureSize);
            writer.UInt32(file.textureData + i * (TestMDLTextureSize * TestMDLTextureSize + TestMDLPaletteBytes));
        }

        for (const auto texture : TestMDLSkinRefs)
            writer.Int16(texture);

        while (writer.Bytes().size() < fileBytes)
            writer.UInt8(0);
    }

    file.bytes = std::move(writer.Bytes());

    return file;
}

inline MDLAssetView TestMDLAsset(const std::vector<std::uint8_t>& file)
{
    MDLAssetView asset;
    asset.pFile     = file.data();
    asset.fileBytes = static_cast<std::uint32_t>(file.size());

    return asset;
}
} // namespace GoldSrcRenderTest
