#pragma once

#include <string>

// 记录网格资产引用（builtin: 或 mesh UUID），供场景保存时写回 JSON
struct MeshSource
{
    std::string value;
};
