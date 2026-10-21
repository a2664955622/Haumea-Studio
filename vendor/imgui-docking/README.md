# Dear ImGui, docking branch

Unmodified copy of Dear ImGui `v1.92.2b-docking` (tag commit
`1f7f1f54af38b0350d8c0008b096a9af6de299c7`), the docking build of the same
1.92.2b release librw bundles in `skeleton/imgui`.

`tools/release/patch_librw_haumea.py` copies these files over librw's
`skeleton/imgui`, so Haumea's panels can be docked. Builds made against an
unpatched librw still work; the editor then falls back to floating panels.

To update, replace the files with those from a newer `-docking` tag that
matches the version librw ships.

`backends/` holds the Win32 and DirectX 9 backends from the same tag, for the
Windows D3D9 build (premake5.lua and tools/release/build-windows-mingw.sh use
them in place of librw's imgui_impl_rw). One change, marked "Haumea" in
imgui_impl_dx9.cpp/.h: ImGui_ImplDX9_SetTextureResolver, so the editor can pass
its rw::Texture pointers as ImTextureIDs while the font atlas stays the
backend's own IDirect3DTexture9.
