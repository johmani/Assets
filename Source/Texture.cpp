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

    Asset* TextureImporter::Import(const AssetMetadata& metadata)
    {
        auto path = (assetManager->desc.assetsDirectory / metadata.filePath).lexically_normal();

        auto textureAsset = (Texture*)assetManager->EmplaceBack(AssetType::Texture2D);
        textureAsset->state = AssetState::Loading;

        HE::Image image(path);
        nvrhi::TextureDesc desc;
        desc.width = image.GetWidth();
        desc.height = image.GetHeight();
        desc.format = nvrhi::Format::RGBA8_UNORM;
        desc.debugName = path.string();
        textureAsset->texture = assetManager->device->createTexture(desc);


        nvrhi::CommandListHandle commandList = assetManager->device->createCommandList({ .enableImmediateExecution = false });
        commandList->open();
        commandList->beginTrackingTextureState(textureAsset->texture, nvrhi::AllSubresources, nvrhi::ResourceStates::Common);
        commandList->writeTexture(textureAsset->texture, 0, 0, image.GetData(), desc.width * 4);
        commandList->setPermanentTextureState(textureAsset->texture, nvrhi::ResourceStates::ShaderResource);
        commandList->commitBarriers();
        commandList->close();
        assetManager->device->executeCommandList(commandList);
        assetManager->device->runGarbageCollection();

        textureAsset->state = AssetState::Loaded;
        assetManager->OnAssetLoaded(textureAsset);

        return textureAsset;
    }

    Asset* TextureImporter::ImportAsync(const AssetMetadata& metadata)
    {
        assetManager->asyncTaskCount++;

        auto textureAsset = (Texture*)assetManager->EmplaceBack(AssetType::Texture2D);
        textureAsset->state = AssetState::Loading;

        HE::Jops::SubmitTask([this, textureAsset, metadata]() {

            auto path = (assetManager->desc.assetsDirectory / metadata.filePath).lexically_normal();

            HE::Image image(path);
            uint8_t* data = image.ExtractData();

            nvrhi::TextureDesc desc;
            desc.width = image.GetWidth();
            desc.height = image.GetHeight();
            desc.format = nvrhi::Format::RGBA8_UNORM;
            desc.debugName = metadata.filePath.string();

            textureAsset->texture = assetManager->device->createTexture(desc);

            HE::Jops::SubmitToMainThread([this, textureAsset, data, desc]() {

                nvrhi::CommandListHandle commandList = assetManager->device->createCommandList({ .enableImmediateExecution = false });

                commandList->open();
                commandList->beginTrackingTextureState(textureAsset->texture, nvrhi::AllSubresources, nvrhi::ResourceStates::Common);
                commandList->writeTexture(textureAsset->texture, 0, 0, data, desc.width * 4);
                commandList->setPermanentTextureState(textureAsset->texture, nvrhi::ResourceStates::ShaderResource);
                commandList->commitBarriers();
                commandList->close();
                
                assetManager->device->executeCommandList(commandList);
                assetManager->device->runGarbageCollection();

                std::free(data);
                textureAsset->state = AssetState::Loaded;
                
                assetManager->OnAssetLoaded(textureAsset);
                assetManager->asyncTaskCount--;
            });
        });

        return textureAsset;
    }

    void TextureImporter::Save(Asset* asset, const AssetMetadata& metadata)
    {
        NOT_YET_IMPLEMENTED();
    }
    
    Asset* TextureImporter::Create(const std::filesystem::path& filePath)
    {
        NOT_YET_IMPLEMENTED();
        return nullptr;
    }
}