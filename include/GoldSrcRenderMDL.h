#if !defined(GOLDSRC_RENDER_MDL_H)
#    define GOLDSRC_RENDER_MDL_H

#    ifdef _WIN32
#        pragma once
#    endif

#    include "GoldSrcRenderMDLTypes.h"

#    include <array>
#    include <cstdint>
#    include <string>
#    include <vector>

//Backend-neutral reading of GoldSrc studio model (MDL version 10) files: structural validation of what receivers draw,
//the geometry of every submodel and the texture header. Everything is derived from the asset's bytes; no engine state
//is read.
namespace GoldSrcRender
{
//GoldSrc's model limits (studio.h), independent of the engine's smaller remap cache.
constexpr std::uint32_t MDLMaxTextures = 100;
//The larger submodel count accommodates SvEngine models.
constexpr std::uint32_t MDLMaxBodyParts = 32;
constexpr std::uint32_t MDLMaxSubmodels = 128;
constexpr std::uint32_t MDLMaxMeshes    = 256;
constexpr std::uint32_t MDLMaxBones     = 128;

//Studio texture flags (mstudiotexture_t::flags): GoldSrc's, then this renderer's extensions, which material files set
//(see StudioMaterial). MDLTextureRendererFlags holds every flag the renderer reads.
constexpr std::uint32_t MDLTextureFlatShade     = 0x0001;
constexpr std::uint32_t MDLTextureChrome        = 0x0002;
constexpr std::uint32_t MDLTextureFullbright    = 0x0004;
constexpr std::uint32_t MDLTextureNoMips        = 0x0008;
constexpr std::uint32_t MDLTextureAlpha         = 0x0010;
constexpr std::uint32_t MDLTextureAdditive      = 0x0020;
constexpr std::uint32_t MDLTextureMasked        = 0x0040;
constexpr std::uint32_t MDLTextureCelshade      = 0x1000;
constexpr std::uint32_t MDLTextureCelshadeFace  = 0x2000;
constexpr std::uint32_t MDLTextureCelshadeHair  = 0x4000;
constexpr std::uint32_t MDLTextureHasOutline    = 0x8000;
constexpr std::uint32_t MDLTextureDoubleFace    = 0x10000;
constexpr std::uint32_t MDLTextureNoOutline     = 0x40000;
constexpr std::uint32_t MDLTextureRendererFlags = 0x7FFFF;

//Flags a texture keeps when every texture is drawn white (StudioShadingState::bFullbrightWhite): flat shading is
//dropped. The cel shading flags of every kind.
constexpr std::uint32_t MDLTextureFullbrightWhiteFlags = MDLTextureRendererFlags & ~MDLTextureFlatShade;
constexpr std::uint32_t MDLTextureCelshadeFlags        = MDLTextureCelshade | MDLTextureCelshadeFace | MDLTextureCelshadeHair;

//GoldSrc model flag (studiohdr_t::flags) of the models drawn with outlines, which a material file may set.
constexpr std::uint32_t MDLModelOutline = 0x1000;

//Bone flag (mstudiobone_t::flags) of the bones of a player model's first person lower body, which a material file sets
//(see MDLAssetView::pBoneFlags).
constexpr std::uint32_t MDLBoneLowerBody = 0x10;

//A mesh of a submodel: indexCount indices of MDLModel::indices from firstIndex, three per triangle, and its texture
//reference into a skin family (see ResolveMDLTexture).
struct MDLMesh
{
    std::uint32_t firstIndex{};
    std::uint32_t indexCount{};
    std::int32_t  skinref{};
};

//The meshes of one submodel: a range of MDLModel::meshes.
struct MDLSubmodel
{
    std::string   name;
    std::uint32_t firstMesh{};
    std::uint32_t meshCount{};
};

//The submodels of one bodypart: a range of MDLModel::submodels.
struct MDLBodyPart
{
    std::uint32_t firstSubmodel{};
    std::uint32_t submodelCount{};
};

//One point of a triangle command, in the coordinates of its bones: every point has its own vertex. texcoord is in
//texels of the texture the mesh draws, as the file stores it.
struct MDLVertex
{
    Vector3      position;
    Vector3      normal;
    Vector2      texcoord;
    std::uint8_t positionBone{};
    std::uint8_t normalBone{};
};

struct MDLTexture
{
    std::string   name;
    std::uint32_t flags{};
    std::uint32_t width{};
    std::uint32_t height{};
};

//Every submodel of a validated MDL asset, triangulated as GoldSrc draws its triangle commands, and the texture header.
struct MDLModel
{
    //The model flags of the main file and the asset's extra ones; the flags of each bone, the asset's or the file's.
    std::uint32_t              flags{};
    std::uint32_t              boneCount{};
    std::vector<std::uint32_t> boneFlags;

    std::vector<MDLBodyPart>   bodyParts;
    std::vector<MDLSubmodel>   submodels;
    std::vector<MDLMesh>       meshes;
    std::vector<MDLVertex>     vertices;
    std::vector<std::uint32_t> indices;

    //The texture header: its textures and skinFamilyCount families of skinRefCount texture indices each, empty when the
    //header has no skin table.
    std::vector<MDLTexture>   textures;
    std::uint32_t             skinRefCount{};
    std::uint32_t             skinFamilyCount{};
    std::vector<std::int16_t> skinRefs;
};

//Textures of a texture whose name starts with "Packed_": up to four images side by side, named by "_D<n>", "_N<n>",
//"_P<n>" and "_S<n>" parts after the prefix (the diffuse, normal, parallax and specular images, n a digit), each image
//a 1 / count wide column of the texture. An image the name does not give is -1.
struct MDLPackedTexture
{
    std::int32_t  diffuse{-1};
    std::int32_t  normal{-1};
    std::int32_t  parallax{-1};
    std::int32_t  specular{-1};
    std::uint32_t count{};
};

//Checks what receivers read of a studio model file and what GoldSrc's loader reads of it, after UtilAssetsIntegrity's
//checks: the header (identifier, version, a length equal to the file size), the bones, the bodyparts with their
//submodels, meshes and triangle commands (every vertex, normal and bone index), the textures with their pixels and
//palettes and the skin families. Everything but the pixels lies before the texture data, the part GoldSrc keeps.
//Sequences, animations, hitboxes and bone controllers are left to the caller, which animates the model.
ErrorCode ValidateMDLFile(const std::uint8_t* pFile, std::uint32_t fileBytes);

//Checks a T.mdl file, of which only the texture header is read: the header, the textures and the skin families.
ErrorCode ValidateMDLTextureFile(const std::uint8_t* pFile, std::uint32_t fileBytes);

//Every shading parameter is finite.
bool ValidStudioShadingParams(const StudioShadingParams& params);

//Checks the files of an asset, its bindings and materials against the texture header, its bone flags against the bones
//and its shading overrides (see MDLAssetView). The bound and material textures themselves are not checked.
ErrorCode ValidateMDLAssetFiles(const MDLAssetView& asset);

//Checks that bodyPart and subModel name a submodel of the asset's file, that boneCount is the bone count of the file and
//that the slots of the texture overrides are textures of the texture header. Every read is bounded, so a recorded asset
//from elsewhere is checked safely.
ErrorCode ValidateMDLSubmodelReference(const MDLAssetView& asset, std::uint32_t bodyPart, std::uint32_t subModel, std::uint32_t boneCount, const TextureBinding* pTextureOverrides, std::uint32_t textureOverrideCount);

//The Client keeps only this structure after publishing the validated file to the Server.
struct MDLRecordingMetadata
{
    std::uint32_t              boneCount{};
    std::uint32_t              textureCount{};
    std::vector<std::uint32_t> bodySubmodels;
};
MDLRecordingMetadata ReadMDLRecordingMetadata(const MDLAssetView& asset);

//Builds every submodel of an MDL asset; the asset is fully validated first.
ErrorCode BuildMDLModel(const MDLAssetView& asset, MDLModel& model);

//Index in MDLModel::textures of the texture a mesh with skinref draws in skin family skin, as GoldSrc's
//StudioDrawPoints picks it: the first family when the model has no family skin. -1 when the model has no skin table or
//the reference or its texture is out of range, which a T.mdl with fewer references than the model's meshes produces:
//such meshes are not drawn.
std::int32_t ResolveMDLTexture(const MDLModel& model, std::uint32_t skin, std::int32_t skinref);

//The packed images a texture name gives (see MDLPackedTexture); none for other names.
MDLPackedTexture ParseMDLPackedTextureName(const std::string& name);
} // namespace GoldSrcRender

#endif
