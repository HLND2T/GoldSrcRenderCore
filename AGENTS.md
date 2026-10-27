# Repository guidance

GoldSrcRenderCore is the backend-neutral CPU reading of GoldSrc BSP and MDL assets. Start with `README.md` for the
headers, targets and build commands.

## Boundaries

- C++17 standard library only. Do not include engine, graphics API, Windows, third-party or consumer headers;
  `src/` and `include/` include only this library's own headers and standard headers.
- The asset types (`GoldSrcRender{Base,BSP,MDL}Types.h`) are part of the binary interfaces of consumers. Keep their
  layout, enumerator values, names and namespace unless a change of those interfaces is explicitly requested. Every
  type that crosses a module boundary keeps its `static_assert(IsABIStable<T>)`.
- Do not add frame, draw or renderer concepts here; they belong to the consumers.
- Test files are generated in code (`tests/GoldSrcRender{BSP,MDL}TestFile.h`). Do not commit real maps, models or
  other game data.

## Code style

- Follow the surrounding code: `p` prefix for pointers, `b` for bools, `sz` for C strings, `m_` for members, `s_` for
  file statics; file-local helpers are `static`, not in an anonymous namespace. No magic numbers, tests included.
- Comments that name GoldSrc engine functions describe the behavior being reproduced; never copy engine code.
- Format with clang-format 23.1.3 through the `format` / `format-check` targets (see `README.md`). Line endings are
  normalized before formatting; do not pass CRLF files to clang-format directly.

## Verification

Build and run `goldsrc_render_core_tests` (CTest) for every change, on x86 and x64 when the change can differ between
them, and run `format-check`. Do not commit, push or publish without authorization.
