//
// Created by 24500 on 2026/10/1.
//

#pragma once


using ResourceHandle = uint32_t;

// template <class T>
// struct IsResourceHandleArray : std::false_type
// {
// };
//
// template <size_t N>
// struct IsResourceHandleArray<std::array<ResourceHandle, N>>
//     : std::true_type
// {
// };
//
// template <class T>
// concept ResourceHandleArray =
//     IsResourceHandleArray<std::remove_cvref_t<T>>::value;


class RenderGraph;

template <class T>
concept RenderPass = requires(RenderGraph& graph)
{
    // T::name;
    // T::inputSlots;
    // T::outputSlots;
    //
    // requires std::is_same_v<decltype(T::name), const std::string_view>;
    // requires ResourceHandleArray<decltype(T::inputSlots)>;
    // requires ResourceHandleArray<decltype(T::outputSlots)>;
    T::registerPass(graph);
    T::setupPass(graph);
    T::executePass(graph);
};
