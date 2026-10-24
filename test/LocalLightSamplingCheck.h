#pragma once

#include "core/Logger.h"
#include "scene/SceneManager.h"
#include "scene/SceneTypes.h"

#include <cmath>

// The fixtures place a receiver inside both colocated lights' ranges and inner cone.
template <class Sample>
void checkLocalLightSampling(SceneManager& scenes, AActor& pointActor, Sample sample)
{
    auto&       point         = *pointActor.getComponent<PointLightComponent>();
    const float originalRange = point.range;
    const float pointOnly     = sample();
    CHECK(std::isfinite(pointOnly) && pointOnly > 0.0f);

    auto& spotActor = scenes.createActor("ALight");
    replaceLightComponent(*spotActor.getComponent<LightComponent>(), std::make_unique<SpotLightComponent>());
    spotActor.transform().position = pointActor.transform().position;
    auto& spot                     = *spotActor.getComponent<SpotLightComponent>();
    spot.color                     = point.color;
    spot.luminousIntensity         = point.luminousIntensity * 3.0f;
    spot.range                     = originalRange;
    spot.cosInner                  = 0.5f;
    spot.cosOuter                  = 0.25f;

    constexpr float kRelativeTolerance = 0.005f;
    const auto expect = [&](float expected) { CHECK(std::abs(sample() - expected) <= expected * kRelativeTolerance); };
    expect(pointOnly * 4.0f);
    point.enabled = false;
    expect(pointOnly * 3.0f);

    spot.cosInner            = 1.0f;
    const float softSpotOnly = sample();
    CHECK(softSpotOnly > 0.0f && softSpotOnly < pointOnly * 3.0f);
    point.enabled = true;
    expect(pointOnly + softSpotOnly);

    point.range = 0.1f;
    expect(softSpotOnly);
    spot.range = 0.1f;
    CHECK(sample() == 0.0f);
    point.range = originalRange;
    spot.range  = originalRange;

    spot.cosOuter = 0.99999f;
    expect(pointOnly);
    spot.cosInner = 0.5f;
    spot.cosOuter = 0.5f;
    expect(pointOnly * 4.0f);
    spotActor.transform().setRotationEulerDegrees({0, 180, 0});
    expect(pointOnly);
    spotActor.transform().setRotationEulerDegrees({0, 0, 0});
    spot.color = glm::vec3{0.0f};
    expect(pointOnly);
    spot.color   = point.color;
    spot.enabled = false;
    expect(pointOnly);

    scenes.deleteActor(spotActor.selectionId());
    expect(pointOnly);
    LOG_INFO(
        "Joint point/spot sampling passed: range, inner/outer cones, hard edge, inverse PMF, disabled/black lights"
    );
}
