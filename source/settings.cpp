#include "settings.hpp"

#include <cstdio>

#include <borealis.hpp>
#include <extern/nlohmann/json.hpp>

namespace
{

constexpr const char* kPath = "sdmc:/switch/switch-feed.json";

settings::Values values;

} // namespace

namespace settings
{

Values& get() { return values; }

void load()
{
    FILE* file = fopen(kPath, "rb");
    if (!file)
    {
        brls::Logger::info("settings: none stored, using defaults");
        return;
    }

    std::string text;
    char        buffer[512];
    size_t      got = 0;
    while ((got = fread(buffer, 1, sizeof(buffer), file)) > 0)
        text.append(buffer, got);
    fclose(file);

    try
    {
        const auto json = nlohmann::json::parse(text);

        // Each field read independently: a file written by an older build is
        // missing keys, and that should cost the default rather than the lot.
        if (json.contains("region") && json["region"].is_string())
            values.region = json["region"].get<std::string>();
        if (json.contains("volume") && json["volume"].is_number_integer())
            values.volume = json["volume"].get<int>();
        if (json.contains("muted") && json["muted"].is_boolean())
            values.muted = json["muted"].get<bool>();

        brls::Logger::info("settings: region={} volume={} muted={}", values.region, values.volume,
                           values.muted);
    }
    catch (const std::exception& e)
    {
        brls::Logger::warning("settings: unreadable ({}), using defaults", e.what());
    }
}

void save()
{
    const nlohmann::json json = {
        { "region", values.region },
        { "volume", values.volume },
        { "muted", values.muted },
    };

    const std::string text = json.dump(2);

    FILE* file = fopen(kPath, "wb");
    if (!file)
    {
        brls::Logger::warning("settings: could not write {}", kPath);
        return;
    }

    fwrite(text.data(), 1, text.size(), file);
    fclose(file);
}

} // namespace settings
