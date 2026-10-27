# GoldSrcRenderCore

Backend-neutral CPU reading of GoldSrc assets: the BSP30 (and Blue Shift) world format and studio models (MDL
version 10). The library validates files, builds the geometry, lightmaps, texture groups and visibility of a map,
builds studio models with their skins and textures, and defines the plain-data asset types that callers pass in.

It depends on the C++17 standard library only: no engine, graphics API, Windows or third-party code.

## Contents

| Header | Contents |
| --- | --- |
| `GoldSrcRenderBaseTypes.h` | `AssetId`, `Vector2`, `Vector3`, `Color`, `Matrix4`, `ErrorCode`, `Error`, `TextureBinding` and the other types shared by every asset |
| `GoldSrcRenderBSPTypes.h` | `BSPLayout`, `DetailTextureBinding`, `BSPAssetView` |
| `GoldSrcRenderMDLTypes.h` | `StudioShadingParams`, `StudioShadingOverrides`, `StudioMaterial`, `MDLAssetView` |
| `GoldSrcRenderAssetUtil.h` | Asset limits, bounded readers and `ValidateBSPAsset` / `ValidateMDLAsset` |
| `GoldSrcRenderBSP.h` | `ValidateBSPFile`, `BuildBSPWorld`, texture animation and scrolling, leaves and visible surfaces |
| `GoldSrcRenderMDL.h` | `ValidateMDLFile`, `ValidateMDLTextureFile`, `BuildMDLModel`, `ResolveMDLTexture`, `ParseMDLPackedTextureName` |

The `*View` types borrow their arrays and strings for the duration of a call. Every type that crosses a module
boundary is standard layout and trivially copyable (checked with `static_assert`): these types are also part of
binary interfaces of consumers, so their layout, enumerator values and names do not change.

## Building

Requires CMake 3.20 and a C++17 compiler; continuous integration builds with MSVC (Windows x86 and x64)
and GCC (Linux).

```bash
cmake -S . -B build
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure
```

The tests (`goldsrc_render_core_tests`) build their BSP and MDL files in code; no game data is needed.

### As a subproject

```cmake
add_subdirectory(path/to/GoldSrcRenderCore)
target_link_libraries(MyTarget PRIVATE GoldSrcRenderCore)
```

| Target | Kind | Contents |
| --- | --- | --- |
| `GoldSrcRenderCore` | static library | The library; its include directory is `include/` |
| `GoldSrcRenderCoreTestFiles` | interface | Generated BSP / MDL test files and comparison helpers (`tests/`), for the tests of projects that read these assets |

A parent project that already defines `GoldSrcRenderCore` keeps its own targets: the subproject adds nothing then.
`GOLDSRCRENDERCORE_BUILD_TESTS` defaults to `ON` in a standalone build and `OFF` in a subproject.

### Formatting

Sources follow the DiligentCore-derived style of `.clang-format`, checked with clang-format 23.1.3 through
[MetaHookSv/FormatValidation](https://github.com/MetaHookSv/FormatValidation):

```bash
python -m pip install clang-format==23.1.3
cmake -S . -B build/format -DFORMAT_VALIDATION_ONLY=ON
cmake --build build/format --target format-check   # or --target format to apply
```

## Users

- [UtilGoldSrcAssetConverter](https://github.com/HLND2T/UtilGoldSrcAssetConverter): GoldSrc BSP to glTF 2.0 / GLB.

## License

MIT, see [LICENSE](LICENSE) and [NOTICE](NOTICE). Half-Life and GoldSrc are trademarks of Valve Corporation; this
project is not affiliated with or endorsed by Valve.
