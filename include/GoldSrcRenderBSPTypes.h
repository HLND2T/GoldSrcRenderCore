#if !defined(GOLDSRC_RENDER_BSP_TYPES_H)
#    define GOLDSRC_RENDER_BSP_TYPES_H

#    ifdef _WIN32
#        pragma once
#    endif

#    include "GoldSrcRenderBaseTypes.h"

//The BSP asset view of IGoldSrcRenderBridge::CreateBSPAsset and its bindings (see GoldSrcRenderBaseTypes.h for the
//rules every view follows).
namespace GoldSrcRender
{
//Lump table of a BSP30 file. BlueShift, the layout of the bshift game directory, swaps the table entries of the
//entities and the planes; every other lump is where GoldSrc expects it.
enum class BSPLayout : std::uint32_t
{
    GoldSrc,
    BlueShift
};

//A detail texture of a miptex slot, as a map's maps/<name>_detail.txt lists them: the slot's surfaces drawn with
//lightmaps multiply their color by twice the detail texture, sampled at their texture coordinates times
//(scaleS, scaleT), as GoldSrc's DT_SetRenderState does. Both scales are finite and above 0.
struct DetailTextureBinding
{
    std::uint32_t slot{};
    AssetId       texture{};
    float         scaleS{1};
    float         scaleT{1};
};

//A BSP30 file as read from disk, before the engine loader changes it in place, and the lump table layout its game
//uses. The whole file is checked at creation; the receiver reads geometry, lightmaps, visibility and miptex names
//from it and ignores embedded miptex pixels. Each binding's slot is a miptex index in the texture lump and names a
//texture asset; slots are distinct. Detail texture slots are miptex indices too, distinct among the detail
//bindings; a detail texture of an unbound slot is never drawn.
//An unbound slot is a missing texture: "sky" miptexes are sky, the others draw the receiver's missing texture.
struct BSPAssetView
{
    const char*                 szIdentifier{};
    const std::uint8_t*         pFile{};
    std::uint32_t               fileBytes{};
    BSPLayout                   layout{BSPLayout::GoldSrc};
    const TextureBinding*       pTextures{};
    std::uint32_t               textureCount{};
    const DetailTextureBinding* pDetailTextures{};
    std::uint32_t               detailTextureCount{};
};

static_assert(IsABIStable<DetailTextureBinding> && IsABIStable<BSPAssetView>);
} // namespace GoldSrcRender

#endif
