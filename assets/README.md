# RenderLab asset layout

`config/config.json` is relative to the repository root. Its asset root is
`../assets`. The asset root is also the descriptor root.

Descriptors are grouped directly under `assets/<AssetClassName>/`. The initial scene is
`assets/Scene/default.json`. Source glTF models and their external textures are
stored under `resource/`. Shader descriptor binary paths are relative to the
process working directory, where CMake places the compiled shaders.

The editor saves the active scene to its descriptor file. Builtin meshes such
as `sphere` and `plane` are referenced by ID and do not need mesh descriptors.

Scenes store `actors` as `{"TypeName": {...}}`, using registered type names as
keys. Actors own polymorphic `components` in the same format, with exactly one
type key per non-null pointer. Scene fields use C++ member names
and IDs use `{"value": "..."}`. Other asset descriptor formats are unchanged.

Texture imports write ready-to-upload pixels to `assets/binary/texture/<id>.bin`.
`resource/lut_ggx.png` is imported as the linear RG8 `lut_ggx` texture; `config/config.json`
records its texture and sampler IDs under `renderer.brdfLut` so the renderer loads them at startup.
Mesh imports write geometry to `assets/binary/geometry/<id>.bin`.
Texture descriptors keep the source `path`, the generated `binary` path, the
pixel `format`, `colorSpace`, `width`, `height`, `mipLevels`, and the `layout` (`Image2D` or `Cubemap`). Runtime loading reads
only the binary. Paths in descriptors are relative to `assets/`; source files
in `resource/` use `../resource/...`. The `assets/` directory
is ignored by Git.

From the repository root, use:

`cmake-build-default/RenderLab.exe --import-texture <source-image>`

`cmake-build-default/RenderLab.exe --import-environmentmap <source.exr|source.png|source.jpg>`

`cmake-build-default/RenderLab.exe --import-mesh <source.gltf|source.glb>`

The first import uses the source filename stem as its ID. If that ID exists,
later imports use `stem1`, `stem2`, and so on.
The command prints the assigned ID. Meshes must be imported before use; runtime
loading reads their generated geometry.

`--import-texture` stores decoded images as 2D textures, preserving the channel
count of 8-bit images. EXR channel storage
determines whether the result uses `RGBA16F` or `RGBA32F`. EXR defaults to
linear color space; ordinary images default to sRGB. Callers may override
`importTexture` color space for linear data maps.
`--import-environmentmap` converts an image to a Cubemap texture as radiance, then creates
an `EnvironmentMap::Desc` that references radiance, irradiance, and a GGX
prefiltered specular Cubemap. Radiance and prefiltered specular each have a
complete mip chain; prefiltered roughness runs from 0 at mip 0 to 1 at the last
mip. Radiance mips average linear pixels before encoding. Irradiance and GGX
prefilter samples select a continuous radiance LOD from their PDF-derived solid
angle divided by the average base-level cubemap texel solid angle, using log4.
The configured GGX sample count applies to prefilter mip 1 and doubles at each
higher mip.
Environment and material texture fields use `TextureBinding` objects with
`TextureID` and `SamplerID`. Builtin `linearRepeat` suits tiling material UVs;
`linearClamp` suits cubemaps and LUTs. `nearestRepeat` suits tiling pixel art,
while `nearestClamp` suits discrete lookup or mask textures.
Environment imports inspect EXR channel storage. HALF RGB channels produce
`RGBA16F`; FLOAT RGB channels produce `RGBA32F`. The importer converts
a 2:1 latitude-longitude EXR into six square cubemap faces. For an EXR marked
`CUBE`, its six vertically stacked faces are reordered to Vulkan face axes.
Pixels are stored mip first, largest to smallest. Within each mip, faces are
stored in +X, -X, +Y, -Y, +Z, -Z order, each face row major with no padding.
The importer writes linear RGBA32F for irradiance and prefiltered specular.
`grasslands_sunset_4k.exr` has no `envmap` attribute, so its 2:1 dimensions
identify it as latitude-longitude when imported as an environment map.
