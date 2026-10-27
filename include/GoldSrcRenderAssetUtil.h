#if !defined(GOLDSRC_RENDER_ASSET_UTIL_H)
#    define GOLDSRC_RENDER_ASSET_UTIL_H

#    ifdef _WIN32
#        pragma once
#    endif

#    include "GoldSrcRenderBSPTypes.h"
#    include "GoldSrcRenderMDLTypes.h"

#    include <cstddef>
#    include <cstdint>

//Limits, file layouts and validation of BSP and MDL assets, shared by every bridge implementation (Inplace, Client and
//Server) and by the tools that read the same files, so that all of them accept and reject exactly the same input.
namespace GoldSrcRender
{
//Bridge limits. MaxTableEntries bounds every table without a more specific limit: frame entries,
//state and resource tables, sprite frames and groups, and texture bindings.
constexpr std::size_t   MaxAssetBytes       = 256 * 1024 * 1024;
constexpr std::size_t   MaxTableEntries     = 1024 * 1024;
constexpr std::size_t   MaxVertices         = 1024 * 1024;
constexpr std::uint32_t MaxTextureDimension = 16384;

//Longest asset identifier in bytes, excluding the terminator. GoldSrc texture identifiers and model paths
//fit in MAX_QPATH (64 bytes with the terminator), which leaves room for longer file system paths.
constexpr std::size_t MaxIdentifierLength = 255;

//BSP file layout: version, then a table of (offset, length) lump entries.
constexpr std::uint32_t BSPVersion          = 30;
constexpr std::uint32_t BSPLumpTableOffset  = 4;
constexpr std::uint32_t BSPLumpEntryBytes   = 8;
constexpr std::uint32_t BSPLumpLengthOffset = 4;
constexpr std::uint32_t BSPLumpCount        = 15;
constexpr std::uint32_t BSPHeaderBytes      = BSPLumpTableOffset + BSPLumpCount * BSPLumpEntryBytes;
constexpr std::uint32_t BSPModelLump        = 14;
constexpr std::uint32_t BSPModelBytes       = 64;

//Signed 32-bit first face and face count of a model record.
constexpr std::uint32_t BSPModelFirstFaceOffset = 56;
constexpr std::uint32_t BSPModelFaceCountOffset = 60;

//MDL file header layout.
constexpr std::uint32_t MDLIdentifier       = 0x54534449;
constexpr std::uint32_t MDLVersion          = 10;
constexpr std::uint32_t MDLHeaderBytes      = 244;
constexpr std::uint32_t MDLIdentifierOffset = 0;
constexpr std::uint32_t MDLVersionOffset    = 4;
constexpr std::uint32_t MDLLengthOffset     = 72;

bool ValidVector(const Vector3& vector);

//A borrowed array may be null only when it is empty.
bool ValidArray(const void* pData, std::uint32_t count);

//Whether szString ends within maxLength bytes, reading no byte past the limit.
bool IsTerminatedWithin(const char* szString, std::size_t maxLength);

//An identifier may be null; otherwise it must end within MaxIdentifierLength bytes. No byte past the limit is read.
bool ValidIdentifier(const char* szIdentifier);

//Returns true if [offset, offset + count * stride) fits in a buffer of size bytes.
bool ValidRange(std::size_t size, std::uint32_t offset, std::uint32_t count, std::uint32_t stride);

//Little-endian read; the caller must ensure that 4 bytes are readable at pBytes + offset.
std::uint32_t ReadUInt32(const std::uint8_t* pBytes, std::size_t offset);

//The caller must ensure pBytes holds at least BSPHeaderBytes.
std::uint32_t ReadBSPLumpOffset(const std::uint8_t* pBytes, std::uint32_t lump);

std::uint32_t ReadBSPLumpLength(const std::uint8_t* pBytes, std::uint32_t lump);

//Number of submodels in a BSP file's lump table, the world being submodel 0; zero when pBytes is null or holds less
//than BSPHeaderBytes. The models' table entry is the same in every BSPLayout.
std::uint32_t CountBSPSubmodels(const std::uint8_t* pBytes, std::uint32_t size);

//Range of the face lump the submodel lists, from its model record. Returns false when the submodel or its record is
//outside the file or the range is negative; the faces themselves are not checked.
bool ReadBSPSubmodelFaces(const std::uint8_t* pBytes, std::uint32_t size, std::uint32_t submodel, std::uint32_t& firstFace, std::uint32_t& faceCount);

//Validate* functions check only the data passed in and return ErrorCode::None on success.
//Asset references and capacity limits are checked by the caller.

//Checks the whole file, the slots of the bindings and detail texture bindings (see ValidateBSPFile) and the detail
//texture scales; the bound textures are not checked.
ErrorCode ValidateBSPAsset(const BSPAssetView& asset);

//Checks the files, bindings, materials and shading overrides (see ValidateMDLAssetFiles); the bound and material
//textures are not checked.
ErrorCode ValidateMDLAsset(const MDLAssetView& asset);
} // namespace GoldSrcRender

#endif
