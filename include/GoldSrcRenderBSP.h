#if !defined(GOLDSRC_RENDER_BSP_H)
#    define GOLDSRC_RENDER_BSP_H

#    ifdef _WIN32
#        pragma once
#    endif

#    include "GoldSrcRenderBSPTypes.h"

#    include <array>
#    include <cstdint>
#    include <vector>

//Backend-neutral reading of BSP30 world files: structural validation, the geometry and lightmaps of every submodel,
//and the world's visibility. Everything is derived from the BSP bytes and the asset's texture bindings; no engine state is read.
namespace GoldSrcRender
{
//Lumps of the BSP30 lump table, besides BSPModelLump, by their meaning; BSPLayout::BlueShift files store the
//entities and the planes in each other's table entry.
constexpr std::uint32_t BSPEntityLump      = 0;
constexpr std::uint32_t BSPPlaneLump       = 1;
constexpr std::uint32_t BSPTextureLump     = 2;
constexpr std::uint32_t BSPVertexLump      = 3;
constexpr std::uint32_t BSPVisibilityLump  = 4;
constexpr std::uint32_t BSPNodeLump        = 5;
constexpr std::uint32_t BSPTexInfoLump     = 6;
constexpr std::uint32_t BSPFaceLump        = 7;
constexpr std::uint32_t BSPLightingLump    = 8;
constexpr std::uint32_t BSPLeafLump        = 10;
constexpr std::uint32_t BSPMarkSurfaceLump = 11;
constexpr std::uint32_t BSPEdgeLump        = 12;
constexpr std::uint32_t BSPSurfEdgeLump    = 13;

//Lightmaps are packed into square pages of BSPLightmapPageSize luxels; every face has up to BSPLightmapStyles
//lightstyle layers, the same luxels in each layer.
constexpr std::uint32_t BSPLightmapPageSize = 128;
constexpr std::uint32_t BSPLightmapStyles   = 4;
constexpr std::uint32_t BSPMaxLightmapPages = 1024;
constexpr std::uint8_t  BSPNoLightstyle     = 255;

//Texture slot of surfaces whose miptex has no binding or no entry: drawn with the backend's missing texture.
constexpr std::uint32_t BSPMissingTextureSlot = 0xFFFFFFFF;

//Binding of texture groups drawn with the missing texture (see BSPWorldTextureGroup::binding).
constexpr std::uint32_t BSPMissingTextureBinding = 0xFFFFFFFF;

//Surface of the faces that are not drawn as surfaces (see BSPWorldFace::surface).
constexpr std::uint32_t BSPNoSurface = 0xFFFFFFFF;

//Texture axes of a texinfo, each the s or t vector followed by its offset.
constexpr std::uint32_t BSPTextureAxes       = 2;
constexpr std::uint32_t BSPTextureVectorSize = 4;

//Leaf 0, shared by all solid space; it has no visibility row and sees every world leaf.
constexpr std::uint32_t BSPSolidLeaf = 0;

//GoldSrc's GL_SubdivideSurface cuts turbulent faces along the multiples of BSPWaterSubdivideSize of each axis,
//skipping cuts closer than BSPWaterMinCutMargin to an edge of the piece, and stops with an error on pieces of more than
//BSPMaxWaterPieceVertices corners. Faces cut into more than BSPMaxWaterPieces pieces are rejected too: GoldSrc would
//exhaust its hunk long before.
constexpr std::int32_t  BSPWaterSubdivideSize    = 64;
constexpr float         BSPWaterMinCutMargin     = 8;
constexpr std::uint32_t BSPMaxWaterPieceVertices = 60;
constexpr std::uint32_t BSPMaxWaterPieces        = 65536;

//Number of faces of every submodel that are not drawn, by the category that excludes them. Masked ("{") faces are
//drawn like other surfaces: whether their texels are alpha tested depends on their texture's AlphaMode and on the
//draw (see IsBrushTextureAlphaTested). Turbulent faces are water surfaces (see BSPWaterSurface). Faces whose texinfo
//has TEX_SPECIAL are drawn without lightmap (see BSPWorldTextureGroup::bUnlit).
struct BSPSkippedFaces
{
    std::uint32_t sky{};
    std::uint32_t degenerate{};
};

//One drawn face: a range of BSPWorld::indices and its lightmap placement.
struct BSPWorldSurface
{
    //Miptex index of the face's texture, or BSPMissingTextureSlot.
    std::uint32_t                               slot{};
    std::uint32_t                               firstIndex{};
    std::uint32_t                               indexCount{};
    std::uint32_t                               lightmapPage{};
    std::array<std::uint8_t, BSPLightmapStyles> styles{};
};

//Animated textures change frame BSPTextureAnimationRate times a second, as GoldSrc's R_TextureAnimation steps them
//with cl.time * 10.
constexpr std::uint32_t BSPTextureAnimationRate = 10;

//A frame of an animated texture: the binding and detail binding of its slot (see BSPWorldTextureGroup::binding and
//BSPWorldTextureGroup::detailBinding).
struct BSPTextureFrame
{
    std::uint32_t binding{BSPMissingTextureBinding};
    std::uint32_t detailBinding{BSPMissingTextureBinding};
};

//The frames a texture group shows in one state of its entity (see BSPWorldTextureGroup::animation): frameCount frames
//of BSPWorld::textureFrames from firstFrame, in turn (see SelectBSPTextureFrame). A group without frames shows its own
//slot.
struct BSPTextureAnimation
{
    std::uint32_t firstFrame{};
    std::uint32_t frameCount{};
};

//A run of surfaces of one submodel sharing one texture slot and its animations; each submodel's surfaces are sorted
//by slot, then by the frames of their animations, which only random tiling slots choose per surface.
struct BSPWorldTextureGroup
{
    std::uint32_t slot{};

    //Index in BSPAssetView::pTextures of the slot's binding, so that receivers find the group's texture version
    //without searching; BSPMissingTextureBinding with BSPMissingTextureSlot. Valid for the asset the world was built
    //from.
    std::uint32_t binding{BSPMissingTextureBinding};
    std::uint32_t firstSurface{};
    std::uint32_t surfaceCount{};

    //Index in BSPAssetView::pDetailTextures of the slot's detail texture, or BSPMissingTextureBinding.
    std::uint32_t detailBinding{BSPMissingTextureBinding};

    //Width in texels of the slot's miptex for the faces GoldSrc's Mod_LoadFaces tiles (SURF_DRAWTILED), which its
    //ScrollOffset moves: those whose name starts with "scroll" and those whose texinfo has TEX_SPECIAL, except sky and
    //water. 0 for groups that do not scroll (see GetBSPScrollOffset).
    std::uint32_t scrollWidth{};

    //The frames the group shows, as GoldSrc's Mod_LoadTextures sequences and R_TextureAnimation selects them; slots
    //that are not animated have none. Bound miptexes named '+' or '-', a frame character and a common rest of the
    //name (compared with case) form two sequences: frames '0' to '9' and the alternate frames 'A' to 'J' (without
    //case), each numbered from its first frame without gaps; a name group with a gap or another frame character is not
    //animated, where GoldSrc stops with an error. animation is what an entity whose frame is 0 shows: the sequence of
    //the slot; alternateAnimation what the others show: the other sequence, when the name has both, or the same.
    //Each starts from a base, the slot itself or the first frame of the other sequence. A base named '-' tiles at
    //random: each surface shows the one frame GetBSPRandomTextureFrame picks with the size of the base; otherwise every
    //frame is shown in turn.
    BSPTextureAnimation animation;
    BSPTextureAnimation alternateAnimation;

    //The group's faces have TEX_SPECIAL in their texinfo, so GoldSrc's GL_CreateSurfaceLightmap gives them no
    //lightmap: they take no room in the lightmap pages and their samples are not read. GoldSrc then samples page 0
    //from its corner, the lightmap of other surfaces; the bridge draws them fully bright instead, without dynamic
    //lights, in the render modes that are lit (see IsBrushLightmapped).
    bool bUnlit{};
};

//A turbulent face (texture name starting with '!', "laser" or "water", bound to a texture), drawn without lightmap and
//warped as GoldSrc's EmitWaterPolys does. Its polygon is cut into pieces as GoldSrc's GL_SubdivideSurface does (see
//SubdivideBSPWaterPolygon), kept in the order GoldSrc draws them. Their vertices are in BSPWorld's vertex arrays,
//with the face's plane normal and texture axes, no lightmap coordinates, and the texture coordinates GoldSrc's warp
//starts from: the dot products with the texture axes, without their offsets and not divided by the texture size.
struct BSPWaterSurface
{
    //A fan for every piece, in its own winding, then the same fans with the corners of each piece in reverse order,
    //as EmitWaterPolys draws a face seen from behind. Both have indexCount indices.
    std::uint32_t firstIndex{};
    std::uint32_t reversedFirstIndex{};
    std::uint32_t indexCount{};

    //The indices of the first piece alone, from firstIndex: the piece GoldSrc's DrawGLSolidPoly draws.
    std::uint32_t firstPieceIndexCount{};

    //Height of the first corner of the first piece, which decides the sign of the wave (see SelectBSPWaterRuns in
    //GoldSrcRenderBSPView.h).
    float referenceHeight{};
};

//A run of water surfaces of one submodel on the same side of the same plane and sharing one texture slot. Each
//submodel's water surfaces are sorted by slot, then plane, then side.
struct BSPWaterGroup
{
    std::uint32_t slot{};

    //Index in BSPAssetView::pTextures of the slot's binding (see BSPWorldTextureGroup::binding); water surfaces are
    //always bound.
    std::uint32_t binding{};

    //The plane as the file stores it, and whether the faces lie on its back. bHorizontal marks the planes GoldSrc
    //calls PLANE_Z, whose normal is the z axis.
    Vector3 normal;
    float   dist{};
    bool    bBack{};
    bool    bHorizontal{};

    std::uint32_t firstSurface{};
    std::uint32_t surfaceCount{};
};

//A face of a submodel, as what is drawn over it, such as decals, needs it: the surface built from it, or BSPNoSurface
//for faces that are not drawn as surfaces (sky, degenerate and water faces), and for a surface the texinfo
//axes and lightmap placement that give its lightmap coordinates (see GetBSPFaceLightmapTexcoord). waterSurface is the
//entry of BSPWorld::waterSurfaces built from a water face, or BSPNoSurface. The normal and texture axes are those of
//the face's vertices (turned around for faces on the back of their plane), for every face.
struct BSPWorldFace
{
    std::uint32_t surface{BSPNoSurface};
    std::uint32_t waterSurface{BSPNoSurface};
    float         vectors[BSPTextureAxes][BSPTextureVectorSize]{};
    std::int32_t  textureMins[BSPTextureAxes]{};
    std::uint32_t lightmapX{};
    std::uint32_t lightmapY{};
    Vector3       normal;
    Vector3       sTangent;
    Vector3       tTangent;
};

//The drawn surfaces of one submodel and their texture groups, and its water surfaces and their groups: ranges of
//BSPWorld::surfaces, BSPWorld::textureGroups, BSPWorld::waterSurfaces and BSPWorld::waterGroups. A submodel without
//any has empty ranges.
struct BSPSubmodel
{
    //Center of the BSP model bounds, in model coordinates, used for backend distance sorting.
    Vector3 center;

    //The submodel's range of the file's face lump, and its entries in BSPWorld::faces, one per face of the range in
    //order: face firstFace + i is BSPWorld::faces[firstFaceEntry + i].
    std::uint32_t firstFace{};
    std::uint32_t faceCount{};
    std::uint32_t firstFaceEntry{};

    std::uint32_t firstSurface{};
    std::uint32_t surfaceCount{};
    std::uint32_t firstTextureGroup{};
    std::uint32_t textureGroupCount{};
    std::uint32_t firstWaterSurface{};
    std::uint32_t waterSurfaceCount{};
    std::uint32_t firstWaterGroup{};
    std::uint32_t waterGroupCount{};
};

//Visibility tree of the world model, kept to find the visible surfaces of a view.
struct BSPWorldNode
{
    Vector3 normal;
    float   dist{};

    //Non-negative children are nodes; a negative child c is leaf -1 - c.
    std::array<std::int32_t, 2> children{};
    std::int32_t                parent{-1};
};

struct BSPWorldLeaf
{
    std::int32_t  parent{-1};
    std::int32_t  visibilityOffset{-1};
    std::uint32_t firstMarkSurface{};
    std::uint32_t markSurfaceCount{};
};

//Every submodel of a validated BSP asset: the static world (submodel 0) and the inline brush models, in the
//coordinates of the file. Vertices and lightmap coordinates do not depend on where a submodel is drawn.
struct BSPWorld
{
    //Per vertex; each face has its own vertices.
    std::vector<Vector3> positions;
    std::vector<Vector3> normals;
    std::vector<Vector3> sTangents;
    std::vector<Vector3> tTangents;
    std::vector<Vector2> texcoords;
    std::vector<Vector2> lightmapTexcoords;

    //Surfaces are stored by submodel, then grouped by texture within each submodel; submodels has one entry per
    //model of the file. textureFrames holds the frames of every animation sequence (see
    //BSPWorldTextureGroup::animation), each sequence once, in frame order.
    std::vector<std::uint32_t>        indices;
    std::vector<BSPWorldSurface>      surfaces;
    std::vector<BSPWorldTextureGroup> textureGroups;
    std::vector<BSPTextureFrame>      textureFrames;
    std::vector<BSPSubmodel>          submodels;

    //The faces of every submodel, by submodel (see BSPSubmodel::firstFaceEntry); a face listed by two submodels has an
    //entry in each.
    std::vector<BSPWorldFace> faces;

    //Water surfaces are stored by submodel, then grouped as BSPWaterGroup describes. Their indices follow those of
    //every drawn surface.
    std::vector<BSPWaterSurface> waterSurfaces;
    std::vector<BSPWaterGroup>   waterGroups;

    //RGBA8 luxels of each lightstyle layer: lightmapPages pages of BSPLightmapPageSize squared luxels. Only the layers
    //usedStyleLayers lists are allocated; the others, and every layer of a fully bright world, are empty.
    std::uint32_t                                            lightmapPages{};
    std::array<std::vector<std::uint8_t>, BSPLightmapStyles> lightmaps;

    //Bit k is set when a drawn surface has samples in lightstyle layer k.
    std::uint32_t usedStyleLayers{};

    //The file has no lighting at all: surfaces are drawn fully bright, as GoldSrc does.
    bool bFullbright{};

    BSPSkippedFaces skipped;

    //World tree, the leaves' face lists (marksurfaces mapped to surface indices of submodel 0 or -1, and to water
    //surface indices of submodel 0 or -1), and the world node owning each surface and water surface, -1 for those of
    //other submodels.
    std::int32_t              rootNode{};
    std::vector<BSPWorldNode> nodes;
    std::vector<BSPWorldLeaf> leaves;
    std::vector<std::int32_t> markSurfaces;
    std::vector<std::int32_t> markWaterSurfaces;
    std::vector<std::int32_t> surfaceNode;
    std::vector<std::int32_t> waterSurfaceNode;
    std::uint32_t             visibleLeafCount{};
    std::vector<std::uint8_t> visibility;
};

//Checks the complete file: lump sizes, every index and offset, face extents, lightmap ranges, the world tree and
//every compressed visibility row. Bindings must name distinct, present miptex entries, and so must detail texture
//bindings.
ErrorCode ValidateBSPFile(const std::uint8_t* pFile, std::uint32_t fileBytes, BSPLayout layout, const TextureBinding* pTextures, std::uint32_t textureCount, const DetailTextureBinding* pDetailTextures, std::uint32_t detailTextureCount);

//Builds every submodel of a BSP asset; the asset is fully validated first. Lightmaps are packed in submodel and face
//order, the face order in which GoldSrc packs compiled maps, whose submodels list consecutive faces. Unbound slots
//draw the missing texture, except "sky" names, which are skipped like other unsupported surfaces (see
//BSPSkippedFaces); turbulent faces are only water surfaces when their slot is bound.
ErrorCode BuildBSPWorld(const BSPAssetView& asset, BSPWorld& world);

//Cuts a convex polygon as GoldSrc's SubdividePolygon does, in its floating point arithmetic: each piece is cut on
//the first axis whose multiple of BSPWaterSubdivideSize nearest to the middle of the piece lies at least
//BSPWaterMinCutMargin from both its ends, the front part first. pieces receives the pieces in the order GoldSrc draws
//them, the reverse of the order they are made. Fails with InvalidData on a piece of more than
//BSPMaxWaterPieceVertices corners, more than BSPMaxWaterPieces pieces, or a cut that leaves the piece whole: GoldSrc's
//bounds start at +-9999, so it never ends cutting pieces beyond them.
ErrorCode SubdivideBSPWaterPolygon(const std::vector<Vector3>& polygon, std::vector<std::vector<Vector3>>& pieces);

//Entry of BSPWorld::faces for face of the file's face lump in submodel, or null when the submodel does not exist or
//does not list the face.
const BSPWorldFace* FindBSPSubmodelFace(const BSPWorld& world, std::uint32_t submodel, std::uint32_t face);

//Lightmap coordinates of position, a point on face in the coordinates of the file, in the lightmap page of face's
//surface, which must exist: the same values, in the same floating point arithmetic, as the face's vertices have.
Vector2 GetBSPFaceLightmapTexcoord(const BSPWorldFace& face, const Vector3& position);

//Offset in texels along s of a scrolling texture group scrollWidth texels wide (see
//BSPWorldTextureGroup::scrollWidth), moving scrollSpeed texels per second, at sceneTime seconds: GoldSrc's
//ScrollOffset times the width, wrapped into (-scrollWidth, scrollWidth) with the sign of the motion. It is computed in
//double precision, so a long sceneTime does not make the texture jitter. 0 without width or for inputs that are not
//finite.
float GetBSPScrollOffset(float scrollSpeed, double sceneTime, std::uint32_t scrollWidth);

//Frame, below frameCount (which is not 0), that a surface whose lightmap starts at textureMins shows of a random tiling
//sequence whose base is width by height texels (see BSPWorldTextureGroup::animation): GoldSrc's R_TextureAnimation
//picks it from a cell of a 20 by 20 table, (textureMins + (size << 16)) / size modulo 20 on each axis, made
//non-negative here. GoldSrc fills its table with random values once per process; this one is a fixed hash of the
//cell, so every receiver and every replay picks the same frames. width and height are not 0.
std::uint32_t GetBSPRandomTextureFrame(const std::int32_t (&textureMins)[BSPTextureAxes], std::uint32_t width, std::uint32_t height, std::uint32_t frameCount);

//The binding and detail binding group shows at sceneTime, in the alternate state of its entity when bAlternate (see
//BSPWorldTextureGroup::animation): its own slot's without frames, otherwise frame floor(sceneTime *
//BSPTextureAnimationRate) modulo the frame count, computed in double precision; the first frame for times that are
//negative or not finite. group belongs to world.
BSPTextureFrame SelectBSPTextureFrame(const BSPWorld& world, const BSPWorldTextureGroup& group, bool bAlternate, double sceneTime);

//Distance from the transformed model-bounds center to the camera. Inputs are validated finite values and an affine
//transform. Distances beyond float range saturate so command sort keys remain finite.
float GetBSPSubmodelCameraDistance(const BSPSubmodel& submodel, const Matrix4& transform, const Vector3& cameraOrigin);

//Leaf containing origin, following the world tree; BSPSolidLeaf when origin is in solid space.
std::uint32_t FindBSPLeaf(const BSPWorld& world, const Vector3& origin);

//Marks the surfaces visible from leaf, following GoldSrc: a surface is visible when a leaf in the leaf's PVS lists
//it and the node owning it is an ancestor of such a leaf. BSPSolidLeaf, or a file without visibility, sees every
//world leaf. visibleSurfaces receives one flag per surface and visibleWaterSurfaces one per water surface; only those
//of submodel 0 can be visible.
void FindBSPVisibleSurfaces(const BSPWorld& world, std::uint32_t leaf, std::vector<std::uint8_t>& visibleSurfaces, std::vector<std::uint8_t>& visibleWaterSurfaces);
} // namespace GoldSrcRender

#endif
