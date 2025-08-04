#include "HydraEngine/Base.h"

import Assets;
import HE;
import nvrhi;
import std;

namespace Assets {

    TextureImporter::TextureImporter(AssetManager* am)
        : assetManager(am)
    {
    }

    Asset TextureImporter::Import(AssetHandle handle, const std::filesystem::path& filePath)
    {
        auto path = (assetManager->desc.assetsDirectory / filePath).lexically_normal();

        Asset asset = assetManager->CreateAsset(handle);
        auto& texture = asset.Add<Texture>();
        auto& assetState = asset.Get<AssetState>();
        assetState = AssetState::Loading;

        bool isHDR = filePath.extension() == ".hdr";

        HE::Image image(path);
        nvrhi::TextureDesc desc;
        desc.width = image.GetWidth();
        desc.height = image.GetHeight();
        desc.format = isHDR ? nvrhi::Format::RGB32_FLOAT : nvrhi::Format::RGBA8_UNORM;
        desc.debugName = path.string();
        desc.initialState = nvrhi::ResourceStates::ShaderResource;
        desc.keepInitialState = true;
        texture.texture = assetManager->device->createTexture(desc);

        int bytesPerPixel = isHDR ? 3 * sizeof(float) : 4;
        int rowPitch = desc.width * bytesPerPixel;

        nvrhi::CommandListHandle commandList = assetManager->device->createCommandList({ .enableImmediateExecution = false });
        commandList->open();
        commandList->writeTexture(texture.texture, 0, 0, image.GetData(), rowPitch);
        commandList->close();
        assetManager->device->executeCommandList(commandList);
        assetManager->device->runGarbageCollection();

        assetState = AssetState::Loaded;
        assetManager->OnAssetLoaded(asset);

        return asset;
    }

    Asset TextureImporter::ImportAsync(AssetHandle handle, const std::filesystem::path& filePath)
    {
        assetManager->asyncTaskCount++;

        Asset asset = assetManager->CreateAsset(handle);
        auto& assetState = asset.Get<AssetState>();
        assetState = AssetState::Loading;

        HE::Jops::SubmitTask([this, handle, filePath]() {

            Asset asset = assetManager->GetAsset(handle);

            auto path = (assetManager->desc.assetsDirectory / filePath).lexically_normal();
            HE::Image image(path);
            uint8_t* data = image.ExtractData();

            bool isHDR = filePath.extension() == ".hdr";

            nvrhi::TextureDesc desc;
            desc.width = image.GetWidth();
            desc.height = image.GetHeight();
            desc.format = isHDR ? nvrhi::Format::RGB32_FLOAT : nvrhi::Format::RGBA8_UNORM;
            desc.debugName = filePath.string();
            desc.initialState = nvrhi::ResourceStates::ShaderResource;
            desc.keepInitialState = true;

            Texture& texture = asset.Add<Texture>();
            texture.texture = assetManager->device->createTexture(desc);

            HE::Jops::SubmitToMainThread([this, handle, data, desc, isHDR]() {

                Asset asset = assetManager->FindAsset(handle);
                auto& texture = asset.Get<Texture>();
                auto& state = asset.Get<AssetState>();

                nvrhi::CommandListHandle commandList = assetManager->device->createCommandList({ .enableImmediateExecution = false });

                int bytesPerPixel = isHDR ? 3 * sizeof(float) : 4;
                int rowPitch = desc.width * bytesPerPixel;

                commandList->open();
                commandList->writeTexture(texture.texture, 0, 0, data, rowPitch);
                commandList->close();
                
                assetManager->device->executeCommandList(commandList);
                assetManager->device->runGarbageCollection();

                std::free(data);
                state = AssetState::Loaded;
                
                assetManager->OnAssetLoaded(asset);
                assetManager->asyncTaskCount--;
            });
        });

        return asset;
    }

    Asset TextureImporter::Create(AssetHandle handle, const std::filesystem::path& filePath)
    {
        NOT_YET_IMPLEMENTED();
        return {};
    }

    void TextureImporter::Save(Asset asset, const std::filesystem::path& filePath)
    {
        NOT_YET_IMPLEMENTED();
    }
}