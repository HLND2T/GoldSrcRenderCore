#if !defined(GOLDSRC_RENDER_MDL_TYPES_H)
#    define GOLDSRC_RENDER_MDL_TYPES_H

#    ifdef _WIN32
#        pragma once
#    endif

#    include "GoldSrcRenderBaseTypes.h"

//The studio model asset view of IGoldSrcRenderBridge::CreateMDLAsset, its materials and shading parameters (see
//GoldSrcRenderBaseTypes.h for the rules every view follows).
namespace GoldSrcRender
{
//Base specular of the studio textures without material (see StudioMaterial).
constexpr Vector2 DefaultStudioBaseSpecular{1, 2};

//Cel shading, outline, rim light, rim dark and hair parameters of studio models, as the r_studio_celshade_*,
//r_studio_outline_*, r_studio_rimlight_*, r_studio_rimdark_* and r_studio_hair_* cvars of the same names give them,
//except that colors are normalized RGB in [0, 1] instead of 0-255. Receivers apply them to the textures cel shaded
//(see MDLTextureCelshade in GoldSrcRenderMDL.h) and to outlines.
struct StudioShadingParams
{
    std::array<float, 4> celshadeSpecular{};
    float                celshadeMidpoint{};
    float                celshadeSoftness{};
    Vector3              celshadeShadowColor;
    Vector3              celshadeHeadOffset;
    Vector2              celshadeLightdirAdjust;
    float                outlineSize{};
    float                outlineDark{};
    float                rimlightPower{};
    float                rimlightSmooth{};
    Vector2              rimlightSmooth2;
    Vector3              rimlightColor;
    float                rimdarkPower{};
    float                rimdarkSmooth{};
    Vector2              rimdarkSmooth2;
    Vector3              rimdarkColor;
    float                hairSpecularExp{};
    Vector3              hairSpecularIntensity;
    std::array<float, 4> hairSpecularNoise{};
    float                hairSpecularExp2{};
    Vector3              hairSpecularIntensity2;
    std::array<float, 4> hairSpecularNoise2{};
    Vector2              hairSpecularSmooth;
    Vector2              hairShadowOffset;
};

//The members of StudioShadingParams, in declaration order, as bits of StudioShadingOverrides::mask.
enum class StudioShadingParam : std::uint32_t
{
    CelshadeSpecular,
    CelshadeMidpoint,
    CelshadeSoftness,
    CelshadeShadowColor,
    CelshadeHeadOffset,
    CelshadeLightdirAdjust,
    OutlineSize,
    OutlineDark,
    RimlightPower,
    RimlightSmooth,
    RimlightSmooth2,
    RimlightColor,
    RimdarkPower,
    RimdarkSmooth,
    RimdarkSmooth2,
    RimdarkColor,
    HairSpecularExp,
    HairSpecularIntensity,
    HairSpecularNoise,
    HairSpecularExp2,
    HairSpecularIntensity2,
    HairSpecularNoise2,
    HairSpecularSmooth,
    HairShadowOffset,
    Count
};

//Parameters a model sets for itself: each member of values whose StudioShadingParam bit is set in mask replaces the
//frame's value (see StudioShadingState) for that model; the others are ignored.
struct StudioShadingOverrides
{
    std::uint32_t       mask{};
    StudioShadingParams values;
};

//What a model's material file (GoldSrc models' "<model>_external.txt") sets for one texture of the texture header.
//flags replaces the texture's flags from the file: the GoldSrc studio texture flags and this renderer's extensions
//(see GoldSrcRenderMDL.h). The other textures are texture assets drawn instead of, or over, the model's own:
//- replaceTexture is drawn instead of the texture. Texture coordinates stay those of the model's texture size, except
//  that replaceScale scales them for each axis: a positive scale gives the replacement's size times the scale, a
//  negative one the model's texture size times its absolute value, and zero keeps the model's texture size;
//- normalTexture, parallaxTexture and specularTexture are the normal, parallax and specular maps, sampled with the
//  same texture coordinates.
//InvalidAssetId leaves a texture out, as does a texture the receiver cannot create. An animated replaceTexture (see
//TextureAssetView) shows its frames in turn, in time with FrameState::sceneTime, and is left out when the receiver
//cannot create its frames; the others show their first frame.
//baseSpecular, when bBaseSpecular, replaces StudioShadingState::baseSpecular.
//A texture without material is drawn with the base specular DefaultStudioBaseSpecular instead.
struct StudioMaterial
{
    std::uint32_t textureIndex{};
    std::uint32_t flags{};
    AssetId       replaceTexture{};
    Vector2       replaceScale;
    AssetId       normalTexture{};
    AssetId       parallaxTexture{};
    AssetId       specularTexture{};
    bool          bBaseSpecular{};
    Vector2       baseSpecular;
};

//A studio model file as read from disk, before the engine loader changes it in place. The whole file is checked at
//creation (see ValidateMDLFile in GoldSrcRenderMDL.h); the receiver reads the bodyparts, submodels, meshes, bones and
//texture header from it and ignores sequences, animations, hitboxes and texture pixels: callers submit final bones.
//The texture header is the main file, or the T.mdl file when the main file has no textures; pTextureFile holds the
//original T.mdl bytes then, and is empty otherwise. Each binding's slot is a texture index of the texture header and
//names the texture asset drawn for it; slots are distinct, and meshes whose texture has no binding are not drawn.
//Materials name distinct textures of the texture header. extraModelFlags are GoldSrc model flags added to those of the
//main file, which a material file may set (such as MDLModelOutline); shading holds the shading parameters the model
//sets. pBoneFlags holds the flags of every bone of the main file in file order, as a material file sets them (such as
//MDLBoneLowerBody), or is empty, leaving the flags of the file.
struct MDLAssetView
{
    const char*         szIdentifier{};
    const std::uint8_t* pFile{};
    std::uint32_t       fileBytes{};

    const std::uint8_t* pTextureFile{};
    std::uint32_t       textureFileBytes{};

    const TextureBinding* pTextures{};
    std::uint32_t         textureCount{};

    const StudioMaterial*  pMaterials{};
    std::uint32_t          materialCount{};
    std::uint32_t          extraModelFlags{};
    StudioShadingOverrides shading;

    const std::uint32_t* pBoneFlags{};
    std::uint32_t        boneFlagCount{};
};

static_assert(IsABIStable<StudioShadingParams> && IsABIStable<StudioShadingOverrides> && IsABIStable<StudioMaterial>);
static_assert(IsABIStable<MDLAssetView>);
} // namespace GoldSrcRender

#endif
