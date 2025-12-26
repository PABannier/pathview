#pragma once

#include "PolygonLoader.h"
#include "JSONPolygonLoader.h"
#include "ProtobufPolygonLoader.h"

#include <memory>
#include <string>
#include <filesystem>

class PolygonLoaderFactory {
public:
    static std::unique_ptr<PolygonLoader> CreateLoader(const std::string& filePath) {
        std::string extension = std::filesystem::path(filePath).extension().string();

        if (extension == ".json") {
            return std::make_unique<JSONPolygonLoader>();
        } else if (extension == ".pb" || extension == ".proto") {
            return std::make_unique<ProtobufPolygonLoader>();
        }

        return nullptr;
    }
};
