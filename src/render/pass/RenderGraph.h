//
// Created by 24500 on 2026/10/1.
//

#pragma once
#include <cstdint>
#include <array>
#include <vector>
#include <memory>
#include <string>
#include <unordered_map>
#include "PassDefine.h"
#include "core/Logger.h"

class RenderGraph
{
public:
    using ResourceHandle = uint32_t;

    struct PassInfo
    {
        // 用来校验
        std::vector<ResourceHandle> inputResourceHandles;
        std::vector<ResourceHandle> outputResourceHandles;
    };

    struct ResourceDesc
    {

    };

    void init();

    std::tuple<std::vector<ResourceHandle>, std::vector<ResourceHandle>>
    registerPass(const std::string_view& name, int inputSlotCount, int outputSlotCount)
    {
        const auto it = passInfos.find(name);
        CHECK(it == passInfos.end());

        std::vector<ResourceHandle> inputResourceHandles;
        std::vector<ResourceHandle> outputResourceHandles;

        for (int i = 0; i < inputSlotCount; i++)
        {
            inputResourceHandles.push_back(globalInputSlots.size());
            globalInputSlots.push_back(nullptr);
        }

        for (int i = 0; i < outputSlotCount; i++)
        {
            outputResourceHandles.push_back(globalOutputSlots.size());
            globalOutputSlots.push_back(nullptr);
        }

        // 存多一份用来校验
        auto passInfo                  = std::make_unique<PassInfo>();
        passInfo->inputResourceHandles = inputResourceHandles;
        passInfo->inputResourceHandles = outputResourceHandles;

        passInfos.insert_or_assign(name, std::move(passInfo));

        return std::make_tuple(inputResourceHandles, outputResourceHandles);
    }

    // template <class T>
    //     requires RenderPass<T>
    // void addPass()
    // {
    //     auto it = passInfos.find(T::name);
    //     CHECK(it == passInfos.end());
    //
    // auto passInfo                = std::make_unique<PassInfo>();
    // passInfo->inputSlotBeginIdx  = globalInputSlots.size();
    // passInfo->outputSlotBeginIdx = globalOutputSlots.size();
    //
    // passInfos.insert_or_assign(T::name, passInfo);
    //
    // auto& inputSlots  = T::input;
    // auto& outputSlots = T::output;
    //
    // for (auto& slot : inputSlots)
    // {
    //     slot = globalInputSlots.size();
    //     globalInputSlots.push_back(nullptr);
    // }
    //
    // for (auto& slot : outputSlots)
    // {
    //     slot = globalOutputSlots.size();
    //     globalOutputSlots.push_back(nullptr);
    // }
    // }

private:
    std::unordered_map<std::string_view, std::unique_ptr<PassInfo>> passInfos;

    std::vector<std::unique_ptr<ResourceDesc>> globalInputSlots;
    std::vector<std::unique_ptr<ResourceDesc>> globalOutputSlots;
};
