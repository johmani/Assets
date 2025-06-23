#include "HydraEngine/Base.h"

import Assets;
import HE;
import std;
import simdjson;
import magic_enum;


namespace Assets {

    AssetType AssetImporter::GetAssetTypeFromFileExtension(const std::filesystem::path& extension)
    {
        std::string extensionStr = extension.string();

        std::transform(extensionStr.begin(), extensionStr.end(), extensionStr.begin(), [](uint8_t c) { return (char)std::tolower(c); });

             if (extension == ".scene")           return AssetType::Scene;
        else if (extension == ".prefab")          return AssetType::Prefab;
        else if (extension == ".png")             return AssetType::Texture2D;
        else if (extension == ".jpg")             return AssetType::Texture2D;
        else if (extension == ".hdr")             return AssetType::Texture2D;
        else if (extension == ".exr")             return AssetType::Texture2D;
        else if (extension == ".glb")             return AssetType::MeshSource;
        else if (extension == ".mp3")             return AssetType::AudioSource;
        else if (extension == ".wav")             return AssetType::AudioSource;
        else if (extension == ".material")        return AssetType::Material;
        else if (extension == ".physicsmaterial") return AssetType::PhysicsMaterial;
        else if (extension == ".animation")       return AssetType::AnimationClip;
        else if (extension == ".hlsl")            return AssetType::Shader;
        else if (extension == ".ttf")             return AssetType::Font;

        return AssetType::None;
    }

    void AssetImporter::Init(AssetManager* assetManager)
    {
        importers[(int)AssetType::Texture2D]  = HE::CreateScope<TextureImporter>(assetManager);
        importers[(int)AssetType::Scene]      = HE::CreateScope<SceneImporter>(assetManager);
        importers[(int)AssetType::MeshSource] = HE::CreateScope<MeshSourceImporter>(assetManager);
    }

    Asset* AssetImporter::ImportAsset(const AssetMetadata& metadata, AssetImportingMode mode)
    {
        if (metadata.type != AssetType::None)
        {
            auto& importer = importers[int(metadata.type)];
            HE::Timer t;

            Asset* asset = nullptr;
            switch (mode)
            {
            case AssetImportingMode::Sync:
            {
                asset = importer->Import(metadata);
                break;
            }
            case AssetImportingMode::Async:
            {
                if (importer->IsSupportAsyncLoading())
                    asset = importer->ImportAsync(metadata);
                else
                    asset = importer->Import(metadata);
                break;
            }
            default: HE_ASSERT(false); break;
            }

            if (asset)
            {
                HE_INFO("AssetImporter::ImportAsset [{}][{}][{}ms]", magic_enum::enum_name<AssetType>(metadata.type), metadata.filePath.string(), t.ElapsedMilliseconds());
                return asset;
            }
            else
            {
                return nullptr;
            }
        }

        HE_ERROR("No importer available for asset : {}", metadata.filePath.string());
        return nullptr;
    }

    void AssetImporter::SaveAsset(Asset* asset, const AssetMetadata& metadata)
    {
        if (metadata.type != AssetType::None)
        {
            auto& importer = importers[int(metadata.type)];
            importer->Save(asset, metadata);
            return;
        }

        HE_ERROR("No importer available for asset type: {}", metadata.filePath.string());
    }

    Asset* AssetImporter::CreateNewAsset(const std::filesystem::path& filePath)
    {
        AssetType type = GetAssetTypeFromFileExtension(filePath.extension());
        if (type != AssetType::None)
        {
            auto& importer = importers[int(type)];
            return importer->Create(filePath);
        }

        HE_ERROR("No Creator available for asset type: {}", magic_enum::enum_name<AssetType>(type));
        return nullptr;
    }

    Asset* AssetImporter::CreateNewAsset(AssetType type)
    {
        if (type != AssetType::None)
        {
            auto& importer = importers[int(type)];
            return importer->Create("");
        }

        return nullptr;
        HE_ERROR("No Creator available for asset type: {}", magic_enum::enum_name<AssetType>(type));
    }


    AssetManager::AssetManager(nvrhi::DeviceHandle pDevice, const AssetManagerDesc& pDesc)
        : device(pDevice)
        , desc(pDesc)
    {
        textures.reserve(1000);
        materials.reserve(1000);
        meshSources.reserve(1000);
        scenes.reserve(1000);

        assetImporter.Init(this);
    }

    bool AssetManager::IsAssetHandleValid(AssetHandle handle) const
    {
        return handle != 0 && assetRegistry.contains(handle);
    }

    bool AssetManager::IsAssetLoaded(AssetHandle handle) const
    {
        return loadedAssets.contains(handle);
    }

    AssetType AssetManager::GetAssetType(AssetHandle handle) const
    {
        if (IsAssetHandleValid(handle))
            return assetRegistry.at(handle).type;

        return AssetType::None;
    }

    bool AssetManager::IsAssetFilePathValid(const std::filesystem::path& filePath)
    {
        return pathToHandleMap.contains(filePath);
    }

    AssetHandle AssetManager::GetAssetHandleFromFilePath(const std::filesystem::path& filePath)
    {
        if (IsAssetFilePathValid(filePath))
            return pathToHandleMap.at(filePath);

        return 0;
    }

    AssetHandle AssetManager::ImportAsset(const std::filesystem::path& filepath, bool loadToMemeory)
    {
        if (IsAssetFilePathValid(filepath))
            return pathToHandleMap.at(filepath);

        AssetHandle handle;
        AssetMetadata metadata;

        metadata.filePath = filepath;
        metadata.type = assetImporter.GetAssetTypeFromFileExtension(filepath.extension());

        if (metadata.type == AssetType::None)
        {
            HE_ERROR("[AssetManager] : {} is not supported asset", filepath.string());
            return 0;
        }

        if (loadToMemeory)
        {
            Asset* asset = assetImporter.ImportAsset(metadata, desc.importMode);

            if (asset)
            {
                HE_INFO("[AssetManager] : import Asset from {}, loadToMemeory = {} ", metadata.filePath.string(), loadToMemeory);

                asset->handle = handle;
                
                {
                    std::scoped_lock<std::mutex> lock(metaMutex);

                    loadedAssets[handle] = asset;
                    assetRegistry[handle] = metadata;
                    pathToHandleMap[filepath] = handle;
                }

                SerializeAssetRegistry();

                return handle;
            }
            else
            {
                HE_ERROR("[AssetManager] : ImportAsset - asset import failed!");
            }
        }
        else
        {
            {
                std::scoped_lock<std::mutex> lock(metaMutex);

                assetRegistry[handle] = metadata;
                pathToHandleMap[filepath] = handle;
            }
           
            SerializeAssetRegistry();

            HE_INFO("import Asset from {}, loadToMemeory = {} ", metadata.filePath.string(), loadToMemeory);

            return handle;
        }

        return 0;
    }

    Asset* AssetManager::CreateNewAsset(const std::filesystem::path& filePath)
    {
        HE_PROFILE_FUNCTION();

        AssetMetadata metadata;
        Asset* asset = assetImporter.CreateNewAsset(filePath);

        metadata.filePath = filePath;
        metadata.type = assetImporter.GetAssetTypeFromFileExtension(filePath.extension());
        HE_ASSERT(metadata.type != AssetType::None);

        if (asset)
        {
            {
                std::scoped_lock<std::mutex> lock(metaMutex);

                loadedAssets[asset->handle] = asset;
                assetRegistry[asset->handle] = metadata;
                pathToHandleMap[filePath] = asset->handle;
            }

            SerializeAssetRegistry();

            for (auto& [id, subscriber] : subscribers)
                subscriber->OnAssetCreated(asset);

            return asset;
        }

        return nullptr;
    }

    void AssetManager::AddMemoryOnlyAsset(Asset* asset)
    {
        HE_PROFILE_FUNCTION();

        AssetMetadata metadata;

        metadata.filePath = "";
        metadata.type = asset->GetType();

        HE_ASSERT(metadata.type != AssetType::None);

        if (asset)
        {
            {
                std::scoped_lock<std::mutex> lock(metaMutex);

                loadedAssets[asset->handle] = asset;
                assetRegistry[asset->handle] = metadata;
            }

            for (auto& [id, subscriber] : subscribers)
                subscriber->OnAssetCreated(asset);
        }
    }

    void AssetManager::SaveAsset(AssetHandle handle)
    {
        HE_PROFILE_FUNCTION();

        Asset* asset = GetAsset(handle);
        const AssetMetadata& meta = GetMetadata(handle);

        assetImporter.SaveAsset(asset, meta);

        for (auto& [id, subscriber] : subscribers)
            subscriber->OnAssetSaved(asset);
    }

    SubscriberHandle AssetManager::Subscribe(AssetEventCallback* assetEventCallback)
    {
        HE_PROFILE_FUNCTION();

        SubscriberHandle handle;
        subscribers[handle] = assetEventCallback;
        return handle;
    }

    void AssetManager::UnSubscribe(SubscriberHandle handle)
    {
        HE_PROFILE_FUNCTION();

        if (subscribers.contains(handle))
        {
            subscribers.erase(handle);
            HE_TRACE("[UnSubscribe] : {} ,number of subscribers : {}", (uint64_t)handle, subscribers.size());
            return;
        }

        HE_ERROR("[AssetManager] : Invalid Subscriber handle");
    }

    AssetHandle AssetManager::GetOrMakeAsset(const std::filesystem::path& filePath, const std::filesystem::path& newAssetPath, bool overwriteExisting)
    {
        HE_PROFILE_FUNCTION();

        std::filesystem::path absolute = desc.assetsDirectory / newAssetPath;

        if (std::filesystem::exists(absolute) && !overwriteExisting)
        {
            AssetHandle handle = GetAssetHandleFromFilePath(newAssetPath);
            if (!handle)
            {
                handle = ImportAsset(newAssetPath, false);
            }

            return handle;
        }
        else
        {
            if (!std::filesystem::exists(absolute.parent_path()))
                std::filesystem::create_directories(absolute.parent_path());

            std::filesystem::copy_options options;

            if (overwriteExisting)
                options |= std::filesystem::copy_options::overwrite_existing;

            HE::FileSystem::Copy(filePath, absolute);
            AssetHandle handle = ImportAsset(newAssetPath, false);

            return handle;
        }

        return 0;
    }

    void AssetManager::ReloadAsset(AssetHandle handle)
    {
        HE_PROFILE_FUNCTION();

        if (!assetRegistry.contains(handle))
        {
            HE_ERROR("[AssetManager] : ReloadAsset {} : invalid asset handle", (uint64_t)handle);
            return;
        }

        if (loadedAssets.contains(handle))
            UnloadAsset(handle);

        const AssetMetadata& metadata = GetMetadata(handle);
        Asset* asset = assetImporter.ImportAsset(metadata, desc.importMode);

        if (!asset)
        {
            HE_ERROR("[AssetManager] :  asset reload failed!");
            return;
        }

        {
            std::scoped_lock<std::mutex> lock(assetsMutex);
            loadedAssets[handle] = asset;
        }

        for (auto& [id, subscriber] : subscribers)
            subscriber->OnAssetReloaded(asset);
    }

    void AssetManager::RemoveAsset(AssetHandle handle)
    {
        HE_PROFILE_FUNCTION();

        if (!assetRegistry.contains(handle))
        {
            HE_ERROR("[AssetManager] : RemoveAsset {} : invalid asset handle", (uint64_t)handle);
            return;
        }

        for (auto& [id, subscriber] : subscribers)
            subscriber->OnAssetRemoved(handle);

        {
            std::scoped_lock<std::mutex> lock(metaMutex);
           
            auto& path = GetMetadata(handle).filePath;
            if (pathToHandleMap.contains(path))
                pathToHandleMap.erase(path);

            if (loadedAssets.contains(handle))
                loadedAssets.erase(handle);

            assetRegistry.erase(handle);
        }

        SerializeAssetRegistry();
    }

    void AssetManager::ChangeAssetPath(AssetHandle handle, const std::filesystem::path& newPath)
    {
        if (assetRegistry.contains(handle))
        {
            {
                std::scoped_lock<std::mutex> lock(metaMutex);

                pathToHandleMap.erase(assetRegistry.at(handle).filePath);
                pathToHandleMap[newPath] = handle;
            }

            assetRegistry.at(handle).filePath = newPath;

            SerializeAssetRegistry();
        }
    }

    const AssetMetadata& AssetManager::GetMetadata(AssetHandle handle) const
    {
        if (assetRegistry.contains(handle))
            return assetRegistry.at(handle);

        static AssetMetadata s_NullMetadata;
        return s_NullMetadata;
    }

    const std::filesystem::path& AssetManager::GetFilePath(AssetHandle handle) const
    {
        return GetMetadata(handle).filePath;
    }

    std::filesystem::path AssetManager::GetAssetFileSystemPath(AssetHandle handle) const
    {
        return desc.assetsDirectory / GetMetadata(handle).filePath;
    }

    Asset* AssetManager::GetAsset(AssetHandle handle)
    {
        if (!IsAssetHandleValid(handle))
            return nullptr;

        Asset* asset = nullptr;
        if (IsAssetLoaded(handle))
        {
            asset = loadedAssets.at(handle);
        }
        else
        {
            const AssetMetadata& metadata = GetMetadata(handle);
            asset = assetImporter.ImportAsset(metadata, desc.importMode);
            if (asset)
            {
                {
                    std::scoped_lock<std::mutex> lock(assetsMutex);
                    loadedAssets[handle] = asset;
                }
                
                asset->handle = handle;
            }
        }

        return asset;
    }

    void AssetManager::UnloadAsset(AssetHandle handle)
    {
        HE_PROFILE_FUNCTION();

        if (!loadedAssets.contains(handle))
        {
            HE_ERROR("[AssetManager] : UnloadAsset {} : Asset not loaded", (uint64_t)handle);
            return;
        }

        auto asset = loadedAssets.at(handle);
        AssetType type = asset->GetType();

        HE_TRACE("Unload {}", magic_enum::enum_name<AssetType>(type));

        // remove memeory only asset
        if (GetMetadata(handle).filePath.empty())
            assetRegistry.erase(handle);

        for (auto& [id, subscriber] : subscribers)
            subscriber->OnAssetUnloaded(asset);

        for (int i = 0; auto handle : asset->GetDependencies())
        {
            UnloadAsset(handle);
        }

        switch (type)
        {
        case AssetType::Texture2D:
        {
            for (uint32_t i = 0; i < textures.size(); i++)
                if (textures[i].handle == handle)
                    textures.erase(textures.begin() + i);
            break;
        }
        case AssetType::Material:
        {
            for (uint32_t i = 0; i < materials.size(); i++)
                if (materials[i].handle == handle)
                    materials.erase(materials.begin() + i);
            break;
        }
        case AssetType::MeshSource:
        {
            for (uint32_t i = 0; i < meshSources.size(); i++)
                if (meshSources[i].handle == handle)
                    meshSources.erase(meshSources.begin() + i);
            break;
        }
        case AssetType::Scene:
        {
            for (uint32_t i = 0; i < scenes.size(); i++)
                if (scenes[i].handle == handle)
                    scenes.erase(scenes.begin() + i);
            break;
        }
        default: HE_ASSERT(false);
        }

        loadedAssets.erase(handle);
    }

    void AssetManager::UnloadAllAssets()
    {
        HE_PROFILE_FUNCTION();

#if 0
        scenes.clear();
        meshSources.clear();
        materials.clear();
        textures.clear();
        loadedAssets.clear();

        for (const auto& [handle, meta] : assetRegistry)
            if (GetMetadata(handle).filePath.empty())
                assetRegistry.erase(handle);
#else
        for (int i = (int)scenes.size() - 1; i >= 0; i--)
            UnloadAsset(scenes[i].handle);

        for (int i = (int)meshSources.size() - 1; i >= 0; i--)
            UnloadAsset(meshSources[i].handle);

        for (int i = (int)textures.size() - 1; i >= 0; i--)
            UnloadAsset(textures[i].handle);

        for (int i = (int)materials.size() - 1; i >= 0; i--)
            UnloadAsset(materials[i].handle);

        HE_VERIFY(textures.size() == 0);
        HE_VERIFY(meshSources.size() == 0);
        HE_VERIFY(materials.size() == 0);
        HE_VERIFY(scenes.size() == 0);
        HE_VERIFY(loadedAssets.size() == 0);
#endif
    }

    void AssetManager::OnAssetLoaded(Asset* asset)
    {
        HE_PROFILE_FUNCTION();

        for (auto& [id, subscriber] : subscribers)
            subscriber->OnAssetLoaded(asset);
    }

    void AssetManager::SerializeAssetRegistry()
    {
        HE_PROFILE_FUNCTION();

        std::ofstream file(desc.assetsRegistryFilePath);
        if (!file.is_open())
        {
            HE_ERROR("[AssetManager] : Unable to open file for writing, {}", desc.assetsRegistryFilePath.string());
            return;
        }

        std::ostringstream oss;
        oss << "{\n";

        {
            bool firstItem = false;
            oss << "\t\"assetRegistry\" : [\n";
            for (const auto& [handle, metadata] : assetRegistry)
            {
                if (metadata.filePath.empty() || std::filesystem::exists(metadata.filePath))
                    continue;

                if (firstItem) oss << ",\n";
                firstItem = true;

                std::string filepathStr = metadata.filePath.generic_string();

                oss << "\t\t{\n";
                oss << "\t\t\t\"handle\" : " << handle << ",\n";
                oss << "\t\t\t\"filePath\" : \"" << filepathStr << "\",\n";
                oss << "\t\t\t\"type\" : \"" << magic_enum::enum_name<AssetType>(metadata.type) << "\"\n";
                oss << "\t\t}";
            }
            oss << "\n\t]\n";
        }

        oss << "}\n";

        file << oss.str();
        file.close();
    }

    bool AssetManager::DeserializeAssetRegistry()
    {
        HE_PROFILE_FUNCTION();

        static simdjson::dom::parser parser;
        auto doc = parser.load(desc.assetsRegistryFilePath.string());

        auto ar = doc["assetRegistry"].get_array();
        if (!ar.error())
        {
            for (auto metaData : ar)
            {
                auto handle = metaData["handle"].get_uint64().value();
                auto& metadata = assetRegistry[handle];
                metadata.filePath = std::filesystem::path(metaData["filePath"].get_c_str().value()).lexically_normal();

                auto type = magic_enum::enum_cast<AssetType>(metaData["type"].get_c_str().value());
                if (type.has_value()) metadata.type = type.value();
                pathToHandleMap[metadata.filePath] = handle;
            }
        }

        return true;
    }

    void AssetManager::ReserveRange(AssetType type, size_t size)
    {
        std::scoped_lock<std::mutex> lock(assetsMutex);
        switch (type)
        {
        case AssetType::Scene:            scenes.resize(size);        break;
        case AssetType::MeshSource:       meshSources.resize(size);   break;
        case AssetType::Material:         materials.resize(size);     break;
        case AssetType::Texture2D:        textures.resize(size);      break;
        case AssetType::Prefab:                                       break;
        case AssetType::PhysicsMaterial:                              break;
        case AssetType::AudioSource:                                  break;
        case AssetType::AnimationClip:                                break;
        case AssetType::Shader:                                       break;
        case AssetType::Font:                                         break;
        }
    }

    Asset* AssetManager::EmplaceBack(AssetType type)
    {
        std::scoped_lock<std::mutex> lock(assetsMutex);
        switch (type)
        {
        case AssetType::Scene:            return &scenes.emplace_back();        
        case AssetType::MeshSource:       return &meshSources.emplace_back();
        case AssetType::Material:         return &materials.emplace_back();
        case AssetType::Texture2D:        return &textures.emplace_back();
        case AssetType::Prefab:           return nullptr;
        case AssetType::PhysicsMaterial:  return nullptr;
        case AssetType::AudioSource:      return nullptr;
        case AssetType::AnimationClip:    return nullptr;
        case AssetType::Shader:           return nullptr;
        case AssetType::Font:             return nullptr;
        }

        return nullptr;
    }
}