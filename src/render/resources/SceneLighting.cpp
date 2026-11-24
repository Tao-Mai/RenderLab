#include "render/resources/SceneLighting.h"

#include <slang-rhi/shader-cursor.h>
#include "asset/BuiltinAssets.h"
#include "render/device/RhiCheck.h"
#include "render/resources/RenderResourceManager.h"
#include "render/resources/ShaderData.h"

void SceneLighting::init(rhi::IDevice& device, RenderResourceManager& resources)
{
    rhi::BufferDesc desc;
    desc.size  = kMaxLights * sizeof(LightData);
    desc.usage = rhi::BufferUsage::ShaderResource | rhi::BufferUsage::CopyDestination;
    rhiCheck(device.createBuffer(desc, nullptr, lights.writeRef()));

    rhiCheck(resources.texture(BuiltinAssets::Texture::kBrdfLut)->getDefaultView(brdfLut.writeRef()));
    rhiCheck(resources.texture(BuiltinAssets::Texture::kSheenLut)->getDefaultView(sheenLut.writeRef()));
    lutSampler = resources.sampler(BuiltinAssets::Sampler::linearClamp);
}

void SceneLighting::bindRaster(
    const rhi::ShaderCursor& root, float ambientCoefficient, const GpuEnvironmentMap* environment
) const
{
    if (!environment)
        rhiCheck(root["lightingParameters"].getDereferenced()["ambientCoefficient"].setData(ambientCoefficient));
    rhiCheck(root["lights"].setBinding(lights));

    if (const auto sheen = root["sheenLUT"]; sheen.isValid())
    {
        rhiCheck(sheen.setBinding(sheenLut));
        rhiCheck(root["sheenLUTSampler"].setBinding(lutSampler));
    }
    if (environment)
    {
        rhiCheck(root["irradianceMap"].setBinding(environment->irradiance));
        rhiCheck(root["irradianceMapSampler"].setBinding(environment->irradianceSampler));
        rhiCheck(root["prefilteredSpecularMap"].setBinding(environment->prefilteredSpecular));
        rhiCheck(root["prefilteredSpecularMapSampler"].setBinding(environment->prefilteredSampler));
        rhiCheck(root["brdfLUT"].setBinding(brdfLut));
        rhiCheck(root["brdfLUTSampler"].setBinding(lutSampler));
        if (root["radianceMap"].isValid())
            environment->bindRadiance(root);
    }
}
