"""Compare the ported numerical kernels with a caller-supplied Falcor checkout.

Usage: python test/FalcorPostProcessSourceCheck.py <Falcor-source-directory>
"""

import re
import sys
from pathlib import Path


def function(source, name):
    match = re.search(r"\b(?:float[234]?|void)\s+" + name + r"\s*\(", source)
    assert match, name
    start = source.index("{", match.start())
    depth = 1
    end = start + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


def tokens(source):
    source = re.sub(r"//[^\n]*|/\*.*?\*/", "", source, flags=re.S)
    return re.sub(r"\s+", "", source)


def rename(source, names):
    for before, after in names.items():
        source = re.sub(r"\b" + before + r"\b", after, source)
    return source


def main():
    falcor = Path(sys.argv[1])
    root = Path(__file__).resolve().parents[1]
    original = (falcor / "Source/RenderPasses/SimplePostFX/SimplePostFX.cs.slang").read_text()
    port = (root / "passes/postprocess/falcor/simple_post_fx.slang").read_text()
    names = {
        "gSrc": "sourceImage", "gBloomed": "bloomedImage", "gDst": "destinationImage",
        "gLinearSampler": "linearSampler", "uv_circular": "circularUv",
        "r_squared": "radiusSquared", "squashed_luma": "squashedLuma",
    }
    for name in ["Resolution", "InvRes", "BloomAmount", "VignetteAmount", "ChromaticAberrationAmount",
                 "SaturationCurve", "ColorOffset", "ColorScale", "ColorPower", "Star", "StarDir1",
                 "StarDir2", "StarDir3", "Wipe", "InPlace"]:
        names["g" + name] = "parameters." + name[0].lower() + name[1:]
    for name in ["w0", "x1", "w1", "x2", "w2", "x1u", "x2u", "x1v", "x2v"]:
        names[name] = "k" + name[0].upper() + name[1:]
    original = original.replace("gBarrelDistort.x", "parameters.barrelScale")
    original = original.replace("gBarrelDistort.y", "parameters.barrelAmount")
    for component, field in [("x", "saturationQuadratic"), ("y", "saturationLinear"), ("z", "saturationConstant")]:
        original = original.replace("gSaturationCurve." + component, "parameters." + field)
    original = rename(original, names)
    for name in ["blurFilter2x2", "blurFilter3x3", "blurFilter5x5"]:
        assert tokens(function(original, name)) == tokens(function(port, name)), name
    # Each conditional compilation branch has a computeMain entry point.
    entries = [m.start() for m in re.finditer(r"void computeMain\(", port)]
    for index, name in enumerate(["downsample", "upsample", "runPostFX"]):
        expected = function(original, name)
        actual = function(port[entries[index]:], "computeMain")
        expected = expected.replace("if (parameters.inPlace)", "if (parameters.inPlace != 0)")
        actual = re.sub(r"if\s*\(parameters.bypass != 0\)\s*\{[^}]*\}", "", actual)
        assert tokens(expected) == tokens(actual), name
    original = (falcor / "Source/RenderPasses/ToneMapper/ToneMapping.ps.slang").read_text()
    port = (root / "passes/postprocess/falcor/tonemapping.slang").read_text()
    names = {"gParams": "parameters"}
    names.update({name: "k" + name for name in "ABCDEF"})
    original = rename(original, names)
    # 'const' on the curve coefficients does not change the numerical expression.
    port = re.sub(r"\bconst\s+", "", port)
    for name in ["calcLuminance", "toneMapLinear", "toneMapReinhard", "toneMapReinhardModified",
                 "toneMapHejiHableAlu", "applyUc2Curve", "toneMapHableUc2", "toneMapAces"]:
        assert tokens(function(original, name)) == tokens(function(port, name)), name
    assert "kExposureKey=0.042" in tokens(port) and "kLuminanceLod=16.0" in tokens(port)
    assert "float3(0.2126f,0.7152f,0.0722f)" in tokens(
        (root / "passes/postprocess/falcor/simple_post_fx.slang").read_text()
    )
    print("Falcor source parity passed: 3 blur filters, 3 effect kernels, 8 tone mapping functions.")


if __name__ == "__main__":
    main()
