#include "GoldSrcRenderBSP.h"
#include "GoldSrcRenderAssetUtil.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <limits>
#include <tuple>
#include <utility>

namespace GoldSrcRender
{
//Record sizes of the BSP30 lumps.
constexpr std::uint32_t BSPPlaneBytes       = 20;
constexpr std::uint32_t BSPVertexBytes      = 12;
constexpr std::uint32_t BSPNodeBytes        = 24;
constexpr std::uint32_t BSPTexInfoBytes     = 40;
constexpr std::uint32_t BSPFaceBytes        = 20;
constexpr std::uint32_t BSPLeafBytes        = 28;
constexpr std::uint32_t BSPMarkSurfaceBytes = 2;
constexpr std::uint32_t BSPEdgeBytes        = 4;
constexpr std::uint32_t BSPSurfEdgeBytes    = 4;
constexpr std::uint32_t BSPMiptexBytes      = 40;
constexpr std::uint32_t BSPLightSampleBytes = 3;

//Field offsets within the records.
constexpr std::uint32_t BSPPlaneDistOffset       = 12;
constexpr std::uint32_t BSPPlaneTypeOffset       = 16;
constexpr std::uint32_t BSPNodeChildrenOffset    = 4;
constexpr std::uint32_t BSPNodeFirstFaceOffset   = 20;
constexpr std::uint32_t BSPNodeFaceCountOffset   = 22;
constexpr std::uint32_t BSPLeafVisibilityOffset  = 4;
constexpr std::uint32_t BSPLeafFirstMarkOffset   = 20;
constexpr std::uint32_t BSPLeafMarkCountOffset   = 22;
constexpr std::uint32_t BSPTexInfoMiptexOffset   = 32;
constexpr std::uint32_t BSPTexInfoFlagsOffset    = 36;
constexpr std::uint32_t BSPFaceSideOffset        = 2;
constexpr std::uint32_t BSPFaceFirstEdgeOffset   = 4;
constexpr std::uint32_t BSPFaceEdgeCountOffset   = 8;
constexpr std::uint32_t BSPFaceTexInfoOffset     = 10;
constexpr std::uint32_t BSPFaceStylesOffset      = 12;
constexpr std::uint32_t BSPFaceLightOffset       = 16;
constexpr std::uint32_t BSPModelMinsOffset       = 0;
constexpr std::uint32_t BSPModelMaxsOffset       = 12;
constexpr std::uint32_t BSPModelHeadNodeOffset   = 36;
constexpr std::uint32_t BSPModelVisLeavesOffset  = 52;
constexpr std::uint32_t BSPMiptexNameBytes       = 16;
constexpr std::uint32_t BSPMiptexWidthOffset     = 16;
constexpr std::uint32_t BSPMiptexHeightOffset    = 20;
constexpr std::uint32_t BSPMiptexTableOffset     = 4;
constexpr std::uint32_t BSPMiptexOffsetBytes     = 4;
constexpr std::int32_t  BSPAbsentOffset          = -1;
constexpr std::int32_t  BSPSolidContents         = -2;
constexpr std::int32_t  BSPTextureSpecial        = 1;
constexpr std::uint32_t BSPMinFaceEdges          = 3;
constexpr std::uint32_t BSPMissingTextureSize    = 16;
constexpr std::uint32_t BSPLuxelSize             = 16;
constexpr std::int32_t  BSPMaxSurfaceExtent      = 512;
constexpr std::uint32_t BSPLightmapBytesPerLuxel = RGBABytes;
constexpr std::uint8_t  BSPOpaqueAlpha           = 255;
constexpr std::uint32_t BSPVisibilityBitsPerByte = 8;
constexpr std::uint32_t BSPAxes                  = 3;

//GoldSrc's PLANE_Z plane type: the normal is the z axis.
constexpr std::int32_t BSPPlaneAxialZ = 2;

//Initial bounds of GoldSrc's BoundPoly, and the half SubdividePolygon finds the middle of a piece and rounds it to the
//grid with, in double precision.
constexpr float  BSPWaterBoundsInitialMin = 9999.0f;
constexpr float  BSPWaterBoundsInitialMax = -9999.0f;
constexpr double BSPWaterHalf             = 0.5;

//Texture coordinates beyond this are rejected so that face extents stay far from integer overflow.
constexpr double BSPMaxTextureCoordinate = 268435456.0;

//Initial texture coordinate bounds of GoldSrc's CalcSurfaceExtents. They are kept because they change the extents
//of faces lying entirely beyond them.
constexpr float BSPExtentsInitialMin = 999999.0f;
constexpr float BSPExtentsInitialMax = -99999.0f;

//Lightmap coordinates sample luxel centers; a page spans BSPLightmapPageSize luxels.
constexpr float BSPLightmapHalfLuxel  = BSPLuxelSize / 2.0f;
constexpr float BSPLightmapPageExtent = static_cast<float>(BSPLightmapPageSize * BSPLuxelSize);

static std::uint16_t ReadUInt16(const std::uint8_t* pBytes, std::size_t offset)
{
    return static_cast<std::uint16_t>(pBytes[offset] | pBytes[offset + 1] << 8);
}

static std::int16_t ReadInt16(const std::uint8_t* pBytes, std::size_t offset)
{
    return static_cast<std::int16_t>(ReadUInt16(pBytes, offset));
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

static float Dot(const Vector3& a, const Vector3& b)
{
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

static Vector3 Negate(const Vector3& vector)
{
    return {-vector.x, -vector.y, -vector.z};
}

//A zero vector stays zero.
static Vector3 Normalize(const Vector3& vector)
{
    const auto length = std::sqrt(Dot(vector, vector));

    if (!(length > 0))
        return {};

    return {vector.x / length, vector.y / length, vector.z / length};
}

//The lumps of a file whose header has been checked, indexed by their meaning whatever the file's layout.
struct BSPFileView
{
    const std::uint8_t* pFile{};

    std::uint32_t offsets[BSPLumpCount]{};
    std::uint32_t lengths[BSPLumpCount]{};

    std::uint32_t Count(std::uint32_t lump, std::uint32_t recordBytes) const
    {
        return lengths[lump] / recordBytes;
    }

    const std::uint8_t* Record(std::uint32_t lump, std::uint32_t index, std::uint32_t recordBytes) const
    {
        return pFile + offsets[lump] + static_cast<std::size_t>(index) * recordBytes;
    }
};

static ErrorCode ReadBSPHeader(const std::uint8_t* pFile, std::uint32_t fileBytes, BSPLayout layout, BSPFileView& file)
{
    if (layout > BSPLayout::BlueShift)
        return ErrorCode::InvalidData;

    if (!ValidArray(pFile, fileBytes) || fileBytes < BSPHeaderBytes || fileBytes > MaxAssetBytes || ReadUInt32(pFile, 0) != BSPVersion)
        return ErrorCode::InvalidData;

    file.pFile = pFile;

    for (std::uint32_t i = 0; i < BSPLumpCount; ++i)
    {
        file.offsets[i] = ReadBSPLumpOffset(pFile, i);
        file.lengths[i] = ReadBSPLumpLength(pFile, i);

        if (!ValidRange(fileBytes, file.offsets[i], file.lengths[i], 1))
            return ErrorCode::InvalidData;
    }

    //Every later read uses the lumps by meaning; the file bytes stay as they are.
    if (layout == BSPLayout::BlueShift)
    {
        std::swap(file.offsets[BSPEntityLump], file.offsets[BSPPlaneLump]);
        std::swap(file.lengths[BSPEntityLump], file.lengths[BSPPlaneLump]);
    }

    const std::pair<std::uint32_t, std::uint32_t> recordLumps[] = {
        {BSPPlaneLump, BSPPlaneBytes},
        {BSPVertexLump, BSPVertexBytes},
        {BSPNodeLump, BSPNodeBytes},
        {BSPTexInfoLump, BSPTexInfoBytes},
        {BSPFaceLump, BSPFaceBytes},
        {BSPLeafLump, BSPLeafBytes},
        {BSPMarkSurfaceLump, BSPMarkSurfaceBytes},
        {BSPEdgeLump, BSPEdgeBytes},
        {BSPSurfEdgeLump, BSPSurfEdgeBytes},
        {BSPModelLump, BSPModelBytes},
    };

    for (const auto& [lump, recordBytes] : recordLumps)
    {
        if (file.lengths[lump] % recordBytes != 0)
            return ErrorCode::InvalidData;
    }

    //The world needs its model, its tree and the solid leaf 0.
    if (!file.Count(BSPModelLump, BSPModelBytes) || !file.Count(BSPNodeLump, BSPNodeBytes) || !file.Count(BSPLeafLump, BSPLeafBytes))
        return ErrorCode::InvalidData;

    return ErrorCode::None;
}

//A miptex entry of the texture lump and the indices of its slot's binding and detail texture binding,
//BSPMissingTextureBinding when there is none.
struct BSPMiptex
{
    bool          bPresent{};
    std::uint32_t binding{BSPMissingTextureBinding};
    std::uint32_t detailBinding{BSPMissingTextureBinding};
    char          szName[BSPMiptexNameBytes + 1]{};
    std::uint32_t width{};
    std::uint32_t height{};
};

static bool HasPrefix(const char* szName, const char* szPrefix, bool bIgnoreCase)
{
    for (; *szPrefix; ++szName, ++szPrefix)
    {
        auto a = static_cast<unsigned char>(*szName);
        auto b = static_cast<unsigned char>(*szPrefix);

        if (bIgnoreCase)
        {
            a = static_cast<unsigned char>(std::tolower(a));
            b = static_cast<unsigned char>(std::tolower(b));
        }

        if (a != b)
            return false;
    }

    return true;
}

//Reads the texture lump; an empty lump has no entries. Bindings must name distinct, present entries, and so must
//detail texture bindings.
static ErrorCode ReadBSPMiptexes(const BSPFileView& file, const TextureBinding* pTextures, std::uint32_t textureCount, const DetailTextureBinding* pDetailTextures, std::uint32_t detailTextureCount, std::vector<BSPMiptex>& miptexes)
{
    miptexes.clear();

    const auto  length = file.lengths[BSPTextureLump];
    const auto* pLump  = file.pFile + file.offsets[BSPTextureLump];

    if (length)
    {
        if (length < BSPMiptexTableOffset)
            return ErrorCode::InvalidData;

        const auto count = ReadInt32(pLump, 0);

        if (count < 0 || static_cast<std::uint32_t>(count) > MaxTableEntries || !ValidRange(length, BSPMiptexTableOffset, count, BSPMiptexOffsetBytes))
            return ErrorCode::InvalidData;

        miptexes.resize(count);

        for (std::uint32_t i = 0; i < miptexes.size(); ++i)
        {
            const auto offset = ReadInt32(pLump, BSPMiptexTableOffset + i * BSPMiptexOffsetBytes);

            if (offset == BSPAbsentOffset)
                continue;

            if (offset < 0 || !ValidRange(length, offset, 1, BSPMiptexBytes))
                return ErrorCode::InvalidData;

            auto& miptex = miptexes[i];

            miptex.bPresent = true;
            std::memcpy(miptex.szName, pLump + offset, BSPMiptexNameBytes);
            miptex.width  = ReadUInt32(pLump, offset + BSPMiptexWidthOffset);
            miptex.height = ReadUInt32(pLump, offset + BSPMiptexHeightOffset);

            if (!miptex.width || !miptex.height || miptex.width > MaxTextureDimension || miptex.height > MaxTextureDimension)
                return ErrorCode::InvalidData;

            //GoldSrc blanks the third character of names containing '~' when it loads them.
            if (std::strchr(miptex.szName, '~'))
                miptex.szName[2] = ' ';
        }
    }

    for (std::uint32_t i = 0; i < textureCount; ++i)
    {
        const auto slot = pTextures[i].slot;

        if (slot >= miptexes.size() || !miptexes[slot].bPresent || miptexes[slot].binding != BSPMissingTextureBinding)
            return ErrorCode::InvalidData;

        miptexes[slot].binding = i;
    }

    for (std::uint32_t i = 0; i < detailTextureCount; ++i)
    {
        const auto slot = pDetailTextures[i].slot;

        if (slot >= miptexes.size() || !miptexes[slot].bPresent || miptexes[slot].detailBinding != BSPMissingTextureBinding)
            return ErrorCode::InvalidData;

        miptexes[slot].detailBinding = i;
    }

    return ErrorCode::None;
}

//Why a world face is drawn or skipped.
enum class BSPFaceCategory
{
    Drawn,
    Sky,
    Turbulent,
    Degenerate
};

struct BSPFace
{
    std::uint32_t                               plane{};
    bool                                        bBack{};
    std::uint32_t                               firstEdge{};
    std::uint32_t                               edgeCount{};
    std::uint32_t                               texInfo{};
    std::array<std::uint8_t, BSPLightmapStyles> styles{};
    std::int32_t                                lightOffset{};
};

static BSPFace ReadFace(const BSPFileView& file, std::uint32_t index)
{
    const auto* pFace = file.Record(BSPFaceLump, index, BSPFaceBytes);

    BSPFace face;
    face.plane       = ReadUInt16(pFace, 0);
    face.bBack       = ReadInt16(pFace, BSPFaceSideOffset) != 0;
    face.firstEdge   = ReadUInt32(pFace, BSPFaceFirstEdgeOffset);
    face.edgeCount   = ReadUInt16(pFace, BSPFaceEdgeCountOffset);
    face.texInfo     = ReadUInt16(pFace, BSPFaceTexInfoOffset);
    face.lightOffset = ReadInt32(pFace, BSPFaceLightOffset);
    std::memcpy(face.styles.data(), pFace + BSPFaceStylesOffset, BSPLightmapStyles);

    return face;
}

struct BSPTexInfo
{
    float        vectors[BSPTextureAxes][BSPTextureVectorSize]{};
    std::int32_t miptex{};
    std::int32_t flags{};
};

static BSPTexInfo ReadTexInfo(const BSPFileView& file, std::uint32_t index)
{
    const auto* pTexInfo = file.Record(BSPTexInfoLump, index, BSPTexInfoBytes);

    BSPTexInfo texInfo;

    for (std::uint32_t axis = 0; axis < BSPTextureAxes; ++axis)
    {
        for (std::uint32_t i = 0; i < BSPTextureVectorSize; ++i)
            texInfo.vectors[axis][i] = ReadFloat(pTexInfo, (axis * BSPTextureVectorSize + i) * sizeof(float));
    }

    texInfo.miptex = ReadInt32(pTexInfo, BSPTexInfoMiptexOffset);
    texInfo.flags  = ReadInt32(pTexInfo, BSPTexInfoFlagsOffset);

    return texInfo;
}

//The texture a face is drawn with, following GoldSrc: an entry that is absent or unbound is the missing texture,
//whose texinfo flags are cleared, unless it is named as sky; sky textures are never bound. bUnlit marks the drawn faces
//GoldSrc's GL_CreateSurfaceLightmap gives no lightmap: those whose texinfo has TEX_SPECIAL, which Mod_LoadFaces tiles
//(SURF_DRAWTILED) as scrolling and special faces.
struct BSPFaceTexture
{
    std::uint32_t   slot{BSPMissingTextureSlot};
    std::uint32_t   binding{BSPMissingTextureBinding};
    std::uint32_t   detailBinding{BSPMissingTextureBinding};
    std::uint32_t   width{BSPMissingTextureSize};
    std::uint32_t   height{BSPMissingTextureSize};
    std::int32_t    flags{};
    BSPFaceCategory category{BSPFaceCategory::Drawn};
    std::uint32_t   scrollWidth{};
    bool            bUnlit{};
};

static BSPFaceTexture ResolveFaceTexture(const std::vector<BSPMiptex>& miptexes, const BSPTexInfo& texInfo, const BSPFace& face)
{
    BSPFaceTexture texture;

    if (!miptexes.empty() && miptexes[texInfo.miptex].bPresent)
    {
        const auto& miptex = miptexes[texInfo.miptex];

        if (HasPrefix(miptex.szName, "sky", false))
        {
            texture.flags    = texInfo.flags;
            texture.category = BSPFaceCategory::Sky;
        }
        else if (miptex.binding != BSPMissingTextureBinding)
        {
            texture.slot          = texInfo.miptex;
            texture.binding       = miptex.binding;
            texture.detailBinding = miptex.detailBinding;
            texture.width         = miptex.width;
            texture.height        = miptex.height;
            texture.flags         = texInfo.flags;

            //Tiled faces, those of scrolling textures and the other faces whose texinfo has TEX_SPECIAL, are drawn like
            //other surfaces, their groups keeping the width the scroll offset wraps at (see GetBSPScrollOffset), as
            //GoldSrc's ScrollOffset moves every SURF_DRAWTILED face. Masked ("{") textures are drawn too: their texture
            //asset's AlphaMode marks the transparent texels.
            if (HasPrefix(miptex.szName, "scroll", false))
                texture.scrollWidth = miptex.width;
            else if (miptex.szName[0] == '!' || HasPrefix(miptex.szName, "laser", true) || HasPrefix(miptex.szName, "water", true))
                texture.category = BSPFaceCategory::Turbulent;
            else if (texInfo.flags & BSPTextureSpecial)
                texture.scrollWidth = miptex.width;
        }
    }

    texture.bUnlit = texture.category == BSPFaceCategory::Drawn && (texture.flags & BSPTextureSpecial);

    if (face.edgeCount < BSPMinFaceEdges)
        texture.category = BSPFaceCategory::Degenerate;

    return texture;
}

//Animated miptex names, as GoldSrc's Mod_LoadTextures reads them: a prefix, '+' or '-' for random tiling, a frame
//character, then the name the frames share. Frame characters '0' to '9' number the frames of a sequence, 'A' to 'J'
//(without case) those of the alternate sequence.
constexpr char          BSPAnimatedPrefix          = '+';
constexpr char          BSPRandomTilingPrefix      = '-';
constexpr std::uint32_t BSPAnimationFrameChar      = 1;
constexpr std::uint32_t BSPAnimationNameOffset     = 2;
constexpr std::uint32_t BSPMaxAnimationFrames      = 10;
constexpr std::uint32_t BSPAnimationSequences      = 2;
constexpr std::uint32_t BSPPrimarySequence         = 0;
constexpr std::uint32_t BSPAlternateSequence       = 1;
constexpr char          BSPFirstFrameChar          = '0';
constexpr char          BSPFirstAlternateFrameChar = 'A';

//GoldSrc's R_TextureAnimation picks random tiling frames from a table of BSPRandomTileCells by BSPRandomTileCells
//values from 0 to BSPRandomTileMax, at the cell (textureMins + (size << BSPRandomTileSizeShift)) / size of each axis.
constexpr std::int64_t  BSPRandomTileCells     = 20;
constexpr std::uint32_t BSPRandomTileMax       = 0x7FFF;
constexpr std::int64_t  BSPRandomTileSizeShift = 16;

//The cell's value is a fixed integer hash of its index (lowbias32, by Chris Wellons).
constexpr std::uint32_t BSPRandomTileHashShift1      = 16;
constexpr std::uint32_t BSPRandomTileHashMultiplier1 = 0x7FEB352D;
constexpr std::uint32_t BSPRandomTileHashShift2      = 15;
constexpr std::uint32_t BSPRandomTileHashMultiplier2 = 0x846CA68B;
constexpr std::uint32_t BSPRandomTileHashShift3      = 16;

//The sequences a slot's frame belongs to: its own and the other sequence of its name, without frames when the name has
//only one; neither has frames for slots that are not animated.
struct BSPSlotAnimation
{
    BSPTextureAnimation own;
    BSPTextureAnimation other;
};

//Bound miptexes named with an animated prefix and a frame character take part in sequences: Mod_LoadTextures sequences
//the textures it loads, and an unbound slot is one the engine could not load.
static bool IsBSPAnimationCandidate(const BSPMiptex& miptex)
{
    if (!miptex.bPresent || miptex.binding == BSPMissingTextureBinding || !miptex.szName[BSPAnimationFrameChar])
        return false;

    return miptex.szName[0] == BSPAnimatedPrefix || miptex.szName[0] == BSPRandomTilingPrefix;
}

//Reads the frame character of a candidate into its sequence and frame; false for other characters.
static bool ReadBSPAnimationFrame(const BSPMiptex& miptex, std::uint32_t& sequence, std::uint32_t& frame)
{
    const auto c = static_cast<char>(std::toupper(static_cast<unsigned char>(miptex.szName[BSPAnimationFrameChar])));

    if (c >= BSPFirstFrameChar && c < BSPFirstFrameChar + static_cast<char>(BSPMaxAnimationFrames))
    {
        sequence = BSPPrimarySequence;
        frame    = static_cast<std::uint32_t>(c - BSPFirstFrameChar);
        return true;
    }

    if (c >= BSPFirstAlternateFrameChar && c < BSPFirstAlternateFrameChar + static_cast<char>(BSPMaxAnimationFrames))
    {
        sequence = BSPAlternateSequence;
        frame    = static_cast<std::uint32_t>(c - BSPFirstAlternateFrameChar);
        return true;
    }

    return false;
}

/*
====================
SequenceBSPTextureAnimations

Sequences the animated miptexes as GoldSrc's Mod_LoadTextures does (see BSPWorldTextureGroup::animation): candidates
sharing the rest of their name, compared with case, form a primary and an alternate sequence, the later slot of a
repeated frame taking its place. The frames of each sequence are appended to world.textureFrames and their slots to
frameSlots; slots receives the sequences of every slot, one entry per miptex. A name group with another frame character
or a missing frame is not animated.
====================
*/
static void SequenceBSPTextureAnimations(const std::vector<BSPMiptex>& miptexes, BSPWorld& world, std::vector<BSPSlotAnimation>& slots, std::vector<std::uint32_t>& frameSlots)
{
    slots.assign(miptexes.size(), {});
    frameSlots.clear();

    std::vector<std::uint8_t> sequenced(miptexes.size());

    for (std::uint32_t first = 0; first < miptexes.size(); ++first)
    {
        if (sequenced[first] || !IsBSPAnimationCandidate(miptexes[first]))
            continue;

        std::array<std::array<std::uint32_t, BSPMaxAnimationFrames>, BSPAnimationSequences> frames;
        std::array<std::uint32_t, BSPAnimationSequences>                                    counts{};
        bool                                                                                bValid = true;

        for (auto& sequence : frames)
            sequence.fill(BSPMissingTextureSlot);

        for (auto slot = first; slot < miptexes.size(); ++slot)
        {
            const auto& miptex = miptexes[slot];

            if (sequenced[slot] || !IsBSPAnimationCandidate(miptex) || std::strcmp(miptex.szName + BSPAnimationNameOffset, miptexes[first].szName + BSPAnimationNameOffset))
                continue;

            sequenced[slot] = true;

            std::uint32_t sequence{}, frame{};

            if (!ReadBSPAnimationFrame(miptex, sequence, frame))
            {
                bValid = false;
                continue;
            }

            frames[sequence][frame] = slot;
            counts[sequence]        = std::max(counts[sequence], frame + 1);
        }

        for (std::uint32_t sequence = 0; sequence < BSPAnimationSequences; ++sequence)
        {
            for (std::uint32_t frame = 0; frame < counts[sequence]; ++frame)
                bValid = bValid && frames[sequence][frame] != BSPMissingTextureSlot;
        }

        if (!bValid)
            continue;

        std::array<BSPTextureAnimation, BSPAnimationSequences> animations;

        for (std::uint32_t sequence = 0; sequence < BSPAnimationSequences; ++sequence)
        {
            animations[sequence] = {static_cast<std::uint32_t>(world.textureFrames.size()), counts[sequence]};

            for (std::uint32_t frame = 0; frame < counts[sequence]; ++frame)
            {
                const auto slot = frames[sequence][frame];

                world.textureFrames.push_back({miptexes[slot].binding, miptexes[slot].detailBinding});
                frameSlots.push_back(slot);
            }
        }

        for (std::uint32_t sequence = 0; sequence < BSPAnimationSequences; ++sequence)
        {
            for (std::uint32_t frame = 0; frame < counts[sequence]; ++frame)
                slots[frames[sequence][frame]] = {animations[sequence], animations[BSPAnimationSequences - 1 - sequence]};
        }
    }
}

//The frames a surface shows from base, the first slot of sequence or the surface's own (see
//BSPWorldTextureGroup::animation): the one GetBSPRandomTextureFrame picks when base tiles at random, otherwise all.
static BSPTextureAnimation ResolveBSPAnimation(const BSPMiptex& base, const BSPTextureAnimation& sequence, const std::int32_t (&textureMins)[BSPTextureAxes])
{
    if (base.szName[0] != BSPRandomTilingPrefix)
        return sequence;

    return {sequence.firstFrame + GetBSPRandomTextureFrame(textureMins, base.width, base.height, sequence.frameCount), 1};
}

static bool SameBSPAnimation(const BSPTextureAnimation& a, const BSPTextureAnimation& b)
{
    return a.firstFrame == b.firstFrame && a.frameCount == b.frameCount;
}

//Position of the face's i-th corner. Surfedges and edges must have been checked.
static Vector3 ReadFaceVertex(const BSPFileView& file, const BSPFace& face, std::uint32_t i)
{
    const auto  surfEdge = ReadInt32(file.Record(BSPSurfEdgeLump, face.firstEdge + i, BSPSurfEdgeBytes), 0);
    const auto* pEdge    = file.Record(BSPEdgeLump, surfEdge > 0 ? surfEdge : -surfEdge, BSPEdgeBytes);
    const auto  vertex   = ReadUInt16(pEdge, surfEdge > 0 ? 0 : sizeof(std::uint16_t));

    return ReadVector(file.Record(BSPVertexLump, vertex, BSPVertexBytes), 0);
}

static std::vector<Vector3> ReadFacePolygon(const BSPFileView& file, const BSPFace& face)
{
    std::vector<Vector3> polygon(face.edgeCount);

    for (std::uint32_t i = 0; i < face.edgeCount; ++i)
        polygon[i] = ReadFaceVertex(file, face, i);

    return polygon;
}

static float GetAxis(const Vector3& vector, std::uint32_t axis)
{
    return axis == 0 ? vector.x : axis == 1 ? vector.y :
                                              vector.z;
}

//One step of GoldSrc's SubdividePolygon: cuts polygon on the first axis it can and goes on with the front part, then
//the back part, or appends polygon to pieces when no axis can be cut. Pieces are appended in the order they are made.
static ErrorCode SubdivideWaterPiece(const std::vector<Vector3>& polygon, std::vector<std::vector<Vector3>>& pieces)
{
    if (polygon.size() > BSPMaxWaterPieceVertices)
        return ErrorCode::InvalidData;

    float mins[BSPAxes];
    float maxs[BSPAxes];

    for (std::uint32_t axis = 0; axis < BSPAxes; ++axis)
    {
        mins[axis] = BSPWaterBoundsInitialMin;
        maxs[axis] = BSPWaterBoundsInitialMax;
    }

    for (const auto& vertex : polygon)
    {
        for (std::uint32_t axis = 0; axis < BSPAxes; ++axis)
        {
            const auto value = GetAxis(vertex, axis);

            if (value < mins[axis])
                mins[axis] = value;

            if (value > maxs[axis])
                maxs[axis] = value;
        }
    }

    const auto count = polygon.size();

    for (std::uint32_t axis = 0; axis < BSPAxes; ++axis)
    {
        //GoldSrc keeps the grid line as a float after computing it in double precision.
        auto line = static_cast<float>((mins[axis] + maxs[axis]) * BSPWaterHalf);
        line      = static_cast<float>(BSPWaterSubdivideSize * std::floor(line / BSPWaterSubdivideSize + BSPWaterHalf));

        if (maxs[axis] - line < BSPWaterMinCutMargin || line - mins[axis] < BSPWaterMinCutMargin)
            continue;

        std::vector<float> distances(count + 1);

        for (std::size_t i = 0; i < count; ++i)
            distances[i] = GetAxis(polygon[i], axis) - line;

        distances[count] = distances[0];

        //Bounds start at BoundPoly's +-9999, so beyond them a piece is not cut but kept whole on one side, which
        //GoldSrc would cut again forever.
        const bool bSplits = std::any_of(distances.begin(), distances.end(), [](float distance) { return distance > 0; }) &&
            std::any_of(distances.begin(), distances.end(), [](float distance) { return distance < 0; });

        if (!bSplits)
            return ErrorCode::InvalidData;

        std::vector<Vector3> front;
        std::vector<Vector3> back;

        for (std::size_t i = 0; i < count; ++i)
        {
            const auto& vertex = polygon[i];
            const auto& next   = polygon[(i + 1) % count];

            if (distances[i] >= 0)
                front.push_back(vertex);

            if (distances[i] <= 0)
                back.push_back(vertex);

            if (distances[i] == 0 || distances[i + 1] == 0)
                continue;

            if ((distances[i] > 0) != (distances[i + 1] > 0))
            {
                const auto fraction = distances[i] / (distances[i] - distances[i + 1]);

                const Vector3 clip{vertex.x + fraction * (next.x - vertex.x), vertex.y + fraction * (next.y - vertex.y), vertex.z + fraction * (next.z - vertex.z)};

                front.push_back(clip);
                back.push_back(clip);
            }
        }

        auto code = SubdivideWaterPiece(front, pieces);
        if (!Succeeded(code))
            return code;

        return SubdivideWaterPiece(back, pieces);
    }

    if (pieces.size() >= BSPMaxWaterPieces)
        return ErrorCode::InvalidData;

    pieces.push_back(polygon);

    return ErrorCode::None;
}

ErrorCode SubdivideBSPWaterPolygon(const std::vector<Vector3>& polygon, std::vector<std::vector<Vector3>>& pieces)
{
    pieces.clear();

    const auto code = SubdivideWaterPiece(polygon, pieces);

    if (!Succeeded(code))
    {
        pieces.clear();
        return code;
    }

    //GoldSrc links every new piece in front of the pieces made before it.
    std::reverse(pieces.begin(), pieces.end());

    return ErrorCode::None;
}

static double TextureCoordinate(const BSPTexInfo& texInfo, std::uint32_t axis, const Vector3& position)
{
    const auto* vector = texInfo.vectors[axis];

    return static_cast<double>(position.x) * vector[0] + static_cast<double>(position.y) * vector[1] + static_cast<double>(position.z) * vector[2] + vector[3];
}

//Lightmap rectangle of a face in luxel units, as GoldSrc's CalcSurfaceExtents.
struct BSPFaceExtents
{
    std::int32_t textureMins[BSPTextureAxes]{};
    std::int32_t extents[BSPTextureAxes]{};

    std::uint32_t LuxelWidth() const
    {
        return static_cast<std::uint32_t>(extents[0] / static_cast<std::int32_t>(BSPLuxelSize)) + 1;
    }

    std::uint32_t LuxelHeight() const
    {
        return static_cast<std::uint32_t>(extents[1] / static_cast<std::int32_t>(BSPLuxelSize)) + 1;
    }
};

//The face must have at least one edge. Faces without TEX_SPECIAL may not exceed BSPMaxSurfaceExtent.
static ErrorCode ComputeFaceExtents(const BSPFileView& file, const BSPFace& face, const BSPTexInfo& texInfo, std::int32_t flags, BSPFaceExtents& extents)
{
    float mins[BSPTextureAxes];
    float maxs[BSPTextureAxes];

    for (std::uint32_t axis = 0; axis < BSPTextureAxes; ++axis)
    {
        mins[axis] = BSPExtentsInitialMin;
        maxs[axis] = BSPExtentsInitialMax;
    }

    for (std::uint32_t i = 0; i < face.edgeCount; ++i)
    {
        const auto position = ReadFaceVertex(file, face, i);

        for (std::uint32_t axis = 0; axis < BSPTextureAxes; ++axis)
        {
            const auto value = TextureCoordinate(texInfo, axis, position);

            if (!(std::fabs(value) <= BSPMaxTextureCoordinate))
                return ErrorCode::InvalidData;

            //GoldSrc keeps the extremes as floats.
            mins[axis] = std::min(mins[axis], static_cast<float>(value));
            maxs[axis] = std::max(maxs[axis], static_cast<float>(value));
        }
    }

    for (std::uint32_t axis = 0; axis < BSPTextureAxes; ++axis)
    {
        const auto luxelMin = static_cast<std::int32_t>(std::floor(mins[axis] / BSPLuxelSize));
        const auto luxelMax = static_cast<std::int32_t>(std::ceil(maxs[axis] / BSPLuxelSize));

        extents.textureMins[axis] = luxelMin * static_cast<std::int32_t>(BSPLuxelSize);
        extents.extents[axis]     = (luxelMax - luxelMin) * static_cast<std::int32_t>(BSPLuxelSize);

        if (!(flags & BSPTextureSpecial) && extents.extents[axis] > BSPMaxSurfaceExtent)
            return ErrorCode::InvalidData;
    }

    return ErrorCode::None;
}

//Styles before the first BSPNoLightstyle each have one layer of samples, as GoldSrc reads them.
static std::uint32_t CountFaceStyles(const BSPFace& face)
{
    std::uint32_t count = 0;

    while (count < BSPLightmapStyles && face.styles[count] != BSPNoLightstyle)
        ++count;

    return count;
}

//Decodes the run-length compressed visibility row at offset into rowBytes bytes, as GoldSrc does: a zero byte is
//followed by the number of zero bytes it stands for. Fails if the row runs past the data.
static bool DecodeVisibilityRow(const std::uint8_t* pVisibility, std::size_t visibilityBytes, std::uint32_t offset, std::uint32_t rowBytes, std::uint8_t* pRow)
{
    std::uint32_t written = 0;

    while (written < rowBytes)
    {
        if (offset >= visibilityBytes)
            return false;

        const auto value = pVisibility[offset++];

        if (value)
        {
            pRow[written++] = value;
            continue;
        }

        if (offset >= visibilityBytes)
            return false;

        const auto zeros = std::min<std::uint32_t>(pVisibility[offset++], rowBytes - written);

        std::memset(pRow + written, 0, zeros);
        written += zeros;
    }

    return true;
}

static std::uint32_t VisibilityRowBytes(std::uint32_t visibleLeafCount)
{
    return (visibleLeafCount + BSPVisibilityBitsPerByte - 1) / BSPVisibilityBitsPerByte;
}

static ErrorCode ValidateBSPModels(const BSPFileView& file, std::uint32_t& visibleLeafCount)
{
    const auto faceCount = file.Count(BSPFaceLump, BSPFaceBytes);
    const auto nodeCount = file.Count(BSPNodeLump, BSPNodeBytes);
    const auto leafCount = file.Count(BSPLeafLump, BSPLeafBytes);

    for (std::uint32_t i = 0; i < file.Count(BSPModelLump, BSPModelBytes); ++i)
    {
        const auto* pModel = file.Record(BSPModelLump, i, BSPModelBytes);

        for (std::uint32_t axis = 0; axis < BSPAxes; ++axis)
        {
            const auto minimum = ReadFloat(pModel, BSPModelMinsOffset + axis * sizeof(float));
            const auto maximum = ReadFloat(pModel, BSPModelMaxsOffset + axis * sizeof(float));
            if (!std::isfinite(minimum) || !std::isfinite(maximum) || minimum > maximum)
                return ErrorCode::InvalidData;
        }

        const auto headNode  = ReadInt32(pModel, BSPModelHeadNodeOffset);
        const auto firstFace = ReadInt32(pModel, BSPModelFirstFaceOffset);
        const auto count     = ReadInt32(pModel, BSPModelFaceCountOffset);

        if (headNode < 0 || static_cast<std::uint32_t>(headNode) >= nodeCount)
            return ErrorCode::InvalidData;

        if (firstFace < 0 || count < 0 || !ValidRange(faceCount, firstFace, count, 1))
            return ErrorCode::InvalidData;
    }

    //Leaf 0 is not counted.
    const auto visibleLeaves = ReadInt32(file.Record(BSPModelLump, 0, BSPModelBytes), BSPModelVisLeavesOffset);

    if (visibleLeaves < 0 || static_cast<std::uint32_t>(visibleLeaves) >= leafCount)
        return ErrorCode::InvalidData;

    visibleLeafCount = visibleLeaves;

    return ErrorCode::None;
}

static ErrorCode ValidateBSPGeometry(const BSPFileView& file)
{
    const auto vertexCount = file.Count(BSPVertexLump, BSPVertexBytes);
    const auto edgeCount   = file.Count(BSPEdgeLump, BSPEdgeBytes);

    for (std::uint32_t i = 0; i < file.Count(BSPPlaneLump, BSPPlaneBytes); ++i)
    {
        const auto* pPlane = file.Record(BSPPlaneLump, i, BSPPlaneBytes);

        if (!ValidVector(ReadVector(pPlane, 0)) || !std::isfinite(ReadFloat(pPlane, BSPPlaneDistOffset)))
            return ErrorCode::InvalidData;
    }

    for (std::uint32_t i = 0; i < vertexCount; ++i)
    {
        if (!ValidVector(ReadVector(file.Record(BSPVertexLump, i, BSPVertexBytes), 0)))
            return ErrorCode::InvalidData;
    }

    for (std::uint32_t i = 0; i < edgeCount; ++i)
    {
        const auto* pEdge = file.Record(BSPEdgeLump, i, BSPEdgeBytes);

        if (ReadUInt16(pEdge, 0) >= vertexCount || ReadUInt16(pEdge, sizeof(std::uint16_t)) >= vertexCount)
            return ErrorCode::InvalidData;
    }

    for (std::uint32_t i = 0; i < file.Count(BSPSurfEdgeLump, BSPSurfEdgeBytes); ++i)
    {
        const auto surfEdge = ReadInt32(file.Record(BSPSurfEdgeLump, i, BSPSurfEdgeBytes), 0);

        if (surfEdge == std::numeric_limits<std::int32_t>::min() || static_cast<std::uint32_t>(surfEdge < 0 ? -surfEdge : surfEdge) >= edgeCount)
            return ErrorCode::InvalidData;
    }

    return ErrorCode::None;
}

static ErrorCode ValidateBSPTexInfos(const BSPFileView& file, const std::vector<BSPMiptex>& miptexes)
{
    for (std::uint32_t i = 0; i < file.Count(BSPTexInfoLump, BSPTexInfoBytes); ++i)
    {
        const auto texInfo = ReadTexInfo(file, i);

        for (const auto& vector : texInfo.vectors)
        {
            if (!std::all_of(std::begin(vector), std::end(vector), [](float value) { return std::isfinite(value); }))
                return ErrorCode::InvalidData;
        }

        //Without a texture lump every face uses the missing texture.
        if (!miptexes.empty() && (texInfo.miptex < 0 || static_cast<std::uint32_t>(texInfo.miptex) >= miptexes.size()))
            return ErrorCode::InvalidData;
    }

    return ErrorCode::None;
}

static ErrorCode ValidateBSPFaces(const BSPFileView& file, const std::vector<BSPMiptex>& miptexes)
{
    const auto planeCount    = file.Count(BSPPlaneLump, BSPPlaneBytes);
    const auto surfEdgeCount = file.Count(BSPSurfEdgeLump, BSPSurfEdgeBytes);
    const auto texInfoCount  = file.Count(BSPTexInfoLump, BSPTexInfoBytes);
    const auto lightBytes    = file.lengths[BSPLightingLump];

    for (std::uint32_t i = 0; i < file.Count(BSPFaceLump, BSPFaceBytes); ++i)
    {
        const auto face = ReadFace(file, i);

        if (face.plane >= planeCount || face.texInfo >= texInfoCount || !ValidRange(surfEdgeCount, face.firstEdge, face.edgeCount, 1))
            return ErrorCode::InvalidData;

        if (!face.edgeCount)
            continue;

        const auto texInfo = ReadTexInfo(file, face.texInfo);
        const auto texture = ResolveFaceTexture(miptexes, texInfo, face);

        BSPFaceExtents extents;

        auto code = ComputeFaceExtents(file, face, texInfo, texture.flags, extents);
        if (!Succeeded(code))
            return code;

        //Water faces are cut into pieces as GoldSrc cuts them, which may fail.
        if (texture.category == BSPFaceCategory::Turbulent)
        {
            std::vector<std::vector<Vector3>> pieces;

            code = SubdivideBSPWaterPolygon(ReadFacePolygon(file, face), pieces);
            if (!Succeeded(code))
                return code;
        }

        //Only drawn faces with a lightmap read their samples; a file without lighting is drawn fully bright.
        if (texture.category != BSPFaceCategory::Drawn || texture.bUnlit || !lightBytes || face.lightOffset == BSPAbsentOffset)
            continue;

        const auto luxels = extents.LuxelWidth() * extents.LuxelHeight();

        if (face.lightOffset < 0 || !ValidRange(lightBytes, face.lightOffset, CountFaceStyles(face) * luxels, BSPLightSampleBytes))
            return ErrorCode::InvalidData;
    }

    return ErrorCode::None;
}

//Child c of a node: a node when c >= 0, otherwise leaf -1 - c.
static bool ValidNodeChild(std::int32_t child, std::uint32_t nodeCount, std::uint32_t leafCount)
{
    if (child >= 0)
        return static_cast<std::uint32_t>(child) < nodeCount;

    return static_cast<std::uint32_t>(-1 - child) < leafCount;
}

static ErrorCode ValidateBSPTree(const BSPFileView& file)
{
    const auto planeCount = file.Count(BSPPlaneLump, BSPPlaneBytes);
    const auto nodeCount  = file.Count(BSPNodeLump, BSPNodeBytes);
    const auto leafCount  = file.Count(BSPLeafLump, BSPLeafBytes);
    const auto faceCount  = file.Count(BSPFaceLump, BSPFaceBytes);

    for (std::uint32_t i = 0; i < nodeCount; ++i)
    {
        const auto* pNode = file.Record(BSPNodeLump, i, BSPNodeBytes);

        const auto plane = ReadInt32(pNode, 0);

        if (plane < 0 || static_cast<std::uint32_t>(plane) >= planeCount)
            return ErrorCode::InvalidData;

        for (std::uint32_t side = 0; side < 2; ++side)
        {
            if (!ValidNodeChild(ReadInt16(pNode, BSPNodeChildrenOffset + side * sizeof(std::int16_t)), nodeCount, leafCount))
                return ErrorCode::InvalidData;
        }

        if (!ValidRange(faceCount, ReadUInt16(pNode, BSPNodeFirstFaceOffset), ReadUInt16(pNode, BSPNodeFaceCountOffset), 1))
            return ErrorCode::InvalidData;
    }

    //The world tree must be a tree: no node is reached twice, which also rules out cycles. Leaves may be shared.
    std::vector<std::uint8_t>  visited(nodeCount);
    std::vector<std::uint32_t> pending{static_cast<std::uint32_t>(ReadInt32(file.Record(BSPModelLump, 0, BSPModelBytes), BSPModelHeadNodeOffset))};

    while (!pending.empty())
    {
        const auto node = pending.back();
        pending.pop_back();

        if (visited[node])
            return ErrorCode::InvalidData;

        visited[node] = 1;

        const auto* pNode = file.Record(BSPNodeLump, node, BSPNodeBytes);

        for (std::uint32_t side = 0; side < 2; ++side)
        {
            const auto child = ReadInt16(pNode, BSPNodeChildrenOffset + side * sizeof(std::int16_t));

            if (child >= 0)
                pending.push_back(child);
        }
    }

    return ErrorCode::None;
}

static ErrorCode ValidateBSPLeaves(const BSPFileView& file, std::uint32_t visibleLeafCount)
{
    const auto markSurfaceCount = file.Count(BSPMarkSurfaceLump, BSPMarkSurfaceBytes);
    const auto faceCount        = file.Count(BSPFaceLump, BSPFaceBytes);

    for (std::uint32_t i = 0; i < markSurfaceCount; ++i)
    {
        if (ReadUInt16(file.Record(BSPMarkSurfaceLump, i, BSPMarkSurfaceBytes), 0) >= faceCount)
            return ErrorCode::InvalidData;
    }

    //Rows are decoded straight from the borrowed file.
    const auto*               pVisibility     = file.pFile + file.offsets[BSPVisibilityLump];
    const auto                visibilityBytes = file.lengths[BSPVisibilityLump];
    std::vector<std::uint8_t> row(VisibilityRowBytes(visibleLeafCount));

    for (std::uint32_t i = 0; i < file.Count(BSPLeafLump, BSPLeafBytes); ++i)
    {
        const auto* pLeaf = file.Record(BSPLeafLump, i, BSPLeafBytes);

        if (!ValidRange(markSurfaceCount, ReadUInt16(pLeaf, BSPLeafFirstMarkOffset), ReadUInt16(pLeaf, BSPLeafMarkCountOffset), 1))
            return ErrorCode::InvalidData;

        //Without visibility data every leaf sees the whole world.
        const auto offset = ReadInt32(pLeaf, BSPLeafVisibilityOffset);

        if (!visibilityBytes || offset == BSPAbsentOffset)
            continue;

        if (offset < 0 || !DecodeVisibilityRow(pVisibility, visibilityBytes, offset, static_cast<std::uint32_t>(row.size()), row.data()))
            return ErrorCode::InvalidData;
    }

    return ErrorCode::None;
}

ErrorCode ValidateBSPFile(const std::uint8_t* pFile, std::uint32_t fileBytes, BSPLayout layout, const TextureBinding* pTextures, std::uint32_t textureCount, const DetailTextureBinding* pDetailTextures, std::uint32_t detailTextureCount)
{
    if (!ValidArray(pTextures, textureCount) || !ValidArray(pDetailTextures, detailTextureCount))
        return ErrorCode::InvalidData;

    BSPFileView file;

    auto code = ReadBSPHeader(pFile, fileBytes, layout, file);
    if (!Succeeded(code))
        return code;

    std::uint32_t visibleLeafCount{};

    code = ValidateBSPModels(file, visibleLeafCount);
    if (!Succeeded(code))
        return code;

    std::vector<BSPMiptex> miptexes;

    code = ReadBSPMiptexes(file, pTextures, textureCount, pDetailTextures, detailTextureCount, miptexes);
    if (!Succeeded(code))
        return code;

    code = ValidateBSPGeometry(file);
    if (!Succeeded(code))
        return code;

    code = ValidateBSPTexInfos(file, miptexes);
    if (!Succeeded(code))
        return code;

    code = ValidateBSPFaces(file, miptexes);
    if (!Succeeded(code))
        return code;

    code = ValidateBSPTree(file);
    if (!Succeeded(code))
        return code;

    return ValidateBSPLeaves(file, visibleLeafCount);
}

//Packs face lightmaps into pages, as GoldSrc's AllocBlock: each page tracks the filled height of every column and
//a block goes to the lowest position of the first page it fits.
class CBSPLightmapAllocator
{
public:
    bool Allocate(std::uint32_t width, std::uint32_t height, std::uint32_t& page, std::uint32_t& x, std::uint32_t& y)
    {
        for (page = 0; page < BSPMaxLightmapPages; ++page)
        {
            if (page == m_Columns.size())
                m_Columns.emplace_back();

            auto& columns = m_Columns[page];
            auto  best    = BSPLightmapPageSize;

            for (std::uint32_t i = 0; i + width < BSPLightmapPageSize; ++i)
            {
                std::uint32_t top = 0;
                std::uint32_t j   = 0;

                for (; j < width; ++j)
                {
                    if (columns[i + j] >= best)
                        break;

                    top = std::max(top, columns[i + j]);
                }

                if (j == width)
                {
                    x    = i;
                    y    = top;
                    best = top;
                }
            }

            if (best + height > BSPLightmapPageSize)
                continue;

            for (std::uint32_t i = 0; i < width; ++i)
                columns[x + i] = best + height;

            return true;
        }

        return false;
    }

    std::uint32_t PageCount() const
    {
        return static_cast<std::uint32_t>(m_Columns.size());
    }

private:
    std::vector<std::array<std::uint32_t, BSPLightmapPageSize>> m_Columns;
};

//Lightstyle layers with samples for the face: the first CountFaceStyles(face), none without lighting.
static std::uint32_t CountFaceSampleStyles(const BSPFileView& file, const BSPFace& face)
{
    return file.lengths[BSPLightingLump] && face.lightOffset != BSPAbsentOffset ? CountFaceStyles(face) : 0;
}

//Writes the face's luxels into every allocated lightstyle layer (see BSPWorld::lightmaps). Layers without samples
//stay black.
static void WriteFaceLightmap(const BSPFileView& file, const BSPFace& face, const BSPFaceExtents& extents, std::uint32_t page, std::uint32_t x, std::uint32_t y, BSPWorld& world)
{
    const auto width  = extents.LuxelWidth();
    const auto height = extents.LuxelHeight();
    const auto luxels = width * height;
    const auto styles = CountFaceSampleStyles(file, face);

    for (std::uint32_t layer = 0; layer < BSPLightmapStyles; ++layer)
    {
        if (!(world.usedStyleLayers & (1u << layer)))
            continue;

        auto&       pixels   = world.lightmaps[layer];
        const auto* pSamples = layer < styles ? file.pFile + file.offsets[BSPLightingLump] + face.lightOffset + static_cast<std::size_t>(layer) * luxels * BSPLightSampleBytes : nullptr;

        for (std::uint32_t row = 0; row < height; ++row)
        {
            auto* pDest = pixels.data() + ((static_cast<std::size_t>(page) * BSPLightmapPageSize + y + row) * BSPLightmapPageSize + x) * BSPLightmapBytesPerLuxel;

            for (std::uint32_t column = 0; column < width; ++column, pDest += BSPLightmapBytesPerLuxel)
            {
                if (pSamples)
                {
                    const auto* pSample = pSamples + (static_cast<std::size_t>(row) * width + column) * BSPLightSampleBytes;

                    pDest[0] = pSample[0];
                    pDest[1] = pSample[1];
                    pDest[2] = pSample[2];
                }

                pDest[3] = BSPOpaqueAlpha;
            }
        }
    }
}

//A drawn face before its lightmap page is written, and the frames its texture group shows (see
//BSPWorldTextureGroup::animation). An unlit face (see BSPFaceTexture::bUnlit) has no lightmap: it keeps page 0 at
//0, 0, where GoldSrc leaves the lightmap of a surface it allocates none.
struct BSPPendingSurface
{
    std::uint32_t       submodel{};
    std::uint32_t       face{};
    BSPFace             data;
    BSPFaceExtents      extents;
    BSPWorldSurface     surface;
    std::uint32_t       lightmapX{};
    std::uint32_t       lightmapY{};
    bool                bUnlit{};
    BSPTextureAnimation animation;
    BSPTextureAnimation alternateAnimation;
};

//The normal and texture axes every vertex of a face has, turned around for faces on the back of their plane.
struct BSPFaceFrame
{
    Vector3 normal;
    Vector3 sTangent;
    Vector3 tTangent;
};

static BSPFaceFrame ReadFaceFrame(const BSPFileView& file, const BSPFace& face, const BSPTexInfo& texInfo)
{
    BSPFaceFrame frame;
    frame.normal   = ReadVector(file.Record(BSPPlaneLump, face.plane, BSPPlaneBytes), 0);
    frame.sTangent = Normalize({texInfo.vectors[0][0], texInfo.vectors[0][1], texInfo.vectors[0][2]});
    frame.tTangent = Normalize({texInfo.vectors[1][0], texInfo.vectors[1][1], texInfo.vectors[1][2]});

    if (face.bBack)
    {
        frame.normal   = Negate(frame.normal);
        frame.sTangent = Negate(frame.sTangent);
        frame.tTangent = Negate(frame.tTangent);
    }

    return frame;
}

//Lightmap coordinate along one texture axis of a point whose texture coordinate (dot product with the axis, plus its
//offset) is value, on a face whose lightmap starts at textureMin and is placed at luxel offset of its page.
static float LightmapCoordinate(float value, std::int32_t textureMin, std::uint32_t offset)
{
    return (value - textureMin + offset * BSPLuxelSize + BSPLightmapHalfLuxel) / BSPLightmapPageExtent;
}

static void AppendFaceVertices(const BSPFileView& file, const BSPPendingSurface& pending, const BSPTexInfo& texInfo, const BSPFaceTexture& texture, BSPWorld& world)
{
    const auto& face  = pending.data;
    const auto  frame = ReadFaceFrame(file, face, texInfo);

    const auto firstVertex = static_cast<std::uint32_t>(world.positions.size());

    for (std::uint32_t i = 0; i < face.edgeCount; ++i)
    {
        const auto position = ReadFaceVertex(file, face, i);

        Vector2 coordinate;
        Vector2 lightmap;

        float* coordinates[BSPTextureAxes] = {&coordinate.x, &coordinate.y};
        float* lightmaps[BSPTextureAxes]   = {&lightmap.x, &lightmap.y};

        const float         sizes[BSPTextureAxes]   = {static_cast<float>(texture.width), static_cast<float>(texture.height)};
        const std::uint32_t offsets[BSPTextureAxes] = {pending.lightmapX, pending.lightmapY};

        for (std::uint32_t axis = 0; axis < BSPTextureAxes; ++axis)
        {
            const auto* vector = texInfo.vectors[axis];
            const auto  value  = Dot(position, {vector[0], vector[1], vector[2]}) + vector[3];

            *coordinates[axis] = value / sizes[axis];
            *lightmaps[axis]   = LightmapCoordinate(value, pending.extents.textureMins[axis], offsets[axis]);
        }

        world.positions.push_back(position);
        world.normals.push_back(frame.normal);
        world.sTangents.push_back(frame.sTangent);
        world.tTangents.push_back(frame.tTangent);
        world.texcoords.push_back(coordinate);
        world.lightmapTexcoords.push_back(lightmap);
    }

    //BSP faces are convex; a fan keeps their winding.
    for (std::uint32_t i = 1; i + 1 < face.edgeCount; ++i)
    {
        world.indices.push_back(firstVertex);
        world.indices.push_back(firstVertex + i);
        world.indices.push_back(firstVertex + i + 1);
    }
}

//A turbulent face before its pieces are added.
struct BSPPendingWater
{
    std::uint32_t submodel{};
    std::uint32_t face{};
    BSPFace       data;
    std::uint32_t slot{};
    std::uint32_t binding{};
};

//Adds the pieces of a water face (see BSPWaterSurface): their vertices, a fan for each piece as GoldSrc's GL_POLYGON
//draws it, then the fans of the pieces with their corners in reverse order. The pieces were checked when the file
//was validated.
static BSPWaterSurface AppendWaterVertices(const BSPFileView& file, const BSPPendingWater& pending, BSPWorld& world)
{
    const auto& face    = pending.data;
    const auto  texInfo = ReadTexInfo(file, face.texInfo);
    const auto  frame   = ReadFaceFrame(file, face, texInfo);

    std::vector<std::vector<Vector3>> pieces;
    SubdivideBSPWaterPolygon(ReadFacePolygon(file, face), pieces);

    const Vector3 sAxis{texInfo.vectors[0][0], texInfo.vectors[0][1], texInfo.vectors[0][2]};
    const Vector3 tAxis{texInfo.vectors[1][0], texInfo.vectors[1][1], texInfo.vectors[1][2]};

    std::vector<std::uint32_t> firstVertices;

    for (const auto& piece : pieces)
    {
        firstVertices.push_back(static_cast<std::uint32_t>(world.positions.size()));

        for (const auto& position : piece)
        {
            world.positions.push_back(position);
            world.normals.push_back(frame.normal);
            world.sTangents.push_back(frame.sTangent);
            world.tTangents.push_back(frame.tTangent);
            world.texcoords.push_back({Dot(position, sAxis), Dot(position, tAxis)});
            world.lightmapTexcoords.push_back({});
        }
    }

    BSPWaterSurface surface;
    surface.firstIndex      = static_cast<std::uint32_t>(world.indices.size());
    surface.referenceHeight = pieces.front().front().z;

    for (std::size_t i = 0; i < pieces.size(); ++i)
    {
        const auto first = firstVertices[i];
        const auto count = static_cast<std::uint32_t>(pieces[i].size());

        for (std::uint32_t corner = 1; corner + 1 < count; ++corner)
        {
            world.indices.push_back(first);
            world.indices.push_back(first + corner);
            world.indices.push_back(first + corner + 1);
        }

        if (!i)
            surface.firstPieceIndexCount = static_cast<std::uint32_t>(world.indices.size()) - surface.firstIndex;
    }

    surface.reversedFirstIndex = static_cast<std::uint32_t>(world.indices.size());
    surface.indexCount         = surface.reversedFirstIndex - surface.firstIndex;

    //The reversed corners of a piece start at its last one: a fan from it.
    for (std::size_t i = 0; i < pieces.size(); ++i)
    {
        const auto last  = firstVertices[i] + static_cast<std::uint32_t>(pieces[i].size()) - 1;
        const auto count = static_cast<std::uint32_t>(pieces[i].size());

        for (std::uint32_t corner = 1; corner + 1 < count; ++corner)
        {
            world.indices.push_back(last);
            world.indices.push_back(last - corner);
            world.indices.push_back(last - corner - 1);
        }
    }

    return surface;
}

//Adds the water faces of every submodel, grouped by submodel, slot, plane and side, and maps the faces of submodel 0
//to their water surfaces.
static void BuildBSPWater(const BSPFileView& file, std::vector<BSPPendingWater>& pendingWater, std::vector<std::int32_t>& faceWaterSurfaces, BSPWorld& world)
{
    std::stable_sort(pendingWater.begin(), pendingWater.end(), [](const BSPPendingWater& a, const BSPPendingWater& b) {
        return std::tie(a.submodel, a.slot, a.data.plane, a.data.bBack) < std::tie(b.submodel, b.slot, b.data.plane, b.data.bBack);
    });

    const BSPPendingWater* pPrevious = nullptr;

    for (const auto& pending : pendingWater)
    {
        const auto index = static_cast<std::uint32_t>(world.waterSurfaces.size());

        if (pending.submodel == 0)
            faceWaterSurfaces[pending.face] = static_cast<std::int32_t>(index);

        world.waterSurfaces.push_back(AppendWaterVertices(file, pending, world));

        auto& submodel = world.submodels[pending.submodel];

        world.faces[submodel.firstFaceEntry + (pending.face - submodel.firstFace)].waterSurface = index;

        if (!submodel.waterSurfaceCount)
            submodel.firstWaterSurface = index;

        ++submodel.waterSurfaceCount;

        const bool bSameGroup = pPrevious && std::tie(pPrevious->submodel, pPrevious->slot, pPrevious->data.plane, pPrevious->data.bBack) == std::tie(pending.submodel, pending.slot, pending.data.plane, pending.data.bBack);

        if (!bSameGroup)
        {
            const auto* pPlane = file.Record(BSPPlaneLump, pending.data.plane, BSPPlaneBytes);

            BSPWaterGroup group;
            group.slot         = pending.slot;
            group.binding      = pending.binding;
            group.normal       = ReadVector(pPlane, 0);
            group.dist         = ReadFloat(pPlane, BSPPlaneDistOffset);
            group.bBack        = pending.data.bBack;
            group.bHorizontal  = ReadInt32(pPlane, BSPPlaneTypeOffset) == BSPPlaneAxialZ;
            group.firstSurface = index;

            if (!submodel.waterGroupCount)
                submodel.firstWaterGroup = static_cast<std::uint32_t>(world.waterGroups.size());

            world.waterGroups.push_back(group);
            ++submodel.waterGroupCount;
        }

        ++world.waterGroups.back().surfaceCount;
        pPrevious = &pending;
    }
}

static void CountSkippedFace(BSPFaceCategory category, BSPSkippedFaces& skipped)
{
    switch (category)
    {
        case BSPFaceCategory::Sky:
            ++skipped.sky;
            break;
        case BSPFaceCategory::Degenerate:
            ++skipped.degenerate;
            break;
        case BSPFaceCategory::Drawn:
        case BSPFaceCategory::Turbulent:
            break;
    }
}

//Copies the world tree, the leaves and their face lists, and records which world node owns each surface and water
//surface.
static void BuildBSPVisibility(const BSPFileView& file, const std::vector<std::int32_t>& faceSurfaces, const std::vector<std::int32_t>& faceWaterSurfaces, BSPWorld& world)
{
    const auto nodeCount = file.Count(BSPNodeLump, BSPNodeBytes);
    const auto leafCount = file.Count(BSPLeafLump, BSPLeafBytes);

    world.nodes.resize(nodeCount);
    world.leaves.resize(leafCount);

    for (std::uint32_t i = 0; i < nodeCount; ++i)
    {
        const auto* pNode  = file.Record(BSPNodeLump, i, BSPNodeBytes);
        const auto* pPlane = file.Record(BSPPlaneLump, ReadInt32(pNode, 0), BSPPlaneBytes);

        auto& node  = world.nodes[i];
        node.normal = ReadVector(pPlane, 0);
        node.dist   = ReadFloat(pPlane, BSPPlaneDistOffset);

        for (std::uint32_t side = 0; side < 2; ++side)
            node.children[side] = ReadInt16(pNode, BSPNodeChildrenOffset + side * sizeof(std::int16_t));
    }

    for (std::uint32_t i = 0; i < leafCount; ++i)
    {
        const auto* pLeaf = file.Record(BSPLeafLump, i, BSPLeafBytes);

        auto& leaf            = world.leaves[i];
        leaf.visibilityOffset = world.visibility.empty() ? BSPAbsentOffset : ReadInt32(pLeaf, BSPLeafVisibilityOffset);

        //GoldSrc never draws the faces of solid leaves.
        if (ReadInt32(pLeaf, 0) == BSPSolidContents)
            continue;

        leaf.firstMarkSurface = ReadUInt16(pLeaf, BSPLeafFirstMarkOffset);
        leaf.markSurfaceCount = ReadUInt16(pLeaf, BSPLeafMarkCountOffset);
    }

    world.markSurfaces.resize(file.Count(BSPMarkSurfaceLump, BSPMarkSurfaceBytes));
    world.markWaterSurfaces.resize(world.markSurfaces.size());

    for (std::uint32_t i = 0; i < world.markSurfaces.size(); ++i)
    {
        const auto face = ReadUInt16(file.Record(BSPMarkSurfaceLump, i, BSPMarkSurfaceBytes), 0);

        world.markSurfaces[i]      = faceSurfaces[face];
        world.markWaterSurfaces[i] = faceWaterSurfaces[face];
    }

    //Only nodes of the world tree own world surfaces; parents are set along it.
    world.surfaceNode.assign(world.surfaces.size(), -1);
    world.waterSurfaceNode.assign(world.waterSurfaces.size(), -1);

    std::vector<std::int32_t> pending{world.rootNode};

    while (!pending.empty())
    {
        const auto index = pending.back();
        pending.pop_back();

        const auto* pNode     = file.Record(BSPNodeLump, index, BSPNodeBytes);
        const auto  firstFace = std::uint32_t{ReadUInt16(pNode, BSPNodeFirstFaceOffset)};
        const auto  faceCount = std::uint32_t{ReadUInt16(pNode, BSPNodeFaceCountOffset)};

        for (auto face = firstFace; face < firstFace + faceCount; ++face)
        {
            if (faceSurfaces[face] >= 0)
                world.surfaceNode[faceSurfaces[face]] = index;

            if (faceWaterSurfaces[face] >= 0)
                world.waterSurfaceNode[faceWaterSurfaces[face]] = index;
        }

        for (const auto child : world.nodes[index].children)
        {
            if (child >= 0)
            {
                world.nodes[child].parent = index;
                pending.push_back(child);
            }
            else
                world.leaves[-1 - child].parent = index;
        }
    }
}

ErrorCode BuildBSPWorld(const BSPAssetView& asset, BSPWorld& world)
{
    world = {};

    auto code = ValidateBSPAsset(asset);
    if (!Succeeded(code))
        return code;

    BSPFileView file;

    code = ReadBSPHeader(asset.pFile, asset.fileBytes, asset.layout, file);
    if (!Succeeded(code))
        return code;

    std::vector<BSPMiptex> miptexes;

    code = ReadBSPMiptexes(file, asset.pTextures, asset.textureCount, asset.pDetailTextures, asset.detailTextureCount, miptexes);
    if (!Succeeded(code))
        return code;

    std::vector<BSPSlotAnimation> slotAnimations;
    std::vector<std::uint32_t>    frameSlots;
    SequenceBSPTextureAnimations(miptexes, world, slotAnimations, frameSlots);

    const auto* pWorldModel = file.Record(BSPModelLump, 0, BSPModelBytes);

    world.rootNode         = ReadInt32(pWorldModel, BSPModelHeadNodeOffset);
    world.visibleLeafCount = static_cast<std::uint32_t>(ReadInt32(pWorldModel, BSPModelVisLeavesOffset));
    world.bFullbright      = !file.lengths[BSPLightingLump];

    const auto* pVisibility = file.pFile + file.offsets[BSPVisibilityLump];
    world.visibility.assign(pVisibility, pVisibility + file.lengths[BSPVisibilityLump]);

    //Lightmaps are packed in submodel and face order (see BuildBSPWorld); a face listed by two submodels is built for
    //each of them.
    CBSPLightmapAllocator          allocator;
    std::vector<BSPPendingSurface> pendingSurfaces;
    std::vector<BSPPendingWater>   pendingWater;

    const auto submodelCount = file.Count(BSPModelLump, BSPModelBytes);

    for (std::uint32_t submodel = 0; submodel < submodelCount; ++submodel)
    {
        const auto* pModel    = file.Record(BSPModelLump, submodel, BSPModelBytes);
        const auto  firstFace = static_cast<std::uint32_t>(ReadInt32(pModel, BSPModelFirstFaceOffset));
        const auto  faceCount = static_cast<std::uint32_t>(ReadInt32(pModel, BSPModelFaceCountOffset));

        for (auto face = firstFace; face < firstFace + faceCount; ++face)
        {
            BSPPendingSurface pending;
            pending.submodel = submodel;
            pending.face     = face;
            pending.data     = ReadFace(file, face);

            const auto texInfo = ReadTexInfo(file, pending.data.texInfo);
            const auto texture = ResolveFaceTexture(miptexes, texInfo, pending.data);

            //Water has no lightmap.
            if (texture.category == BSPFaceCategory::Turbulent)
            {
                pendingWater.push_back({submodel, face, pending.data, texture.slot, texture.binding});
                continue;
            }

            if (texture.category != BSPFaceCategory::Drawn)
            {
                CountSkippedFace(texture.category, world.skipped);
                continue;
            }

            code = ComputeFaceExtents(file, pending.data, texInfo, texture.flags, pending.extents);
            if (!Succeeded(code))
                return code;

            //Unlit faces take no room, so the other faces are packed where GoldSrc packs them.
            pending.bUnlit = texture.bUnlit;

            if (!pending.bUnlit && !allocator.Allocate(pending.extents.LuxelWidth(), pending.extents.LuxelHeight(), pending.surface.lightmapPage, pending.lightmapX, pending.lightmapY))
                return ErrorCode::LimitExceeded;

            //Only layers some face has samples for are allocated and written.
            if (!pending.bUnlit)
                world.usedStyleLayers |= (1u << CountFaceSampleStyles(file, pending.data)) - 1;

            pending.surface.slot   = texture.slot;
            pending.surface.styles = pending.data.styles;

            //The alternate state starts from the first frame of the other sequence, when the name has one.
            if (texture.slot != BSPMissingTextureSlot && slotAnimations[texture.slot].own.frameCount)
            {
                const auto& sequences = slotAnimations[texture.slot];

                pending.animation          = ResolveBSPAnimation(miptexes[texture.slot], sequences.own, pending.extents.textureMins);
                pending.alternateAnimation = pending.animation;

                if (sequences.other.frameCount)
                    pending.alternateAnimation = ResolveBSPAnimation(miptexes[frameSlots[sequences.other.firstFrame]], sequences.other, pending.extents.textureMins);
            }

            pendingSurfaces.push_back(pending);
        }
    }

    world.lightmapPages = allocator.PageCount();

    for (std::uint32_t layer = 0; layer < BSPLightmapStyles; ++layer)
    {
        if (world.usedStyleLayers & (1u << layer))
            world.lightmaps[layer].assign(static_cast<std::size_t>(world.lightmapPages) * BSPLightmapPageSize * BSPLightmapPageSize * BSPLightmapBytesPerLuxel, 0);
    }

    //Each submodel's surfaces are drawn grouped by texture, by the frame random tiling surfaces picked and by whether
    //they are lit; the missing texture sorts last.
    std::stable_sort(pendingSurfaces.begin(), pendingSurfaces.end(), [](const BSPPendingSurface& a, const BSPPendingSurface& b) {
        return std::tie(a.submodel, a.surface.slot, a.animation.firstFrame, a.alternateAnimation.firstFrame, a.bUnlit) < std::tie(b.submodel, b.surface.slot, b.animation.firstFrame, b.alternateAnimation.firstFrame, b.bUnlit);
    });

    //The world's visibility maps faces to surfaces of submodel 0 only.
    std::vector<std::int32_t> faceSurfaces(file.Count(BSPFaceLump, BSPFaceBytes), -1);

    world.submodels.resize(submodelCount);

    for (std::uint32_t index = 0; index < submodelCount; ++index)
    {
        const auto*     pModel        = file.Record(BSPModelLump, index, BSPModelBytes);
        const auto      mins          = ReadVector(pModel, BSPModelMinsOffset);
        const auto      maxs          = ReadVector(pModel, BSPModelMaxsOffset);
        constexpr float s_HalfExtent  = 0.5f;
        world.submodels[index].center = {mins.x * s_HalfExtent + maxs.x * s_HalfExtent,
                                         mins.y * s_HalfExtent + maxs.y * s_HalfExtent,
                                         mins.z * s_HalfExtent + maxs.z * s_HalfExtent};

        auto& submodel          = world.submodels[index];
        submodel.firstFace      = static_cast<std::uint32_t>(ReadInt32(pModel, BSPModelFirstFaceOffset));
        submodel.faceCount      = static_cast<std::uint32_t>(ReadInt32(pModel, BSPModelFaceCountOffset));
        submodel.firstFaceEntry = static_cast<std::uint32_t>(world.faces.size());

        for (auto face = submodel.firstFace; face < submodel.firstFace + submodel.faceCount; ++face)
        {
            const auto data  = ReadFace(file, face);
            const auto frame = ReadFaceFrame(file, data, ReadTexInfo(file, data.texInfo));

            BSPWorldFace entry;
            entry.normal   = frame.normal;
            entry.sTangent = frame.sTangent;
            entry.tTangent = frame.tTangent;

            world.faces.push_back(entry);
        }
    }

    for (auto& pending : pendingSurfaces)
    {
        const auto texInfo = ReadTexInfo(file, pending.data.texInfo);
        const auto texture = ResolveFaceTexture(miptexes, texInfo, pending.data);

        if (!pending.bUnlit)
            WriteFaceLightmap(file, pending.data, pending.extents, pending.surface.lightmapPage, pending.lightmapX, pending.lightmapY, world);

        pending.surface.firstIndex = static_cast<std::uint32_t>(world.indices.size());
        AppendFaceVertices(file, pending, texInfo, texture, world);
        pending.surface.indexCount = static_cast<std::uint32_t>(world.indices.size()) - pending.surface.firstIndex;

        const auto surface = static_cast<std::uint32_t>(world.surfaces.size());

        if (pending.submodel == 0)
            faceSurfaces[pending.face] = static_cast<std::int32_t>(surface);

        world.surfaces.push_back(pending.surface);

        auto& submodel = world.submodels[pending.submodel];
        auto& entry    = world.faces[submodel.firstFaceEntry + (pending.face - submodel.firstFace)];

        entry.surface   = surface;
        entry.lightmapX = pending.lightmapX;
        entry.lightmapY = pending.lightmapY;
        std::memcpy(entry.vectors, texInfo.vectors, sizeof(entry.vectors));
        std::memcpy(entry.textureMins, pending.extents.textureMins, sizeof(entry.textureMins));

        if (!submodel.surfaceCount)
            submodel.firstSurface = surface;

        ++submodel.surfaceCount;

        const bool bNewGroup = !submodel.textureGroupCount || world.textureGroups.back().slot != pending.surface.slot ||
            !SameBSPAnimation(world.textureGroups.back().animation, pending.animation) ||
            !SameBSPAnimation(world.textureGroups.back().alternateAnimation, pending.alternateAnimation) || world.textureGroups.back().bUnlit != pending.bUnlit;

        if (bNewGroup)
        {
            if (!submodel.textureGroupCount)
                submodel.firstTextureGroup = static_cast<std::uint32_t>(world.textureGroups.size());

            world.textureGroups.push_back({pending.surface.slot, texture.binding, surface, 0, texture.detailBinding, texture.scrollWidth, pending.animation, pending.alternateAnimation, pending.bUnlit});
            ++submodel.textureGroupCount;
        }

        ++world.textureGroups.back().surfaceCount;
    }

    std::vector<std::int32_t> faceWaterSurfaces(faceSurfaces.size(), -1);
    BuildBSPWater(file, pendingWater, faceWaterSurfaces, world);

    BuildBSPVisibility(file, faceSurfaces, faceWaterSurfaces, world);

    return ErrorCode::None;
}

const BSPWorldFace* FindBSPSubmodelFace(const BSPWorld& world, std::uint32_t submodel, std::uint32_t face)
{
    if (submodel >= world.submodels.size())
        return nullptr;

    const auto& model = world.submodels[submodel];

    if (face < model.firstFace || face - model.firstFace >= model.faceCount)
        return nullptr;

    return &world.faces[model.firstFaceEntry + (face - model.firstFace)];
}

Vector2 GetBSPFaceLightmapTexcoord(const BSPWorldFace& face, const Vector3& position)
{
    Vector2 lightmap;

    float* lightmaps[BSPTextureAxes] = {&lightmap.x, &lightmap.y};

    const std::uint32_t offsets[BSPTextureAxes] = {face.lightmapX, face.lightmapY};

    for (std::uint32_t axis = 0; axis < BSPTextureAxes; ++axis)
    {
        const auto* vector = face.vectors[axis];
        const auto  value  = Dot(position, {vector[0], vector[1], vector[2]}) + vector[3];

        *lightmaps[axis] = LightmapCoordinate(value, face.textureMins[axis], offsets[axis]);
    }

    return lightmap;
}

float GetBSPScrollOffset(float scrollSpeed, double sceneTime, std::uint32_t scrollWidth)
{
    const auto distance = static_cast<double>(scrollSpeed) * sceneTime;

    if (!scrollWidth || !std::isfinite(distance))
        return 0;

    return static_cast<float>(std::fmod(distance, static_cast<double>(scrollWidth)));
}

//Cell of GoldSrc's random tiling table on one axis, made non-negative.
static std::uint32_t GetRandomTileCell(std::int32_t textureMin, std::uint32_t size)
{
    const auto cell = (static_cast<std::int64_t>(textureMin) + (static_cast<std::int64_t>(size) << BSPRandomTileSizeShift)) / size % BSPRandomTileCells;

    return static_cast<std::uint32_t>(cell < 0 ? cell + BSPRandomTileCells : cell);
}

std::uint32_t GetBSPRandomTextureFrame(const std::int32_t (&textureMins)[BSPTextureAxes], std::uint32_t width, std::uint32_t height, std::uint32_t frameCount)
{
    auto value = GetRandomTileCell(textureMins[0], width) * static_cast<std::uint32_t>(BSPRandomTileCells) + GetRandomTileCell(textureMins[1], height);

    value ^= value >> BSPRandomTileHashShift1;
    value *= BSPRandomTileHashMultiplier1;
    value ^= value >> BSPRandomTileHashShift2;
    value *= BSPRandomTileHashMultiplier2;
    value ^= value >> BSPRandomTileHashShift3;

    return (value & BSPRandomTileMax) % frameCount;
}

BSPTextureFrame SelectBSPTextureFrame(const BSPWorld& world, const BSPWorldTextureGroup& group, bool bAlternate, double sceneTime)
{
    const auto& animation = bAlternate ? group.alternateAnimation : group.animation;

    if (!animation.frameCount)
        return {group.binding, group.detailBinding};

    const auto    step = std::floor(sceneTime * BSPTextureAnimationRate);
    std::uint32_t frame{};

    if (std::isfinite(step) && step > 0)
        frame = static_cast<std::uint32_t>(std::fmod(step, static_cast<double>(animation.frameCount)));

    return world.textureFrames[animation.firstFrame + frame];
}

float GetBSPSubmodelCameraDistance(const BSPSubmodel& submodel, const Matrix4& transform, const Vector3& cameraOrigin)
{
    const auto&  m        = transform.values;
    const double x        = submodel.center.x;
    const double y        = submodel.center.y;
    const double z        = submodel.center.z;
    const auto   dx       = m[0] * x + m[1] * y + m[2] * z + m[3] - cameraOrigin.x;
    const auto   dy       = m[4] * x + m[5] * y + m[6] * z + m[7] - cameraOrigin.y;
    const auto   dz       = m[8] * x + m[9] * y + m[10] * z + m[11] - cameraOrigin.z;
    const auto   distance = std::sqrt(dx * dx + dy * dy + dz * dz);
    return static_cast<float>(std::min(distance, static_cast<double>(std::numeric_limits<float>::max())));
}

std::uint32_t FindBSPLeaf(const BSPWorld& world, const Vector3& origin)
{
    auto child = world.rootNode;

    while (child >= 0)
    {
        const auto& node = world.nodes[child];

        child = node.children[Dot(origin, node.normal) - node.dist > 0 ? 0 : 1];
    }

    return static_cast<std::uint32_t>(-1 - child);
}

void FindBSPVisibleSurfaces(const BSPWorld& world, std::uint32_t leaf, std::vector<std::uint8_t>& visibleSurfaces, std::vector<std::uint8_t>& visibleWaterSurfaces)
{
    const auto leafCount = static_cast<std::uint32_t>(world.leaves.size());

    std::vector<std::uint8_t> visibleLeaves(leafCount);

    if (leaf == BSPSolidLeaf || leaf >= leafCount || world.leaves[leaf].visibilityOffset == BSPAbsentOffset)
        std::fill(visibleLeaves.begin() + std::min<std::uint32_t>(1, leafCount), visibleLeaves.end(), 1);
    else
    {
        std::vector<std::uint8_t> row(VisibilityRowBytes(world.visibleLeafCount));

        //The row was checked when the world was built.
        DecodeVisibilityRow(world.visibility.data(), world.visibility.size(), world.leaves[leaf].visibilityOffset, static_cast<std::uint32_t>(row.size()), row.data());

        for (std::uint32_t i = 0; i < world.visibleLeafCount && i + 1 < leafCount; ++i)
            visibleLeaves[i + 1] = (row[i / BSPVisibilityBitsPerByte] >> (i % BSPVisibilityBitsPerByte)) & 1;
    }

    //A visible leaf makes its ancestors visible; a face is drawn by the node owning it.
    std::vector<std::uint8_t> visibleNodes(world.nodes.size());
    std::vector<std::uint8_t> markedSurfaces(world.surfaces.size());
    std::vector<std::uint8_t> markedWaterSurfaces(world.waterSurfaces.size());

    for (std::uint32_t i = 1; i < leafCount; ++i)
    {
        if (!visibleLeaves[i])
            continue;

        const auto& visibleLeaf = world.leaves[i];

        for (auto node = visibleLeaf.parent; node >= 0 && !visibleNodes[node]; node = world.nodes[node].parent)
            visibleNodes[node] = 1;

        for (std::uint32_t mark = 0; mark < visibleLeaf.markSurfaceCount; ++mark)
        {
            const auto surface      = world.markSurfaces[visibleLeaf.firstMarkSurface + mark];
            const auto waterSurface = world.markWaterSurfaces[visibleLeaf.firstMarkSurface + mark];

            if (surface >= 0)
                markedSurfaces[surface] = 1;

            if (waterSurface >= 0)
                markedWaterSurfaces[waterSurface] = 1;
        }
    }

    visibleSurfaces.assign(world.surfaces.size(), 0);

    for (std::uint32_t i = 0; i < world.surfaces.size(); ++i)
    {
        const auto node = world.surfaceNode[i];

        visibleSurfaces[i] = markedSurfaces[i] && node >= 0 && visibleNodes[node];
    }

    visibleWaterSurfaces.assign(world.waterSurfaces.size(), 0);

    for (std::uint32_t i = 0; i < world.waterSurfaces.size(); ++i)
    {
        const auto node = world.waterSurfaceNode[i];

        visibleWaterSurfaces[i] = markedWaterSurfaces[i] && node >= 0 && visibleNodes[node];
    }
}
} // namespace GoldSrcRender
