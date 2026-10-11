#pragma once
#include <future>
#include <stop_token>
#include "asset/AssetImporter.h"
class EnvironmentImportTask
{
public:
    ~EnvironmentImportTask();
    void                       start(ImportEnvironmentMapSetting settings);
    void                       cancel();
    std::optional<std::string> poll();
    void                       shutdown() noexcept;
    bool                       busy() const { return future.valid(); }

private:
    std::stop_source                                                  stop;
    std::future<std::optional<AssetImporter::PreparedEnvironmentMap>> future;
};
