#include "GoldSrcRenderBSPTestFile.h"
#include "GoldSrcRenderMDLTestFile.h"
#include "GoldSrcRenderCoreTestUtil.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

//BSP and MDL reading of GoldSrcRenderCore on generated test files, without any bridge.
using namespace GoldSrcRender;
using namespace GoldSrcRenderTest;

static void Require(bool bCondition, const char* pMessage)
{
    if (!bCondition)
    {
        std::cerr << pMessage << '\n';
        std::exit(EXIT_FAILURE);
    }
}

static bool SameLuxel(const BSPWorld& world, std::uint32_t layer, std::uint32_t x, std::uint32_t y, const std::array<std::uint8_t, 3>& color)
{
    const auto* pLuxel = world.lightmaps[layer].data() + (static_cast<std::size_t>(y) * BSPLightmapPageSize + x) * RGBABytes;

    return pLuxel[0] == color[0] && pLuxel[1] == color[1] && pLuxel[2] == color[2] && pLuxel[3] == 255;
}

static std::uint32_t CountVisible(const BSPWorld& world, std::uint32_t leaf)
{
    std::vector<std::uint8_t> visible;
    std::vector<std::uint8_t> visibleWater;
    FindBSPVisibleSurfaces(world, leaf, visible, visibleWater);

    Require(visible.size() == world.surfaces.size(), "one visibility flag per surface");
    Require(visibleWater.size() == world.waterSurfaces.size(), "one visibility flag per water surface");

    return static_cast<std::uint32_t>(std::count(visible.begin(), visible.end(), 1));
}

static std::uint32_t CountVisibleWater(const BSPWorld& world, std::uint32_t leaf)
{
    std::vector<std::uint8_t> visible;
    std::vector<std::uint8_t> visibleWater;
    FindBSPVisibleSurfaces(world, leaf, visible, visibleWater);

    return static_cast<std::uint32_t>(std::count(visibleWater.begin(), visibleWater.end(), 1));
}

//Water vertices and indices follow those of every drawn surface: the first water index is the first water vertex.
static std::size_t CountDrawnIndices(const BSPWorld& world)
{
    return world.waterSurfaces.empty() ? world.indices.size() : world.waterSurfaces.front().firstIndex;
}

static std::size_t CountDrawnVertices(const BSPWorld& world)
{
    return world.waterSurfaces.empty() ? world.positions.size() : world.indices[world.waterSurfaces.front().firstIndex];
}

//Index of slot's binding in bindings, BSPMissingTextureBinding without one: what texture groups keep.
static std::uint32_t FindTestBinding(const std::vector<TextureBinding>& bindings, std::uint32_t slot)
{
    const auto itor = std::find_if(bindings.begin(), bindings.end(), [slot](const TextureBinding& binding) { return binding.slot == slot; });

    return itor == bindings.end() ? BSPMissingTextureBinding : static_cast<std::uint32_t>(itor - bindings.begin());
}

//Builds the test world: only opaque faces are drawn, grouped by texture, with their luxels in every lightstyle layer;
//the TEX_SPECIAL trigger is drawn too, without lightmap and tiled, as GoldSrc's Mod_LoadFaces and
//GL_CreateSurfaceLightmap treat it; visibility follows the leaves' rows.
static void TestBSPWorldBuild()
{
    using namespace GoldSrcRenderTest;

    const auto                        file     = MakeTestBSP();
    const auto                        bindings = MakeTestBSPBindings(1);
    const std::vector<TextureBinding> unbound;

    BSPWorld world;
    Require(BuildBSPWorld(TestBSPAsset(file, bindings), world) == ErrorCode::None, "build the test world");

    //The masked {grate is drawn like the opaque faces; its texture decides which texels are transparent.
    Require(world.surfaces.size() == 5 && world.textureGroups.size() == 5, "five drawn faces in five groups");
    Require(world.submodels.size() == 1 && world.submodels[0].surfaceCount == 5 && world.submodels[0].textureGroupCount == 5, "the world is submodel 0");
    Require(world.surfaces[0].slot == TestBSPFloorSlot && world.surfaces[1].slot == TestBSPGrateSlot && world.surfaces[2].slot == TestBSPFloor2Slot && world.surfaces[3].slot == TestBSPTriggerSlot && world.surfaces[4].slot == BSPMissingTextureSlot, "surfaces sorted by slot, the missing texture last");
    Require(world.skipped.sky == 1 && world.skipped.degenerate == 1, "unsupported faces are counted by category");

    //The trigger keeps page 0 at 0, 0 and scrolls at its miptex width; only its group is unlit, and no other face
    //scrolls.
    const auto* pTriggerFace = FindBSPSubmodelFace(world, 0, TestBSPTriggerFace);
    Require(world.surfaces[3].lightmapPage == 0 && pTriggerFace && pTriggerFace->surface == 3 && pTriggerFace->lightmapX == 0 && pTriggerFace->lightmapY == 0, "the trigger has no lightmap placement");
    Require(world.textureGroups[3].bUnlit && world.textureGroups[3].scrollWidth == TestBSPTextureSize, "the trigger's group is unlit and tiled");

    for (std::size_t i = 0; i < world.textureGroups.size(); ++i)
        Require(i == 3 || (!world.textureGroups[i].bUnlit && !world.textureGroups[i].scrollWidth), "the other groups are lit and still");

    //The !water face is a water surface, whatever its TEX_SPECIAL flag: one 64x64 piece on the horizontal plane z = 0,
    //unlit, after the drawn surfaces.
    Require(world.waterSurfaces.size() == 1 && world.waterGroups.size() == 1, "one water surface in one group");
    Require(world.submodels[0].firstWaterSurface == 0 && world.submodels[0].waterSurfaceCount == 1 && world.submodels[0].firstWaterGroup == 0 && world.submodels[0].waterGroupCount == 1, "the world owns the water");

    const auto& waterGroup = world.waterGroups[0];
    Require(waterGroup.slot == TestBSPWaterSlot && waterGroup.binding == FindTestBinding(bindings, TestBSPWaterSlot) && waterGroup.firstSurface == 0 && waterGroup.surfaceCount == 1, "the water group keeps its slot's binding");
    Require(waterGroup.normal.z == 1.0f && waterGroup.dist == 0.0f && !waterGroup.bBack && waterGroup.bHorizontal, "the water group keeps its plane");

    //Only the water face maps to the water surface; the other faces have none.
    for (std::uint32_t face = 0; face < TestBSPFaceCount; ++face)
    {
        const auto* pFace = FindBSPSubmodelFace(world, 0, face);
        Require(pFace && pFace->waterSurface == (face == TestBSPWaterFace ? 0 : BSPNoSurface), "faces map to their water surface");
    }

    const auto& water      = world.waterSurfaces[0];
    const auto  waterFirst = world.indices[water.firstIndex];
    Require(water.indexCount == 6 && water.firstPieceIndexCount == 6 && water.reversedFirstIndex == water.firstIndex + water.indexCount && water.referenceHeight == 0.0f, "one piece, forward then reversed");
    Require(world.indices[water.firstIndex + 1] == waterFirst + 1 && world.indices[water.firstIndex + 2] == waterFirst + 2, "the forward fan starts at the first corner");
    Require(world.indices[water.reversedFirstIndex] == waterFirst + 3 && world.indices[water.reversedFirstIndex + 1] == waterFirst + 2 && world.indices[water.reversedFirstIndex + 2] == waterFirst + 1, "the reversed fan starts at the last corner");
    Require(world.texcoords[waterFirst + 2].x == 64.0f && world.texcoords[waterFirst + 2].y == 64.0f, "water keeps raw texture coordinates for the warp");
    Require(world.lightmapTexcoords[waterFirst + 2].x == 0.0f && world.normals[waterFirst].z == 1.0f, "water has no lightmap coordinates and keeps its normal");
    Require(CountDrawnIndices(world) == 30 && CountDrawnVertices(world) == 20, "water follows the drawn surfaces");

    //Each group keeps the index of its slot's binding, so receivers do not search the bindings. The test bindings skip
    //unbound slots, so binding indices and slots differ.
    const std::uint32_t groupSlots[] = {TestBSPFloorSlot, TestBSPGrateSlot, TestBSPFloor2Slot, TestBSPTriggerSlot, BSPMissingTextureSlot};

    for (std::size_t i = 0; i < world.textureGroups.size(); ++i)
        Require(world.textureGroups[i].slot == groupSlots[i] && world.textureGroups[i].binding == FindTestBinding(bindings, groupSlots[i]), "groups keep their slot's binding");

    Require(world.textureGroups[1].binding != world.textureGroups[1].slot, "a binding index is not a slot");

    //Bindings in another order change only the groups' binding indices.
    const std::vector<TextureBinding> reversed(bindings.rbegin(), bindings.rend());

    BSPWorld reordered;
    Require(BuildBSPWorld(TestBSPAsset(file, reversed), reordered) == ErrorCode::None, "build with reversed bindings");
    Require(SameRecords(world.surfaces, reordered.surfaces) && reordered.textureGroups.size() == world.textureGroups.size(), "the binding order does not change the surfaces");

    for (std::size_t i = 0; i < reordered.textureGroups.size(); ++i)
        Require(reordered.textureGroups[i].slot == groupSlots[i] && reordered.textureGroups[i].binding == FindTestBinding(reversed, groupSlots[i]), "groups follow the bindings' order");

    Require(world.positions.size() == 24 && world.indices.size() == 42, "two triangles per quad, twice for water");
    Require(world.normals.size() == 24 && world.sTangents.size() == 24 && world.tTangents.size() == 24 && world.texcoords.size() == 24 && world.lightmapTexcoords.size() == 24, "every vertex attribute per vertex");

    const auto& grate       = world.surfaces[1];
    const auto  grateVertex = world.indices[grate.firstIndex] + 2;
    Require(grate.indexCount == 6 && world.texcoords[grateVertex].x == 4.0f && world.texcoords[grateVertex].y == 4.0f, "the masked face has its geometry and 16x16 texture coordinates");
    Require(grate.styles[0] == TestBSPNoStyle && grate.lightmapPage == 0, "the masked face keeps its lightmap placement");

    const auto& floor = world.surfaces[0];
    Require(floor.indexCount == 6 && world.indices[floor.firstIndex] + 1 == world.indices[floor.firstIndex + 1], "a fan starting at the first corner");

    //Texture coordinates divide by the miptex size; corner 2 is (64, 64).
    const auto first  = world.indices[floor.firstIndex];
    const auto corner = first + 2;
    Require(world.texcoords[corner].x == 1.0f && world.texcoords[corner].y == 2.0f, "floor texture coordinates use its 64x32 miptex");
    Require(world.normals[corner].z == 1.0f && world.sTangents[corner].x == 1.0f && world.tTangents[corner].y == 1.0f, "plane normal and texture axes");
    Require(world.lightmapTexcoords[first].x == 8.0f / 2048.0f && world.lightmapTexcoords[corner].x == 72.0f / 2048.0f, "lightmap coordinates sample luxel centers");

    //Faces are packed in face order: floor (0, 0), wall (5, 0), {grate (10, 0), floor2 (15, 0).
    const std::array<std::uint8_t, 3> black{};
    Require(world.lightmapPages == 1 && world.usedStyleLayers == 3, "one page, two lightstyle layers used");
    Require(SameLuxel(world, 0, 0, 0, TestBSPFloorSample) && SameLuxel(world, 1, 0, 0, black), "floor samples in its first layer only");
    Require(SameLuxel(world, 0, 5, 0, black) && SameLuxel(world, 1, 5, 0, black), "a face without samples is black");
    Require(SameLuxel(world, 0, 14, 4, black), "the masked face without samples is black");
    Require(SameLuxel(world, 0, 19, 4, TestBSPFloor2Sample) && SameLuxel(world, 1, 19, 4, TestBSPFloor2Style), "floor2 samples in two layers");
    Require(world.lightmaps[2].empty() && world.lightmaps[3].empty(), "layers no face has samples for are not allocated");
    Require(world.surfaces[2].styles[1] == TestBSPAnimatedStyle && world.surfaces[2].styles[2] == TestBSPNoStyle, "surfaces keep their lightstyles");
    Require(!world.bFullbright, "a lit world is not fully bright");

    //An unlit face takes no room: with the floor unlit, the faces after it are packed from the corner.
    auto lumps = MakeTestBSPLumps();
    PatchUInt16(lumps[BSPFaceLump], TestBSPFaceInfoOffset, static_cast<std::uint16_t>(TestBSPTriggerSlot));
    const auto unlitFloor = AssembleTestBSP(lumps);

    BSPWorld packed;
    Require(BuildBSPWorld(TestBSPAsset(unlitFloor, bindings), packed) == ErrorCode::None, "build with an unlit first face");
    Require(SameLuxel(packed, 0, 10, 0, TestBSPFloor2Sample) && SameLuxel(packed, 0, 0, 0, black) && packed.usedStyleLayers == 3, "the unlit face is skipped by the packing and its samples are not read");

    //With floor2 down to one lightstyle, only the first layer is allocated; its luxels are those of the full world.
    constexpr std::uint32_t secondStyle = 1;

    lumps = MakeTestBSPLumps();

    lumps[BSPFaceLump][TestBSPFloor2Face * TestBSPFaceBytes + TestBSPFaceStyleOffset + secondStyle] = TestBSPNoStyle;

    const auto oneStyle = AssembleTestBSP(lumps);

    BSPWorld single;
    Require(BuildBSPWorld(TestBSPAsset(oneStyle, bindings), single) == ErrorCode::None && single.usedStyleLayers == 1, "build with one lightstyle layer used");
    Require(single.lightmapPages == world.lightmapPages && single.lightmaps[0] == world.lightmaps[0], "the used layer keeps its luxels");
    Require(single.lightmaps[1].empty() && single.lightmaps[2].empty() && single.lightmaps[3].empty(), "the unused layers are not allocated");

    //A lit world whose faces have no samples allocates no layer, but keeps its lightmap placement.
    lumps = MakeTestBSPLumps();
    PatchUInt32(lumps[BSPFaceLump], TestBSPFloorFace * TestBSPFaceBytes + TestBSPFaceLightOffset, static_cast<std::uint32_t>(TestBSPNoOffset));
    PatchUInt32(lumps[BSPFaceLump], TestBSPFloor2Face * TestBSPFaceBytes + TestBSPFaceLightOffset, static_cast<std::uint32_t>(TestBSPNoOffset));
    const auto noSamples = AssembleTestBSP(lumps);

    BSPWorld dark;
    Require(BuildBSPWorld(TestBSPAsset(noSamples, bindings), dark) == ErrorCode::None && !dark.bFullbright && !dark.usedStyleLayers, "build a lit world without samples");
    Require(dark.lightmapPages == world.lightmapPages && SameRecords(dark.surfaces, world.surfaces), "the faces keep their lightmap placement");
    Require(std::all_of(dark.lightmaps.begin(), dark.lightmaps.end(), [](const std::vector<std::uint8_t>& layer) { return layer.empty(); }), "no layer is allocated");

    Require(FindBSPLeaf(world, {1, 1, 10}) == 1 && FindBSPLeaf(world, {2000, 0, 0}) == 2 && FindBSPLeaf(world, {1, 1, -10}) == 0, "leaves follow the world tree");
    Require(CountVisible(world, 1) == 5, "leaf 1 sees its faces, the masked and unlit ones included");
    Require(CountVisible(world, 2) == 0, "leaf 2 sees none of them");
    Require(CountVisible(world, BSPSolidLeaf) == 5, "the solid leaf sees everything");
    Require(CountVisibleWater(world, 1) == 1 && CountVisibleWater(world, 2) == 0 && CountVisibleWater(world, BSPSolidLeaf) == 1, "water follows the same visibility");

    lumps = MakeTestBSPLumps();
    lumps[BSPVisibilityLump].clear();
    lumps[BSPLightingLump].clear();
    const auto unlit = AssembleTestBSP(lumps);

    Require(BuildBSPWorld(TestBSPAsset(unlit, bindings), world) == ErrorCode::None && world.bFullbright && !world.usedStyleLayers, "a world without lighting is fully bright");
    Require(world.lightmapPages && std::all_of(world.lightmaps.begin(), world.lightmaps.end(), [](const std::vector<std::uint8_t>& layer) { return layer.empty(); }), "a fully bright world keeps its pages but allocates no layer");
    Require(CountVisible(world, 2) == 5, "without visibility every leaf sees everything");

    //Unbound textures are missing, except sky, which stays skipped.
    Require(BuildBSPWorld(TestBSPAsset(file, unbound), world) == ErrorCode::None, "build without bindings");
    Require(world.textureGroups.size() == 1 && world.textureGroups[0].slot == BSPMissingTextureSlot && world.textureGroups[0].binding == BSPMissingTextureBinding && world.skipped.sky == 1, "unbound faces use the missing texture");
    Require(world.surfaces.size() == 6 && world.waterSurfaces.empty() && !world.textureGroups[0].bUnlit, "unbound faces lose their category and TEX_SPECIAL flag");

    const std::vector<std::uint8_t> truncated(file.begin(), file.end() - 1);
    Require(BuildBSPWorld(TestBSPAsset(truncated, bindings), world) == ErrorCode::InvalidData && world.surfaces.empty(), "an invalid file builds nothing");
}

//Texture groups whose miptex name starts with "scroll", matched with case as GoldSrc's Mod_LoadFaces does, keep the
//miptex width; the others have none. GetBSPScrollOffset wraps speed times time into (-width, width), keeping the sign
//of the motion, exactly even at a long scene time, and gives 0 without width or for inputs that are not finite.
static void TestBSPScrollTextures()
{
    using namespace GoldSrcRenderTest;

    static constexpr char s_szScrollName[]    = "scroll";
    static constexpr char s_szNotScrollName[] = "Scroll";
    static constexpr char s_szFloor2Name[]    = "floor2";

    static_assert(sizeof(s_szScrollName) == sizeof(s_szFloor2Name) && sizeof(s_szNotScrollName) == sizeof(s_szFloor2Name), "the renamed miptex keeps its name length");

    const auto bindings = MakeTestBSPBindings(1);

    //The test file with floor2 renamed, so its faces keep their texture coordinates and binding.
    const auto buildWorld = [&bindings](const char* szName, BSPWorld& world) {
        auto       file  = MakeTestBSP();
        const auto pName = std::search(file.begin(), file.end(), std::begin(s_szFloor2Name), std::end(s_szFloor2Name));
        Require(pName != file.end(), "find the floor2 miptex name");

        std::copy(szName, szName + sizeof(s_szFloor2Name), pName);
        Require(BuildBSPWorld(TestBSPAsset(file, bindings), world) == ErrorCode::None, "build the world with a renamed miptex");
    };

    BSPWorld world;
    buildWorld(s_szScrollName, world);

    const std::uint32_t groupSlots[]        = {TestBSPFloorSlot, TestBSPGrateSlot, TestBSPFloor2Slot, TestBSPTriggerSlot, BSPMissingTextureSlot};
    const std::uint32_t groupScrollWidths[] = {0, 0, TestBSPTextureSize, TestBSPTextureSize, 0};

    Require(world.textureGroups.size() == std::size(groupSlots), "scrolling textures do not change the groups");

    for (std::size_t i = 0; i < world.textureGroups.size(); ++i)
        Require(world.textureGroups[i].slot == groupSlots[i] && world.textureGroups[i].scrollWidth == groupScrollWidths[i], "only the scroll and TEX_SPECIAL groups keep their miptex width");

    BSPWorld caseWorld;
    buildWorld(s_szNotScrollName, caseWorld);

    for (const auto& group : caseWorld.textureGroups)
        Require(group.scrollWidth == (group.bUnlit ? TestBSPTextureSize : 0), "a name matching scroll only without case does not scroll");

    constexpr float         s_Speed          = 24;
    constexpr double        s_Time           = 1.5;
    constexpr std::uint32_t s_Width          = 16;
    constexpr float         s_WrappedOffset  = 4;
    constexpr double        s_LongTime       = 1e7 + 0.25;
    constexpr float         s_LongTimeOffset = 6;

    Require(GetBSPScrollOffset(s_Speed, s_Time, s_Width) == s_WrappedOffset, "a forward offset wraps at the width");
    Require(GetBSPScrollOffset(-s_Speed, s_Time, s_Width) == -s_WrappedOffset, "a backward offset keeps its sign");
    Require(GetBSPScrollOffset(s_Speed, s_LongTime, s_Width) == s_LongTimeOffset, "a long scene time keeps an exact offset");
    Require(GetBSPScrollOffset(s_Speed, s_Time, 0) == 0, "no width does not scroll");
    Require(GetBSPScrollOffset(s_Speed, std::numeric_limits<double>::quiet_NaN(), s_Width) == 0, "a time that is not a number does not scroll");
    Require(GetBSPScrollOffset(s_Speed, std::numeric_limits<double>::infinity(), s_Width) == 0, "an infinite time does not scroll");
    Require(GetBSPScrollOffset(std::numeric_limits<float>::infinity(), s_Time, s_Width) == 0, "an infinite speed does not scroll");
}

//Bound miptexes named '+' or '-', a frame character and a shared rest of the name are sequenced as GoldSrc's
//Mod_LoadTextures does: frames '0' to '9' and alternate frames 'A' to 'J' (without case), in frame order, a rest of the
//name compared with case. Texture groups of a sequence show all its frames in turn, ten a second, and the other
//sequence in the alternate state; a name group with a missing frame or another frame character, and unbound slots, are
//not animated. Random tiling ('-') groups show the one frame GetBSPRandomTextureFrame picks for their surface, the same
//in every build; its cells repeat every 20 texture sizes and stay in range for negative lightmap origins.
static void TestBSPAnimatedTextures()
{
    using namespace GoldSrcRenderTest;

    using Renames = std::initializer_list<std::pair<const char*, const char*>>;

    constexpr std::size_t s_FloorGroup  = 0;
    constexpr std::size_t s_GrateGroup  = 1;
    constexpr std::size_t s_Floor2Group = 2;

    const auto bindings      = MakeTestBSPBindings(1);
    const auto floorBinding  = FindTestBinding(bindings, TestBSPFloorSlot);
    const auto grateBinding  = FindTestBinding(bindings, TestBSPGrateSlot);
    const auto floor2Binding = FindTestBinding(bindings, TestBSPFloor2Slot);

    //The test file with miptexes renamed, each new name written over the whole name of the miptex it replaces, so
    //their faces keep their texture coordinates and bindings.
    const auto buildWorld = [&bindings](Renames renames, BSPWorld& world) {
        auto file = MakeTestBSP();

        for (const auto& [szName, szNewName] : renames)
        {
            const auto pName = std::search(file.begin(), file.end(), szName, szName + std::strlen(szName) + 1);
            Require(pName != file.end(), "find the renamed miptex name");

            std::fill_n(pName, TestBSPMiptexNameBytes, 0);
            std::copy(szNewName, szNewName + std::strlen(szNewName), pName);
        }

        Require(BuildBSPWorld(TestBSPAsset(file, bindings), world) == ErrorCode::None, "build the world with renamed miptexes");
    };

    const auto sameAnimation = [](const BSPTextureAnimation& animation, std::uint32_t firstFrame, std::uint32_t frameCount) {
        return animation.firstFrame == firstFrame && animation.frameCount == frameCount;
    };

    const auto isStill = [&sameAnimation](const BSPWorld& world) {
        return world.textureFrames.empty() && std::all_of(world.textureGroups.begin(), world.textureGroups.end(), [&sameAnimation](const BSPWorldTextureGroup& group) {
                   return sameAnimation(group.animation, 0, 0) && sameAnimation(group.alternateAnimation, 0, 0);
               });
    };

    constexpr std::size_t   s_GroupCount      = 5;
    constexpr double        s_FirstFrameTime  = 0.05;
    constexpr double        s_SecondFrameTime = 0.15;
    constexpr double        s_WrappedTime     = 0.25;
    constexpr double        s_LongTime        = 1e7 + 0.15;
    constexpr std::uint32_t s_SequenceFrames  = 2;
    constexpr std::size_t   s_WithAlternate   = 3;
    constexpr std::uint32_t s_AlternateFirst  = 2;

    BSPWorld world;
    buildWorld({{"floor", "+0lab"}, {"floor2", "+1lab"}}, world);

    Require(world.textureGroups.size() == s_GroupCount && world.textureFrames.size() == s_SequenceFrames, "one sequence of two frames, the groups unchanged");
    Require(world.textureFrames[0].binding == floorBinding && world.textureFrames[1].binding == floor2Binding, "frames follow their frame characters");

    for (const auto group : {s_FloorGroup, s_Floor2Group})
        Require(sameAnimation(world.textureGroups[group].animation, 0, s_SequenceFrames) && sameAnimation(world.textureGroups[group].alternateAnimation, 0, s_SequenceFrames), "both frames show the sequence, the same without alternate frames");

    Require(sameAnimation(world.textureGroups[s_GrateGroup].animation, 0, 0), "other slots are not animated");

    const auto& floorGroup = world.textureGroups[s_FloorGroup];

    Require(SelectBSPTextureFrame(world, floorGroup, false, s_FirstFrameTime).binding == floorBinding, "the first tenth of a second shows frame 0");
    Require(SelectBSPTextureFrame(world, floorGroup, false, s_SecondFrameTime).binding == floor2Binding, "the second shows frame 1");
    Require(SelectBSPTextureFrame(world, floorGroup, false, s_WrappedTime).binding == floorBinding, "the frames start over");
    Require(SelectBSPTextureFrame(world, floorGroup, false, s_LongTime).binding == floor2Binding, "a long scene time keeps an exact frame");
    Require(SelectBSPTextureFrame(world, floorGroup, true, s_SecondFrameTime).binding == floor2Binding, "without alternate frames the alternate state shows the sequence");
    Require(SelectBSPTextureFrame(world, world.textureGroups[s_GrateGroup], false, s_SecondFrameTime).binding == grateBinding, "a still group shows its own slot");

    for (const auto invalidTime : {std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity(), -s_SecondFrameTime})
        Require(SelectBSPTextureFrame(world, floorGroup, false, invalidTime).binding == floorBinding, "a time that is negative or not finite shows frame 0");

    BSPWorld alternateWorld;
    buildWorld({{"floor", "+0lab"}, {"floor2", "+1lab"}, {"{grate", "+Alab"}}, alternateWorld);

    const auto& alternateFloor = alternateWorld.textureGroups[s_FloorGroup];
    const auto& alternateGrate = alternateWorld.textureGroups[s_GrateGroup];

    Require(alternateWorld.textureFrames.size() == s_WithAlternate && alternateWorld.textureFrames[s_AlternateFirst].binding == grateBinding, "the alternate sequence follows the sequence");
    Require(sameAnimation(alternateFloor.animation, 0, s_SequenceFrames) && sameAnimation(alternateFloor.alternateAnimation, s_AlternateFirst, 1), "a frame shows the alternate sequence in the alternate state");
    Require(sameAnimation(alternateGrate.animation, s_AlternateFirst, 1) && sameAnimation(alternateGrate.alternateAnimation, 0, s_SequenceFrames), "an alternate frame shows the sequence in the alternate state");
    Require(SelectBSPTextureFrame(alternateWorld, alternateFloor, true, s_SecondFrameTime).binding == grateBinding, "the alternate state shows the alternate frames");
    Require(SelectBSPTextureFrame(alternateWorld, alternateGrate, true, s_SecondFrameTime).binding == floor2Binding, "the alternate state of an alternate frame shows the sequence");

    BSPWorld lowerWorld;
    buildWorld({{"floor", "+0lab"}, {"{grate", "+alab"}}, lowerWorld);
    Require(sameAnimation(lowerWorld.textureGroups[s_FloorGroup].alternateAnimation, 1, 1), "alternate frame characters ignore case");

    BSPWorld caseWorld;
    buildWorld({{"floor", "+0lab"}, {"floor2", "+0LAB"}}, caseWorld);
    Require(sameAnimation(caseWorld.textureGroups[s_FloorGroup].animation, 0, 1) && sameAnimation(caseWorld.textureGroups[s_Floor2Group].animation, 1, 1), "names differing in case form two sequences");

    BSPWorld unboundWorld;
    buildWorld({{"floor", "+0lab"}, {"wall", "+1lab"}}, unboundWorld);
    Require(unboundWorld.textureFrames.size() == 1 && sameAnimation(unboundWorld.textureGroups[s_FloorGroup].animation, 0, 1), "an unbound slot is no frame");

    for (const Renames renames : {Renames{{"floor", "+0lab"}, {"floor2", "+2lab"}}, Renames{{"floor", "+0lab"}, {"floor2", "+Klab"}}})
    {
        BSPWorld stillWorld;
        buildWorld(renames, stillWorld);
        Require(isStill(stillWorld), "a missing frame or another frame character stops the animation of the name");
    }

    const Renames randomRenames = {{"floor", "-0lab"}, {"floor2", "-1lab"}};

    BSPWorld randomWorld, randomAgain;
    buildWorld(randomRenames, randomWorld);
    buildWorld(randomRenames, randomAgain);

    Require(SameBSPWorld(randomWorld, randomAgain), "random tiling picks the same frames in every build");

    //Each random tiling group starts from its own slot, with that slot's size.
    const std::size_t   randomGroups[]  = {s_FloorGroup, s_Floor2Group};
    const std::uint32_t randomWidths[]  = {TestBSPFloorWidth, TestBSPTextureSize};
    const std::uint32_t randomHeights[] = {TestBSPFloorHeight, TestBSPTextureSize};

    for (std::size_t i = 0; i < std::size(randomGroups); ++i)
    {
        const auto& group = randomWorld.textureGroups[randomGroups[i]];
        const auto  pFace = std::find_if(randomWorld.faces.begin(), randomWorld.faces.end(), [&group](const BSPWorldFace& face) { return face.surface == group.firstSurface; });
        Require(pFace != randomWorld.faces.end() && group.surfaceCount == 1, "find the face of a random tiling group");

        const auto frame = GetBSPRandomTextureFrame(pFace->textureMins, randomWidths[i], randomHeights[i], s_SequenceFrames);
        Require(sameAnimation(group.animation, frame, 1) && sameAnimation(group.alternateAnimation, frame, 1), "a random tiling surface shows the frame its cell picks");
    }

    constexpr std::uint32_t s_CellRepeat   = 20;
    constexpr std::uint32_t s_FrameCount   = 7;
    constexpr std::int32_t  s_NegativeMin  = -1000000;
    const std::int32_t      origin[]       = {0, 0};
    const std::int32_t      repeated[]     = {static_cast<std::int32_t>(TestBSPTextureSize * s_CellRepeat), 0};
    const std::int32_t      negativeMins[] = {s_NegativeMin, s_NegativeMin};

    Require(GetBSPRandomTextureFrame(origin, TestBSPTextureSize, TestBSPTextureSize, s_FrameCount) == GetBSPRandomTextureFrame(repeated, TestBSPTextureSize, TestBSPTextureSize, s_FrameCount), "cells repeat every 20 texture sizes");
    Require(GetBSPRandomTextureFrame(negativeMins, TestBSPTextureSize, TestBSPTextureSize, s_FrameCount) < s_FrameCount, "a negative lightmap origin picks a frame in range");
}

//Surfaces of a submodel follow the world's: the world builds as without the submodel, which gets its own surfaces and
//texture groups, drawn with the same texture and lightmap coordinates wherever it is placed, and no visibility.
static void TestBSPSubmodels()
{
    using namespace GoldSrcRenderTest;

    const auto bindings = MakeTestBSPBindings(1);

    BSPWorld single;
    BSPWorld world;
    Require(BuildBSPWorld(TestBSPAsset(MakeTestBSP(), bindings), single) == ErrorCode::None, "build the world alone");
    Require(BuildBSPWorld(TestBSPAsset(MakeTestBSPWithSubmodel(), bindings), world) == ErrorCode::None, "build the world and its submodel");

    Require(world.submodels.size() == 2, "one entry per model");

    const auto& worldModel = world.submodels[0];
    const auto& submodel   = world.submodels[1];
    Require(worldModel.firstSurface == 0 && worldModel.surfaceCount == single.surfaces.size(), "the world's surfaces come first");
    Require(worldModel.firstTextureGroup == 0 && worldModel.textureGroupCount == single.textureGroups.size(), "the world's groups come first");
    Require(std::equal(single.surfaces.begin(), single.surfaces.end(), world.surfaces.begin(), [](const BSPWorldSurface& a, const BSPWorldSurface& b) { return std::memcmp(&a, &b, sizeof(a)) == 0; }), "world surfaces are unchanged");
    Require(std::equal(single.indices.begin(), single.indices.begin() + CountDrawnIndices(single), world.indices.begin()), "world indices are unchanged");
    Require(std::equal(single.lightmapTexcoords.begin(), single.lightmapTexcoords.begin() + CountDrawnVertices(single), world.lightmapTexcoords.begin(), [](const Vector2& a, const Vector2& b) { return a.x == b.x && a.y == b.y; }), "world lightmap placement is unchanged");

    //The submodel has no water; the world's water follows every drawn surface of both.
    Require(!submodel.waterSurfaceCount && !submodel.waterGroupCount && world.waterSurfaces.size() == single.waterSurfaces.size(), "only the world has water");
    Require(CountDrawnIndices(world) == CountDrawnIndices(single) + 18 && world.waterSurfaces[0].indexCount == single.waterSurfaces[0].indexCount, "water indices follow the submodel's");

    //{grate, floor2 and trigger are drawn side by side in their own groups; floor is degenerate, counted with the
    //world's own.
    Require(submodel.firstSurface == worldModel.surfaceCount && submodel.surfaceCount == 3, "the submodel draws {grate, floor2 and trigger");
    Require(submodel.firstTextureGroup == worldModel.textureGroupCount && submodel.textureGroupCount == 3, "the submodel has its own groups");

    const auto& maskedGroup  = world.textureGroups[submodel.firstTextureGroup];
    const auto& group        = world.textureGroups[submodel.firstTextureGroup + 1];
    const auto& triggerGroup = world.textureGroups[submodel.firstTextureGroup + 2];
    Require(maskedGroup.slot == TestBSPGrateSlot && maskedGroup.firstSurface == submodel.firstSurface && maskedGroup.surfaceCount == 1, "the masked face has its own group");
    Require(group.slot == TestBSPFloor2Slot && group.firstSurface == submodel.firstSurface + 1 && group.surfaceCount == 1, "the ordinary face has another group");
    Require(triggerGroup.slot == TestBSPTriggerSlot && triggerGroup.firstSurface == submodel.firstSurface + 2 && triggerGroup.bUnlit && !group.bUnlit, "the unlit face has a third group");
    Require(maskedGroup.binding == FindTestBinding(bindings, TestBSPGrateSlot) && group.binding == FindTestBinding(bindings, TestBSPFloor2Slot), "submodel groups keep their slot's binding");
    Require(world.skipped.degenerate == 2 && world.skipped.sky == single.skipped.sky, "skipped faces of every submodel are counted");

    //The submodel's copy of floor2 keeps the texture coordinates and lightmap layers of the world's.
    const auto& copy     = world.surfaces[group.firstSurface];
    const auto& original = world.surfaces[2];
    Require(copy.slot == original.slot && copy.styles == original.styles && copy.indexCount == original.indexCount && copy.firstIndex >= CountDrawnIndices(single), "the copy has its own indices");

    const auto copyVertex     = world.indices[copy.firstIndex];
    const auto originalVertex = world.indices[original.firstIndex];
    Require(world.texcoords[copyVertex].x == world.texcoords[originalVertex].x && world.texcoords[copyVertex].y == world.texcoords[originalVertex].y, "the copy has the same texture coordinates");
    Require(world.positions[copyVertex].x == world.positions[originalVertex].x && world.positions[copyVertex].y == world.positions[originalVertex].y, "vertices stay in model coordinates");
    Require(copy.lightmapPage == 0 && SameLuxel(world, 0, 29, 4, TestBSPFloor2Sample) && SameLuxel(world, 1, 29, 4, TestBSPFloor2Style), "the copy has its own luxels after the world's and the submodel's {grate");

    Require(world.surfaceNode[submodel.firstSurface] == -1 && world.surfaceNode[group.firstSurface] == -1, "no world node owns the submodel's faces");
    Require(CountVisible(world, 1) == 5 && CountVisible(world, BSPSolidLeaf) == 5, "no leaf sees the submodel");
}

static bool SamePolygon(const std::vector<Vector3>& a, const std::vector<Vector3>& b)
{
    return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](const Vector3& x, const Vector3& y) { return x.x == y.x && x.y == y.y && x.z == y.z; });
}

//GoldSrc's SubdividePolygon: cuts on the 64 unit grid line nearest to the middle of a piece, unless it is closer than
//8 units to an edge, front part first; pieces are drawn in the reverse order they are made.
static void TestBSPWaterSubdivision()
{
    constexpr float gridSize    = BSPWaterSubdivideSize;
    constexpr float twoCells    = 2 * gridSize;
    constexpr float marginWidth = gridSize + BSPWaterMinCutMargin;
    constexpr float narrowWidth = marginWidth - 2;

    //The grid lines nearest to GoldSrc's BoundPoly bounds of +-9999, within and past them.
    constexpr float lastLineWithinBounds = 9984;
    constexpr float firstLinePastBounds  = 10112;

    std::vector<std::vector<Vector3>> pieces;

    Require(SubdivideBSPWaterPolygon(MakeWaterRectangle(gridSize, gridSize), pieces) == ErrorCode::None && pieces.size() == 1, "a single cell is not cut");
    Require(SamePolygon(pieces[0], MakeWaterRectangle(gridSize, gridSize)), "an uncut piece keeps its corners");

    //Cut at x = 64 first, the front (x >= 64) before the back, then each at y = 64: made in the order
    //(64..128, 64..128), (64..128, 0..64), (0..64, 64..128), (0..64, 0..64), drawn in reverse.
    Require(SubdivideBSPWaterPolygon(MakeWaterRectangle(twoCells, twoCells), pieces) == ErrorCode::None && pieces.size() == 4, "two cells each way make four pieces");
    Require(SamePolygon(pieces[0], MakeWaterRectangle(gridSize, gridSize)), "the last piece made is drawn first, its corners in order");

    //Cut pieces start at the first corner on their side, clipped corners where the cut crosses an edge.
    const std::vector<Vector3> lastPiece = {{twoCells, gridSize, 0}, {twoCells, twoCells, 0}, {gridSize, twoCells, 0}, {gridSize, gridSize, 0}};
    Require(SamePolygon(pieces[3], lastPiece), "the first piece made is drawn last");

    //A cut needs 8 units on both sides.
    Require(SubdivideBSPWaterPolygon(MakeWaterRectangle(marginWidth, gridSize), pieces) == ErrorCode::None && pieces.size() == 2, "a cut 8 units from an edge is made");
    Require(SubdivideBSPWaterPolygon(MakeWaterRectangle(narrowWidth, gridSize), pieces) == ErrorCode::None && pieces.size() == 1, "a cut closer to an edge is not");

    //A vertical face is cut along its own extents.
    const std::vector<Vector3> wall = {{0, 0, 0}, {0, twoCells, 0}, {0, twoCells, gridSize}, {0, 0, gridSize}};
    Require(SubdivideBSPWaterPolygon(wall, pieces) == ErrorCode::None && pieces.size() == 2, "a vertical face is cut too");

    //GoldSrc stops on pieces of more than 60 corners; huge faces are refused before they exhaust memory.
    std::vector<Vector3> polygon;

    for (std::uint32_t i = 0; i <= BSPMaxWaterPieceVertices; ++i)
        polygon.push_back({static_cast<float>(i), static_cast<float>(i * i) / gridSize, 0});

    Require(SubdivideBSPWaterPolygon(polygon, pieces) == ErrorCode::InvalidData && pieces.empty(), "reject a piece of more than 60 corners");

    polygon.pop_back();
    Require(SubdivideBSPWaterPolygon(polygon, pieces) == ErrorCode::None, "accept a piece of 60 corners");

    Require(SubdivideBSPWaterPolygon(MakeWaterRectangle(-lastLineWithinBounds, -lastLineWithinBounds, lastLineWithinBounds, lastLineWithinBounds), pieces) == ErrorCode::InvalidData && pieces.empty(), "reject a face cut into too many pieces");

    //Past the bounds GoldSrc keeps cutting a piece on a line at its edge, which leaves it whole.
    Require(SubdivideBSPWaterPolygon(MakeWaterRectangle(firstLinePastBounds, 0, firstLinePastBounds + twoCells, gridSize), pieces) == ErrorCode::InvalidData && pieces.empty(), "reject a face past GoldSrc's bounds");
    Require(SubdivideBSPWaterPolygon(MakeWaterRectangle(lastLineWithinBounds - twoCells, 0, lastLineWithinBounds, gridSize), pieces) == ErrorCode::None && pieces.size() == 2, "cut a face within GoldSrc's bounds");
}

//The backend sorts using the actual model-bounds center, its affine placement and the recorded camera.
static void TestBSPCameraDistance()
{
    using namespace GoldSrcRenderTest;
    const auto bindings = MakeTestBSPBindings(1);
    auto       file     = MakeTestBSPWithSubmodel();
    BSPWorld   world;
    Require(ErrorCode::None == BuildBSPWorld(TestBSPAsset(file, bindings), world), "Build bounds for sorting");
    const auto&     model    = world.submodels[1];
    constexpr float s_Center = TestBSPQuadSize / 2;
    Require(s_Center == model.center.x && s_Center == model.center.y && 0 == model.center.z, "Use model bounds rather than entity origin");
    Matrix4       transform;
    const Vector3 camera{s_Center, s_Center, 0};
    Require(0 == GetBSPSubmodelCameraDistance(model, transform, camera), "Camera at center has zero distance");
    constexpr float s_Translation = 128;
    transform.values[3]           = s_Translation;
    Require(s_Translation == GetBSPSubmodelCameraDistance(model, transform, camera), "Entity translation changes distance");
    const Vector3 movedCamera{s_Center + s_Translation, s_Center, 0};
    Require(0 == GetBSPSubmodelCameraDistance(model, transform, movedCamera), "Each view supplies its own origin");
    transform.values = {0, -1, 0, s_Translation, 1, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    const Vector3 rotatedCenter{s_Translation - s_Center, s_Center, 0};
    Require(0 == GetBSPSubmodelCameraDistance(model, transform, rotatedCenter), "Rotate the center before sorting");
    constexpr float s_Maximum = std::numeric_limits<float>::max();
    transform.values[3]       = s_Maximum;
    const Vector3 distantCamera{-s_Maximum, 0, 0};
    Require(s_Maximum == GetBSPSubmodelCameraDistance(model, transform, distantCamera), "Extreme finite distances saturate");

    const auto modelOffset = ReadBSPLumpOffset(file.data(), BSPModelLump) + BSPModelBytes;
    for (const auto invalidMinimum : {std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN(), static_cast<float>(TestBSPQuadSize + 1)})
    {
        auto          invalidFile = file;
        std::uint32_t bits;
        std::memcpy(&bits, &invalidMinimum, sizeof(bits));
        PatchUInt32(invalidFile, modelOffset, bits);
        Require(ErrorCode::InvalidData == ValidateBSPAsset(TestBSPAsset(invalidFile, bindings)), "Reject invalid sorting bounds");
    }
}

static bool SameVector(const Vector3& a, const Vector3& b)
{
    return a.x == b.x && a.y == b.y && a.z == b.z;
}

//Every submodel has an entry per face of its range, in order; a drawn face names its surface and gives the lightmap
//coordinates and frame of the surface's vertices exactly, so that what is drawn over it lines up with it.
static void TestBSPFaces()
{
    using namespace GoldSrcRenderTest;

    BSPWorld world;
    Require(BuildBSPWorld(TestBSPAsset(MakeTestBSPWithSubmodel(), MakeTestBSPBindings(1)), world) == ErrorCode::None, "build the world and its submodel");

    const auto& worldModel = world.submodels[0];
    const auto& submodel   = world.submodels[1];
    Require(worldModel.firstFace == 0 && worldModel.faceCount == TestBSPFaceCount && worldModel.firstFaceEntry == 0, "the world lists every face");
    Require(submodel.firstFace == TestBSPSubmodelFirstFace && submodel.faceCount == TestBSPSubmodelFaceCount && submodel.firstFaceEntry == TestBSPFaceCount, "the submodel's entries follow the world's");
    Require(world.faces.size() == TestBSPFaceCount + TestBSPSubmodelFaceCount, "one entry per face of each submodel");

    Require(!FindBSPSubmodelFace(world, 1, TestBSPSubmodelFirstFace - 1) && !FindBSPSubmodelFace(world, 1, TestBSPFaceCount), "faces outside the submodel's range have no entry");
    Require(!FindBSPSubmodelFace(world, 2, 0), "a missing submodel has no faces");
    Require(FindBSPSubmodelFace(world, 1, TestBSPSubmodelFirstFace) == &world.faces[submodel.firstFaceEntry], "a face of the submodel finds its own entry");
    Require(FindBSPSubmodelFace(world, 0, TestBSPSubmodelFirstFace) == &world.faces[TestBSPSubmodelFirstFace], "the world's entry of a shared face is its own");

    //Sky, water and degenerate faces are not drawn as surfaces.
    for (const auto face : {1u, 2u, TestBSPDegenerateFace})
    {
        Require(world.faces[face].surface == BSPNoSurface, "undrawn world faces have no surface");

        if (face >= TestBSPSubmodelFirstFace)
            Require(FindBSPSubmodelFace(world, 1, face)->surface == BSPNoSurface, "undrawn submodel faces have no surface");
    }

    const auto& grate  = *FindBSPSubmodelFace(world, 1, TestBSPSubmodelFirstFace);
    const auto& floor2 = *FindBSPSubmodelFace(world, 1, 6);
    Require(grate.surface == submodel.firstSurface && floor2.surface == submodel.firstSurface + 1, "submodel faces name the submodel's surfaces");
    Require(world.surfaces[world.faces[0].surface].slot == TestBSPFloorSlot && world.faces[6].surface < worldModel.surfaceCount, "world faces name the world's surfaces");

    for (const auto& face : world.faces)
    {
        if (face.surface == BSPNoSurface)
            continue;

        const auto& surface = world.surfaces[face.surface];

        for (std::uint32_t i = 0; i < surface.indexCount; ++i)
        {
            const auto vertex   = world.indices[surface.firstIndex + i];
            const auto lightmap = GetBSPFaceLightmapTexcoord(face, world.positions[vertex]);

            Require(lightmap.x == world.lightmapTexcoords[vertex].x && lightmap.y == world.lightmapTexcoords[vertex].y, "face lightmap coordinates match its vertices bit for bit");
            Require(SameVector(face.normal, world.normals[vertex]) && SameVector(face.sTangent, world.sTangents[vertex]) && SameVector(face.tTangent, world.tTangents[vertex]), "face frame matches its vertices");
        }
    }
}

//BuildMDLModel triangulates every submodel as GoldSrc draws its triangle commands and reads the texture header.
static void TestMDLBuild()
{
    using namespace GoldSrcRenderTest;

    const auto complete = MakeTestMDL();

    auto asset            = TestMDLAsset(complete.bytes);
    asset.extraModelFlags = MDLModelOutline;

    MDLModel model;
    Require(BuildMDLModel(asset, model) == ErrorCode::None, "build test model");
    Require(model.flags == MDLModelOutline && model.boneCount == TestMDLBoneCount, "model flags and bones");

    Require(model.bodyParts.size() == TestMDLBodyPartCount, "bodyparts");
    Require(model.bodyParts[0].firstSubmodel == 0 && model.bodyParts[0].submodelCount == TestMDLBodySubmodels, "body submodels");
    Require(model.bodyParts[1].firstSubmodel == TestMDLBodySubmodels && model.bodyParts[1].submodelCount == TestMDLHeadSubmodels, "head submodels");

    Require(model.submodels.size() == TestMDLSubmodelCount && model.submodels[0].name == "main" && model.submodels[2].name == "head", "submodel names");
    Require(model.submodels[0].firstMesh == 0 && model.submodels[0].meshCount == 2, "main meshes");
    Require(model.submodels[1].firstMesh == 2 && model.submodels[1].meshCount == 0, "blank submodel has no meshes");
    Require(model.submodels[2].firstMesh == 2 && model.submodels[2].meshCount == 1, "head meshes");

    //Every point has its own vertex: the strip's 4, the fan's 4 and the head's 3.
    Require(model.vertices.size() == 2 * TestMDLMainVertices + TestMDLHeadVertices, "one vertex per point");

    //A strip turns every other triangle around; a fan turns around its first point.
    const std::vector<std::uint32_t> expectedIndices = {0, 1, 2, 2, 1, 3, 4, 5, 6, 4, 6, 7, 8, 9, 10};
    Require(expectedIndices == model.indices, "strip and fan triangles");

    Require(model.meshes.size() == TestMDLMeshCount, "meshes");
    Require(model.meshes[0].firstIndex == 0 && model.meshes[0].indexCount == 6 && model.meshes[0].skinref == 0, "strip mesh");
    Require(model.meshes[1].firstIndex == 6 && model.meshes[1].indexCount == 6 && model.meshes[1].skinref == 1, "fan mesh");
    Require(model.meshes[2].firstIndex == 12 && model.meshes[2].indexCount == 3 && model.meshes[2].skinref == 0, "head mesh");

    const auto& vertex = model.vertices[1];
    Require(vertex.position.x == 1 && vertex.position.y == 0 && vertex.normal.z == 2, "vertex position and normal");
    Require(vertex.texcoord.x == TestMDLTexcoordS + 1 && vertex.texcoord.y == TestMDLTexcoordT + 1, "texel coordinates as stored");
    Require(vertex.positionBone == 1 && vertex.normalBone == 1 && model.vertices[2].positionBone == 0, "vertex bones");
    Require(model.vertices[2 * TestMDLMainVertices].positionBone == 1, "head vertex bone");

    Require(model.textures.size() == TestMDLTextureCount && model.textures[0].name == "skin_a" && model.textures[1].name == "Packed_D0_N1", "texture names");
    Require(model.textures[1].flags == MDLTextureChrome && model.textures[1].width == TestMDLTextureSize, "texture flags and size");
    Require(model.skinRefCount == TestMDLSkinRefCount && model.skinFamilyCount == TestMDLSkinFamilyCount, "skin table");
    Require(std::equal(TestMDLSkinRefs.begin(), TestMDLSkinRefs.end(), model.skinRefs.begin(), model.skinRefs.end()), "skin references");

    //Bones keep the flags of the file unless the asset gives every bone's.
    auto lowerBody = complete;
    PatchUInt32(lowerBody.bytes, lowerBody.bones + TestMDLBoneBytes + TestMDLBoneFlagsOffset, MDLBoneLowerBody);

    MDLModel fileFlags;
    Require(BuildMDLModel(TestMDLAsset(lowerBody.bytes), fileFlags) == ErrorCode::None, "build a model with bone flags");

    const std::vector<std::uint32_t> expectedFileFlags = {0, MDLBoneLowerBody};
    Require(expectedFileFlags == fileFlags.boneFlags, "bone flags of the file");

    const std::array<std::uint32_t, TestMDLBoneCount> assetFlags = {MDLBoneLowerBody, 0};

    auto flagged          = TestMDLAsset(lowerBody.bytes);
    flagged.pBoneFlags    = assetFlags.data();
    flagged.boneFlagCount = TestMDLBoneCount;

    MDLModel assetFlagged;
    Require(BuildMDLModel(flagged, assetFlagged) == ErrorCode::None, "build a model with asset bone flags");
    Require(std::equal(assetFlags.begin(), assetFlags.end(), assetFlagged.boneFlags.begin(), assetFlagged.boneFlags.end()), "bone flags of the asset");

    //A T.mdl gives the texture header of a model without textures.
    const auto textureless = MakeTestMDL(TestMDLParts::Model);
    const auto textures    = MakeTestMDL(TestMDLParts::Textures);

    MDLModel bare;
    Require(BuildMDLModel(TestMDLAsset(textureless.bytes), bare) == ErrorCode::None && bare.textures.empty() && bare.skinRefs.empty(), "model without textures");
    Require(bare.indices == model.indices, "same geometry without textures");

    auto split             = TestMDLAsset(textureless.bytes);
    split.pTextureFile     = textures.bytes.data();
    split.textureFileBytes = static_cast<std::uint32_t>(textures.bytes.size());

    MDLModel splitModel;
    Require(BuildMDLModel(split, splitModel) == ErrorCode::None && splitModel.textures.size() == TestMDLTextureCount && splitModel.skinRefs == model.skinRefs, "texture header of the T.mdl");

    auto invalid = complete.bytes;
    invalid.pop_back();
    Require(BuildMDLModel(TestMDLAsset(invalid), splitModel) == ErrorCode::InvalidData && splitModel.meshes.empty(), "an invalid model builds nothing");
}

//The texture a mesh draws, as GoldSrc's StudioDrawPoints picks it (the old StudioResolveTextureIndex cases).
static void TestMDLTextureResolve()
{
    using namespace GoldSrcRenderTest;

    MDLModel model;
    model.textures.resize(2);
    model.skinRefCount    = 4;
    model.skinFamilyCount = 1;
    model.skinRefs        = {0, 1, -1, 2};

    Require(ResolveMDLTexture(model, 0, 0) == 0 && ResolveMDLTexture(model, 0, 1) == 1, "references of the family");
    Require(ResolveMDLTexture(model, 0, -1) == -1 && ResolveMDLTexture(model, 0, 4) == -1, "references out of range");
    Require(ResolveMDLTexture(model, 0, 2) == -1 && ResolveMDLTexture(model, 0, 3) == -1, "textures out of range");

    auto noTable         = model;
    noTable.skinRefCount = 0;
    Require(ResolveMDLTexture(noTable, 0, 0) == -1, "no references");

    noTable                 = model;
    noTable.skinFamilyCount = 0;
    Require(ResolveMDLTexture(noTable, 0, 0) == -1, "no families");

    noTable = model;
    noTable.textures.clear();
    Require(ResolveMDLTexture(noTable, 0, 0) == -1, "no textures");

    //A replacement texture model can have fewer references than the body's meshes.
    MDLModel errorSkin;
    errorSkin.textures.resize(1);
    errorSkin.skinRefCount    = 1;
    errorSkin.skinFamilyCount = 1;
    errorSkin.skinRefs        = {0};

    constexpr std::int32_t s_MaxMeshSkinref = 25;

    for (std::int32_t skinref = 1; skinref <= s_MaxMeshSkinref; ++skinref)
        Require(ResolveMDLTexture(errorSkin, 0, skinref) == -1, "missing reference");

    Require(ResolveMDLTexture(errorSkin, 0, 0) == 0, "present reference");

    //A skin past the families draws the first family.
    MDLModel families;
    families.textures.resize(TestMDLTextureCount);
    families.skinRefCount    = TestMDLSkinRefCount;
    families.skinFamilyCount = TestMDLSkinFamilyCount;
    families.skinRefs.assign(TestMDLSkinRefs.begin(), TestMDLSkinRefs.end());

    Require(ResolveMDLTexture(families, 1, 0) == 1 && ResolveMDLTexture(families, 1, 1) == 0, "second family");
    Require(ResolveMDLTexture(families, TestMDLSkinFamilyCount, 0) == 0, "skin past the families");
}

//Packed texture names list their images by letter after the prefix, as StudioParsePackedSkin read them.
static void TestMDLPackedTextureNames()
{
    const auto packed = ParseMDLPackedTextureName("Packed_D0_N1");
    Require(packed.diffuse == 0 && packed.normal == 1 && packed.parallax == -1 && packed.specular == -1 && packed.count == 2, "two packed images");

    const auto all = ParseMDLPackedTextureName("packed_S3_P2_N1_D0");
    Require(all.diffuse == 0 && all.normal == 1 && all.parallax == 2 && all.specular == 3 && all.count == 4, "four images, the prefix in any case");

    const auto repeated = ParseMDLPackedTextureName("Packed_D1_D2");
    Require(repeated.diffuse == 1 && repeated.count == 1, "a repeated letter keeps its first image");

    const auto lower = ParseMDLPackedTextureName("Packed_d1_N2");
    Require(lower.diffuse == -1 && lower.normal == 2 && lower.count == 1, "image letters are upper case");

    const auto plain = ParseMDLPackedTextureName("skin_a");
    Require(plain.diffuse == -1 && plain.count == 0, "not a packed name");

    Require(ParseMDLPackedTextureName("Packed_").count == 0 && ParseMDLPackedTextureName("Packed_D").count == 0, "incomplete parts");
}

int main()
{
    TestBSPWorldBuild();
    TestBSPScrollTextures();
    TestBSPAnimatedTextures();
    TestBSPSubmodels();
    TestBSPCameraDistance();
    TestBSPFaces();
    TestBSPWaterSubdivision();
    TestMDLBuild();
    TestMDLTextureResolve();
    TestMDLPackedTextureNames();

    std::cout << "GoldSrcRenderCore BSP and MDL tests passed\n";

    return EXIT_SUCCESS;
}
