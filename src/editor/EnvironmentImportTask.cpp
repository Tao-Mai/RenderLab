#include "editor/EnvironmentImportTask.h"
#include <chrono>
#include "core/Logger.h"

EnvironmentImportTask::~EnvironmentImportTask() { DCHECK(!future.valid()); }

void EnvironmentImportTask::start(ImportEnvironmentMapSetting settings)
{
    DCHECK(!future.valid());
    stop   = {};
    future = std::async(
        std::launch::async,
        [settings = std::move(settings), token = stop.get_token()]
        { return AssetImporter::prepareEnvironmentMap(settings, token); }
    );
}

void EnvironmentImportTask::cancel() { stop.request_stop(); }

std::optional<std::string> EnvironmentImportTask::poll()
{
    if (!future.valid() || future.wait_for(std::chrono::seconds{0}) != std::future_status::ready)
        return std::nullopt;
    auto prepared = future.get();
    if (!prepared || stop.stop_requested())
        return "Environment import canceled";
    return "Imported: " + AssetImporter::saveEnvironmentMap(std::move(*prepared)).value;
}

void EnvironmentImportTask::shutdown() noexcept
{
    if (!future.valid())
        return;
    stop.request_stop();
    future.wait();
    future = {};
}
