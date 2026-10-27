#if !defined(GOLDSRC_RENDER_BASE_TYPES_H)
#    define GOLDSRC_RENDER_BASE_TYPES_H

#    ifdef _WIN32
#        pragma once
#    endif

#    include <array>
#    include <cstdint>
#    include <type_traits>

//Basic types shared by every GoldSrcRender asset and the bridge contract (GoldSrcRenderBridgeTypes.h), which are ABI
//stable across modules and build configurations: they hold plain data only.
//Coordinates use GoldSrc world units, angles are degrees, UV (0,0) is top-left.
//Matrices are row-major and transform column vectors; translation is the last column.
//Colors are normalized RGBA in the explicitly selected texture/frame color space.
namespace GoldSrcRender
{
using AssetId = std::uint64_t;

constexpr AssetId       InvalidAssetId = 0;
constexpr std::uint32_t SkyFaceCount   = 6;
constexpr std::uint32_t RGBABytes      = 4;

//Basic math and color types
struct Vector2
{
    float x{};
    float y{};
};

struct Vector3
{
    float x{};
    float y{};
    float z{};
};

struct Color
{
    float r{1};
    float g{1};
    float b{1};
    float a{1};
};

struct Matrix4
{
    std::array<float, 16> values{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
};

//Assets
//Every asset view carries szIdentifier, a name for the asset such as a GoldSrc texture identifier or model path.
//It is bounded by the bridge limits, and null means the asset has no identifier. The bridge stores it with each
//version but neither requires it to be unique nor looks assets up by it; AssetId remains the handle. In recorded
//frames it is never null, and an asset without an identifier has an empty one.
struct TextureBinding
{
    std::uint32_t slot{};
    AssetId       texture{};
};

//Errors
//Like NTSTATUS, zero means success and every non-zero value names a specific failure.
enum class ErrorCode : std::uint32_t
{
    None = 0,
    InvalidScope,
    MissingState,
    FrozenState,
    InvalidData,
    MissingAsset,
    WrongAssetType,
    InvalidRevision,
    LimitExceeded,
    ReceiverFailure,
    OutOfMemory,
    Unsupported,
    ProtocolError,
    ProtocolMismatch,
    Disconnected,
    Timeout,
    DeviceLost,
    FrameTooLarge,
    SessionAborted
};

constexpr bool Succeeded(ErrorCode code)
{
    return code == ErrorCode::None;
}

struct Error
{
    ErrorCode     code{ErrorCode::None};
    std::uint64_t frameId{};
    AssetId       asset{};
};

//Standard layout and trivially copyable types have the same layout in every module and build configuration,
//which rules out owning and standard library container types. Every type reachable from IGoldSrcRenderBridge
//or IFrameReceiver must satisfy this, including the element types of view arrays.
template <typename T>
constexpr bool IsABIStable = std::is_standard_layout_v<T> && std::is_trivially_copyable_v<T>;

static_assert(IsABIStable<TextureBinding> && IsABIStable<Error>);
} // namespace GoldSrcRender

#endif
