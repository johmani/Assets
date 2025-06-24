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

    Asset TextureImporter::Import(const std::filesystem::path& filePath)
    {
        auto path = (assetManager->desc.assetsDirectory / filePath).lexically_normal();

        Asset asset = assetManager->CreateAsset(AssetType::Texture2D);
        auto& textureAsset = asset.Add<Texture>();
        auto& assetState = asset.Get<AssetState>();
        assetState = AssetState::Loading;

        HE::Image image(path);
        nvrhi::TextureDesc desc;
        desc.width = image.GetWidth();
        desc.height = image.GetHeight();
        desc.format = nvrhi::Format::RGBA8_UNORM;
        desc.debugName = path.string();
        textureAsset.texture = assetManager->device->createTexture(desc);

        nvrhi::CommandListHandle commandList = assetManager->device->createCommandList({ .enableImmediateExecution = false });
        commandList->open();
        commandList->beginTrackingTextureState(textureAsset.texture, nvrhi::AllSubresources, nvrhi::ResourceStates::Common);
        commandList->writeTexture(textureAsset.texture, 0, 0, image.GetData(), desc.width * 4);
        commandList->setPermanentTextureState(textureAsset.texture, nvrhi::ResourceStates::ShaderResource);
        commandList->commitBarriers();
        commandList->close();
        assetManager->device->executeCommandList(commandList);
        assetManager->device->runGarbageCollection();

        assetState = AssetState::Loaded;
        assetManager->OnAssetLoaded(asset);

        return asset;
    }

    Asset TextureImporter::ImportAsync(const std::filesystem::path& filePath)
    {
        assetManager->asyncTaskCount++;

        Asset asset = assetManager->CreateAsset(AssetType::Texture2D);
        auto& assetState = asset.Get<AssetState>();
        assetState = AssetState::Loading;
        auto id = asset.Get<AssetID>().id;

        HE::Jops::SubmitTask([this, id, filePath]() {

            Asset asset = assetManager->GetAsset(id);

            auto path = (assetManager->desc.assetsDirectory / filePath).lexically_normal();

            HE::Image image(path);
            uint8_t* data = image.ExtractData();

            nvrhi::TextureDesc desc;
            desc.width = image.GetWidth();
            desc.height = image.GetHeight();
            desc.format = nvrhi::Format::RGBA8_UNORM;
            desc.debugName = filePath.string();

            Texture& textureAsset = asset.Add<Texture>();

            textureAsset.texture = assetManager->device->createTexture(desc);

            HE::Jops::SubmitToMainThread([this, id, data, desc]() {

                Asset asset = assetManager->GetAsset(id);

                nvrhi::CommandListHandle commandList = assetManager->device->createCommandList({ .enableImmediateExecution = false });

                auto& textureAsset = asset.Get<Texture>();

                commandList->open();
                commandList->beginTrackingTextureState(textureAsset.texture, nvrhi::AllSubresources, nvrhi::ResourceStates::Common);
                commandList->writeTexture(textureAsset.texture, 0, 0, data, desc.width * 4);
                commandList->setPermanentTextureState(textureAsset.texture, nvrhi::ResourceStates::ShaderResource);
                commandList->commitBarriers();
                commandList->close();
                
                assetManager->device->executeCommandList(commandList);
                assetManager->device->runGarbageCollection();

                std::free(data);
                asset.Get<AssetState>() = AssetState::Loaded;
                
                assetManager->OnAssetLoaded(asset);
                assetManager->asyncTaskCount--;
            });
        });

        return asset;
    }

    void TextureImporter::Save(Asset asset, const std::filesystem::path& metadata)
    {
        NOT_YET_IMPLEMENTED();
    }
    
    Asset TextureImporter::Create(const std::filesystem::path& filePath)
    {
        NOT_YET_IMPLEMENTED();
        return {};
    }
}