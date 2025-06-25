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

        HE::Image image(path);
        nvrhi::TextureDesc desc;
        desc.width = image.GetWidth();
        desc.height = image.GetHeight();
        desc.format = nvrhi::Format::RGBA8_UNORM;
        desc.debugName = path.string();
        texture.texture = assetManager->device->createTexture(desc);

        nvrhi::CommandListHandle commandList = assetManager->device->createCommandList({ .enableImmediateExecution = false });
        commandList->open();
        commandList->beginTrackingTextureState(texture.texture, nvrhi::AllSubresources, nvrhi::ResourceStates::Common);
        commandList->writeTexture(texture.texture, 0, 0, image.GetData(), desc.width * 4);
        commandList->setPermanentTextureState(texture.texture, nvrhi::ResourceStates::ShaderResource);
        commandList->commitBarriers();
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

            nvrhi::TextureDesc desc;
            desc.width = image.GetWidth();
            desc.height = image.GetHeight();
            desc.format = nvrhi::Format::RGBA8_UNORM;
            desc.debugName = filePath.string();

            Texture& texture = asset.Add<Texture>();
            texture.texture = assetManager->device->createTexture(desc);

            HE::Jops::SubmitToMainThread([this, handle, data, desc]() {

                Asset asset = assetManager->FindAsset(handle);
                auto& texture = asset.Get<Texture>();
                auto& state = asset.Get<AssetState>();

                nvrhi::CommandListHandle commandList = assetManager->device->createCommandList({ .enableImmediateExecution = false });

                commandList->open();
                commandList->beginTrackingTextureState(texture.texture, nvrhi::AllSubresources, nvrhi::ResourceStates::Common);
                commandList->writeTexture(texture.texture, 0, 0, data, desc.width * 4);
                commandList->setPermanentTextureState(texture.texture, nvrhi::ResourceStates::ShaderResource);
                commandList->commitBarriers();
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