# C++26 reflection for clang-p2996 + MSVC STL

`include/meta` is the `<meta>` header of [bloomberg/clang-p2996](https://github.com/bloomberg/clang-p2996)
(`libcxx/include/meta`, Apache-2.0 WITH LLVM-exception), adapted to the MSVC STL by
`tools/gen_meta_msstl.py`. Don't edit it by hand — regenerate it after updating the compiler:

```
python tools/gen_meta_msstl.py D:/toolchains/clang-p2996
```

It is appended to the `std` module built by `cmake/std_module.cmake`, so `import std;`
(or `import std.compat;`) provides `std::meta` like in C++26.

Adaptations:

- libc++ internals (`<__config>`, `<__ranges/*>`) replaced with standard headers.
- `std::is_consteval_only` / `is_consteval_only_v` and P2591 `string + string_view` added
  (missing from the MSVC STL).
- `^^ptrdiff_t` / `^^nullptr_t` replaced: the MSVC STL declares them as using-declarations,
  which can't be reflected.
- `to_chars_result::operator bool` (P2497) and `make_signed<float>` usages avoided.
- `std::meta::source_location_of` is unsupported: it needs libc++'s
  `std::source_location::__impl`. Using it is a compile error.

Consteval-only types (`std::meta::info`) also need a few MSVC STL helpers to be
`constexpr` (e.g. `std::vector<std::meta::info>`); `tools/gen_stl_overlay.py` generates
patched copies of those headers into the build directory at configure time.
