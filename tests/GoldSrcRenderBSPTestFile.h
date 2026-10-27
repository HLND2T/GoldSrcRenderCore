#pragma once

#include "GoldSrcRenderBSP.h"
#include "GoldSrcRenderAssetUtil.h"

#include <array>
#include <climits>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

//A small BSP30 world for tests. Every face is the same 64x64 quad on the plane z = 0:
//
//  face  texture  slot  bound  category
//  0     floor    0     yes    drawn, one lightstyle
//  1     sky      1     no     sky
//  2     !water   2     yes    turbulent: one water surface of a single 64x64 piece, unlit
//  3     wall     3     no     drawn with the missing texture, no samples
//  4     {grate   4     yes    drawn, masked (its texture's AlphaMode marks transparent texels)
//  5     floor    0     yes    degenerate (two edges)
//  6     floor2   5     yes    drawn, two lightstyles
//  7     trigger  6     yes    drawn unlit and tiled (TEX_SPECIAL): no lightmap, scrolls with its entity
//
//The world (model 0) lists every face. AddTestBSPSubmodel appends submodel 1, which lists faces 4 to 7 again: it draws
//the masked {grate, the ordinary floor2 and the unlit trigger.
//
//Node 0 splits x at 1000: leaf 2 lies beyond it, node 1 before it. Node 1 owns every face and splits z at 0 into
//leaf 1 above and the solid leaf 0 below. Leaf 1 lists every face and sees only itself; leaf 2 lists none and sees
//only itself. The entities lump holds a worldspawn; BSPLayout::BlueShift files swap its table entry with the planes'.
namespace GoldSrcRenderTest
{
using namespace GoldSrcRender;

constexpr std::uint32_t TestBSPFaceCount       = 8;
constexpr std::uint32_t TestBSPFloorFace       = 0;
constexpr std::uint32_t TestBSPSkyFace         = 1;
constexpr std::uint32_t TestBSPWaterFace       = 2;
constexpr std::uint32_t TestBSPDegenerateFace  = 5;
constexpr std::uint32_t TestBSPFloor2Face      = 6;
constexpr std::uint32_t TestBSPTriggerFace     = 7;
constexpr std::uint32_t TestBSPMiptexCount     = 7;
constexpr std::uint32_t TestBSPFloorSlot       = 0;
constexpr std::uint32_t TestBSPSkySlot         = 1;
constexpr std::uint32_t TestBSPWaterSlot       = 2;
constexpr std::uint32_t TestBSPWallSlot        = 3;
constexpr std::uint32_t TestBSPGrateSlot       = 4;
constexpr std::uint32_t TestBSPFloor2Slot      = 5;
constexpr std::uint32_t TestBSPTriggerSlot     = 6;
constexpr std::uint32_t TestBSPFloorWidth      = 64;
constexpr std::uint32_t TestBSPFloorHeight     = 32;
constexpr std::uint32_t TestBSPTextureSize     = 16;
constexpr std::uint32_t TestBSPQuadSize        = 64;
constexpr std::uint32_t TestBSPLuxelsPerSide   = TestBSPQuadSize / 16 + 1;
constexpr std::uint32_t TestBSPFaceLuxels      = TestBSPLuxelsPerSide * TestBSPLuxelsPerSide;
constexpr std::uint32_t TestBSPSampleBytes     = 3;
constexpr std::uint32_t TestBSPVisibleLeaves   = 2;
constexpr float         TestBSPSplitDistance   = 1000;
constexpr std::int32_t  TestBSPEmptyContents   = -1;
constexpr std::int32_t  TestBSPSolidContents   = -2;
constexpr std::int32_t  TestBSPNoOffset        = -1;
constexpr std::int32_t  TestBSPSpecialFlag     = 1;
constexpr std::uint8_t  TestBSPNoStyle         = 255;
constexpr std::uint8_t  TestBSPAnimatedStyle   = 5;
constexpr std::uint32_t TestBSPMiptexNameBytes = 16;
constexpr std::uint32_t TestBSPMiptexBytes     = 40;

//Faces of submodel 1: {grate (drawn, masked), floor (degenerate), floor2 (drawn) and trigger (drawn unlit).
constexpr std::uint32_t TestBSPSubmodelFirstFace = 4;
constexpr std::uint32_t TestBSPSubmodelFaceCount = 4;
constexpr std::int32_t  TestBSPSubmodelHeadNode  = 1;
constexpr std::uint32_t TestBSPModelHulls        = 4;

//Sample colors written for the lit faces.
constexpr std::array<std::uint8_t, 3> TestBSPFloorSample  = {10, 20, 30};
constexpr std::array<std::uint8_t, 3> TestBSPFloor2Sample = {40, 50, 60};
constexpr std::array<std::uint8_t, 3> TestBSPFloor2Style  = {70, 80, 90};

//Record sizes and offsets the tests patch.
constexpr std::uint32_t TestBSPFaceBytes       = 20;
constexpr std::uint32_t TestBSPTexInfoBytes    = 40;
constexpr std::uint32_t TestBSPNodeBytes       = 24;
constexpr std::uint32_t TestBSPLeafBytes       = 28;
constexpr std::uint32_t TestBSPFaceEdgesOffset = 8;
constexpr std::uint32_t TestBSPFaceInfoOffset  = 10;
constexpr std::uint32_t TestBSPFaceStyleOffset = 12;
constexpr std::uint32_t TestBSPFaceLightOffset = 16;
constexpr std::uint32_t TestBSPNodeChildOffset = 4;
constexpr std::uint32_t TestBSPLeafVisOffset   = 4;
constexpr std::uint32_t TestBSPPlaneBytes      = 20;

//Entity text, terminator included. Its length is not a whole number of plane records, so a file whose layout is
//mistaken for the other one is rejected.
constexpr char TestBSPEntities[] = "{\n\"classname\" \"worldspawn\"\n}\n";
static_assert(sizeof(TestBSPEntities) % TestBSPPlaneBytes != 0, "the entity text must not pass as planes");

class CBSPWriter
{
public:
    void UInt8(std::uint8_t value)
    {
        m_Bytes.push_back(value);
    }

    void Int16(std::int16_t value)
    {
        UInt16(static_cast<std::uint16_t>(value));
    }

    void UInt16(std::uint16_t value)
    {
        for (std::size_t i = 0; i < sizeof(value); ++i)
            m_Bytes.push_back(static_cast<std::uint8_t>(value >> (i * CHAR_BIT)));
    }

    void Int32(std::int32_t value)
    {
        UInt32(static_cast<std::uint32_t>(value));
    }

    void UInt32(std::uint32_t value)
    {
        for (std::size_t i = 0; i < sizeof(value); ++i)
            m_Bytes.push_back(static_cast<std::uint8_t>(value >> (i * CHAR_BIT)));
    }

    void Float(float value)
    {
        std::uint32_t bits;
        std::memcpy(&bits, &value, sizeof(bits));
        UInt32(bits);
    }

    void Vector(float x, float y, float z)
    {
        Float(x);
        Float(y);
        Float(z);
    }

    void Name(const char* szName, std::size_t size)
    {
        for (std::size_t i = 0; i < size; ++i)
            m_Bytes.push_back(i < std::strlen(szName) ? static_cast<std::uint8_t>(szName[i]) : 0);
    }

    std::vector<std::uint8_t>& Bytes()
    {
        return m_Bytes;
    }

private:
    std::vector<std::uint8_t> m_Bytes;
};

inline void PatchUInt16(std::vector<std::uint8_t>& bytes, std::size_t offset, std::uint16_t value)
{
    for (std::size_t i = 0; i < sizeof(value); ++i)
        bytes[offset + i] = static_cast<std::uint8_t>(value >> (i * CHAR_BIT));
}

inline void PatchUInt32(std::vector<std::uint8_t>& bytes, std::size_t offset, std::uint32_t value)
{
    for (std::size_t i = 0; i < sizeof(value); ++i)
        bytes[offset + i] = static_cast<std::uint8_t>(value >> (i * CHAR_BIT));
}

using BSPTestLumps = std::array<std::vector<std::uint8_t>, BSPLumpCount>;

inline BSPTestLumps MakeTestBSPLumps()
{
    BSPTestLumps lumps;

    lumps[BSPEntityLump].assign(TestBSPEntities, TestBSPEntities + sizeof(TestBSPEntities));

    {
        CBSPWriter planes;
        planes.Vector(0, 0, 1);
        planes.Float(0);
        planes.Int32(2);
        planes.Vector(1, 0, 0);
        planes.Float(TestBSPSplitDistance);
        planes.Int32(0);
        lumps[BSPPlaneLump] = std::move(planes.Bytes());
    }

    {
        const char* names[TestBSPMiptexCount] = {"floor", "sky", "!water", "wall", "{grate", "floor2", "trigger"};

        CBSPWriter textures;
        textures.Int32(TestBSPMiptexCount);

        const auto tableBytes = sizeof(std::int32_t) * (1 + TestBSPMiptexCount);

        for (std::uint32_t i = 0; i < TestBSPMiptexCount; ++i)
            textures.Int32(static_cast<std::int32_t>(tableBytes + i * TestBSPMiptexBytes));

        for (std::uint32_t i = 0; i < TestBSPMiptexCount; ++i)
        {
            textures.Name(names[i], TestBSPMiptexNameBytes);
            textures.UInt32(i == TestBSPFloorSlot ? TestBSPFloorWidth : TestBSPTextureSize);
            textures.UInt32(i == TestBSPFloorSlot ? TestBSPFloorHeight : TestBSPTextureSize);

            //No embedded pixels.
            for (std::uint32_t mip = 0; mip < 4; ++mip)
                textures.UInt32(0);
        }

        lumps[BSPTextureLump] = std::move(textures.Bytes());
    }

    {
        CBSPWriter vertices;
        vertices.Vector(0, 0, 0);
        vertices.Vector(TestBSPQuadSize, 0, 0);
        vertices.Vector(TestBSPQuadSize, TestBSPQuadSize, 0);
        vertices.Vector(0, TestBSPQuadSize, 0);
        lumps[BSPVertexLump] = std::move(vertices.Bytes());
    }

    //Rows of one byte: leaf 1 sees leaf 1, leaf 2 sees leaf 2.
    lumps[BSPVisibilityLump] = {0x01, 0x02};

    {
        CBSPWriter nodes;

        //Node 0: x > 1000 is leaf 2, the rest node 1.
        nodes.Int32(1);
        nodes.Int16(-3);
        nodes.Int16(1);
        for (std::uint32_t i = 0; i < 6; ++i)
            nodes.Int16(0);
        nodes.UInt16(0);
        nodes.UInt16(0);

        //Node 1: z > 0 is leaf 1, the rest the solid leaf 0; it owns every face.
        nodes.Int32(0);
        nodes.Int16(-2);
        nodes.Int16(-1);
        for (std::uint32_t i = 0; i < 6; ++i)
            nodes.Int16(0);
        nodes.UInt16(0);
        nodes.UInt16(TestBSPFaceCount);

        lumps[BSPNodeLump] = std::move(nodes.Bytes());
    }

    {
        CBSPWriter texInfos;

        for (std::uint32_t i = 0; i < TestBSPMiptexCount; ++i)
        {
            const float vectors[2][4] = {{1, 0, 0, 0}, {0, 1, 0, 0}};

            for (const auto& vector : vectors)
            {
                for (const auto value : vector)
                    texInfos.Float(value);
            }

            const bool bSpecial = i == TestBSPSkySlot || i == TestBSPWaterSlot || i == TestBSPTriggerSlot;

            texInfos.Int32(static_cast<std::int32_t>(i));
            texInfos.Int32(bSpecial ? TestBSPSpecialFlag : 0);
        }

        lumps[BSPTexInfoLump] = std::move(texInfos.Bytes());
    }

    {
        const std::uint32_t slots[TestBSPFaceCount] = {TestBSPFloorSlot, TestBSPSkySlot, TestBSPWaterSlot, TestBSPWallSlot, TestBSPGrateSlot, TestBSPFloorSlot, TestBSPFloor2Slot, TestBSPTriggerSlot};

        const auto layerBytes = static_cast<std::int32_t>(TestBSPFaceLuxels * TestBSPSampleBytes);

        CBSPWriter faces;

        for (std::uint32_t i = 0; i < TestBSPFaceCount; ++i)
        {
            const bool bFloor  = i == 0;
            const bool bFloor2 = slots[i] == TestBSPFloor2Slot;

            faces.UInt16(0);
            faces.Int16(0);
            faces.Int32(0);
            faces.UInt16(i == TestBSPDegenerateFace ? 2 : 4);
            faces.UInt16(static_cast<std::uint16_t>(slots[i]));
            faces.UInt8(bFloor || bFloor2 ? 0 : TestBSPNoStyle);
            faces.UInt8(bFloor2 ? TestBSPAnimatedStyle : TestBSPNoStyle);
            faces.UInt8(TestBSPNoStyle);
            faces.UInt8(TestBSPNoStyle);
            faces.Int32(bFloor ? 0 : bFloor2 ? layerBytes :
                                               TestBSPNoOffset);
        }

        lumps[BSPFaceLump] = std::move(faces.Bytes());
    }

    {
        CBSPWriter lighting;

        const std::array<std::uint8_t, 3>* layers[] = {&TestBSPFloorSample, &TestBSPFloor2Sample, &TestBSPFloor2Style};

        for (const auto* pLayer : layers)
        {
            for (std::uint32_t i = 0; i < TestBSPFaceLuxels; ++i)
            {
                for (const auto value : *pLayer)
                    lighting.UInt8(value);
            }
        }

        lumps[BSPLightingLump] = std::move(lighting.Bytes());
    }

    {
        CBSPWriter leaves;

        const std::int32_t  contents[] = {TestBSPSolidContents, TestBSPEmptyContents, TestBSPEmptyContents};
        const std::int32_t  offsets[]  = {TestBSPNoOffset, 0, 1};
        const std::uint16_t marks[]    = {0, TestBSPFaceCount, 0};

        for (std::uint32_t i = 0; i < 3; ++i)
        {
            leaves.Int32(contents[i]);
            leaves.Int32(offsets[i]);
            for (std::uint32_t j = 0; j < 6; ++j)
                leaves.Int16(0);
            leaves.UInt16(0);
            leaves.UInt16(marks[i]);
            leaves.UInt32(0);
        }

        lumps[BSPLeafLump] = std::move(leaves.Bytes());
    }

    {
        CBSPWriter marks;
        for (std::uint16_t i = 0; i < TestBSPFaceCount; ++i)
            marks.UInt16(i);
        lumps[BSPMarkSurfaceLump] = std::move(marks.Bytes());
    }

    {
        CBSPWriter          edges;
        const std::uint16_t corners[][2] = {{0, 0}, {0, 1}, {1, 2}, {2, 3}, {3, 0}};
        for (const auto& edge : corners)
        {
            edges.UInt16(edge[0]);
            edges.UInt16(edge[1]);
        }
        lumps[BSPEdgeLump] = std::move(edges.Bytes());
    }

    {
        CBSPWriter surfEdges;
        for (std::int32_t i = 1; i <= 4; ++i)
            surfEdges.Int32(i);
        lumps[BSPSurfEdgeLump] = std::move(surfEdges.Bytes());
    }

    {
        CBSPWriter models;
        models.Vector(0, 0, 0);
        models.Vector(TestBSPQuadSize, TestBSPQuadSize, 0);
        models.Vector(0, 0, 0);
        models.Int32(0);
        for (std::uint32_t i = 1; i < 4; ++i)
            models.Int32(0);
        models.Int32(TestBSPVisibleLeaves);
        models.Int32(0);
        models.Int32(TestBSPFaceCount);
        lumps[BSPModelLump] = std::move(models.Bytes());
    }

    return lumps;
}

//Appends submodel 1, with the bounds of the quad, its own head node and no visible leaves.
inline void AddTestBSPSubmodel(BSPTestLumps& lumps)
{
    CBSPWriter model;
    model.Vector(0, 0, 0);
    model.Vector(TestBSPQuadSize, TestBSPQuadSize, 0);
    model.Vector(0, 0, 0);
    model.Int32(TestBSPSubmodelHeadNode);
    for (std::uint32_t i = 1; i < TestBSPModelHulls; ++i)
        model.Int32(0);
    model.Int32(0);
    model.Int32(TestBSPSubmodelFirstFace);
    model.Int32(TestBSPSubmodelFaceCount);

    lumps[BSPModelLump].insert(lumps[BSPModelLump].end(), model.Bytes().begin(), model.Bytes().end());
}

//Lays the lumps out after the header in table order. lumps are indexed by meaning; a BlueShift file stores the
//entities in the planes' table entry and the planes in the entities' entry.
inline std::vector<std::uint8_t> AssembleTestBSP(const BSPTestLumps& lumps, BSPLayout layout = BSPLayout::GoldSrc)
{
    std::vector<std::uint8_t> bytes(BSPHeaderBytes);

    PatchUInt32(bytes, 0, BSPVersion);

    for (std::uint32_t i = 0; i < BSPLumpCount; ++i)
    {
        auto lump = i;

        if (layout == BSPLayout::BlueShift && i == BSPEntityLump)
            lump = BSPPlaneLump;
        else if (layout == BSPLayout::BlueShift && i == BSPPlaneLump)
            lump = BSPEntityLump;

        const auto entry = BSPLumpTableOffset + i * BSPLumpEntryBytes;

        PatchUInt32(bytes, entry, static_cast<std::uint32_t>(bytes.size()));
        PatchUInt32(bytes, entry + BSPLumpLengthOffset, static_cast<std::uint32_t>(lumps[lump].size()));

        bytes.insert(bytes.end(), lumps[lump].begin(), lumps[lump].end());
    }

    return bytes;
}

inline std::vector<std::uint8_t> MakeTestBSP(BSPLayout layout = BSPLayout::GoldSrc)
{
    return AssembleTestBSP(MakeTestBSPLumps(), layout);
}

//The test world with submodel 1 (see AddTestBSPSubmodel).
inline std::vector<std::uint8_t> MakeTestBSPWithSubmodel(BSPLayout layout = BSPLayout::GoldSrc)
{
    auto lumps = MakeTestBSPLumps();
    AddTestBSPSubmodel(lumps);

    return AssembleTestBSP(lumps, layout);
}

//Bindings of the test world's bound slots to textures firstTexture, firstTexture + 1, ...
inline std::vector<TextureBinding> MakeTestBSPBindings(AssetId firstTexture)
{
    const std::uint32_t slots[] = {TestBSPFloorSlot, TestBSPWaterSlot, TestBSPGrateSlot, TestBSPFloor2Slot, TestBSPTriggerSlot};

    std::vector<TextureBinding> bindings;

    for (const auto slot : slots)
        bindings.push_back({slot, firstTexture + bindings.size()});

    return bindings;
}
} // namespace GoldSrcRenderTest
