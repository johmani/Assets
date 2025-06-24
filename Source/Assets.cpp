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

    Asset AssetImporter::ImportAsset(const std::filesystem::path& path, AssetImportingMode mode)
    {
        auto type = GetAssetTypeFromFileExtension(path.extension());

        if (type != AssetType::None)
        {
            auto& importer = importers[int(type)];
            HE::Timer t;

            Asset asset = {};
            switch (mode)
            {
            case AssetImportingMode::Sync:
            {
                asset = importer->Import(path);
                break;
            }
            case AssetImportingMode::Async:
            {
                if (importer->IsSupportAsyncLoading())
                    asset = importer->ImportAsync(path);
                else
                    asset = importer->Import(path);
                break;
            }
            default: HE_ASSERT(false); break;
            }

            if (asset)
            {
                HE_INFO("AssetImporter::ImportAsset [{}][{}][{}ms]", magic_enum::enum_name<AssetType>(type), path.string(), t.ElapsedMilliseconds());
                return asset;
            }
            else
            {
                return {};
            }
        }

        HE_ERROR("No importer available for asset : {}", path.string());
        return {};
    }

    void AssetImporter::SaveAsset(Asset asset, const std::filesystem::path& path)
    {
        auto type = GetAssetTypeFromFileExtension(path.extension());

        if (type != AssetType::None)
        {
            auto& importer = importers[int(type)];
            importer->Save(asset, path);
            return;
        }

        HE_ERROR("No importer available for asset type: {}", path.string());
    }

    Asset AssetImporter::CreateNewAsset(const std::filesystem::path& filePath)
    {
        auto type = GetAssetTypeFromFileExtension(filePath.extension());
        if (type != AssetType::None)
        {
            auto& importer = importers[int(type)];
            return importer->Create(filePath);
        }

        HE_ERROR("No Creator available for asset type: {}", magic_enum::enum_name<AssetType>(type));
        return {};
    }

    Asset AssetImporter::CreateNewAsset(AssetType type)
    {
        if (type != AssetType::None)
        {
            auto& importer = importers[int(type)];
            return importer->Create("");
        }

        return {};
        HE_ERROR("No Creator available for asset type: {}", magic_enum::enum_name<AssetType>(type));
    }


    AssetManager::AssetManager(nvrhi::DeviceHandle pDevice, const AssetManagerDesc& pDesc)
        : device(pDevice)
        , desc(pDesc)
    {
        assetImporter.Init(this);
    }

    AssetType AssetManager::GetAssetType(AssetHandle handle) const
    {
        if (IsAssetHandleValid(handle))
            return metaMap.at(handle).type;

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

    AssetHandle AssetManager::ImportAsset(const std::filesystem::path& filePath, bool loadToMemeory)
    {
        if (IsAssetFilePathValid(filePath))
            return pathToHandleMap.at(filePath);

        AssetHandle handle;
        AssetMetadata metadata;

        metadata.filePath = filePath;
        metadata.type = assetImporter.GetAssetTypeFromFileExtension(filePath.extension());

        if (metadata.type == AssetType::None)
        {
            HE_ERROR("[AssetManager] : {} is not supported asset", filePath.string());
            return 0;
        }

        if (loadToMemeory)
        {
            Asset asset = assetImporter.ImportAsset(filePath, desc.importMode);

            if (asset)
            {
                HE_INFO("[AssetManager] : import Asset from {}, loadToMemeory = {} ", filePath.string(), loadToMemeory);

                asset.Get<AssetID>().id = handle;
                
                {
                    std::scoped_lock<std::mutex> lock(metaMutex);

                    assetMap[handle] = asset;
                    metaMap[handle] = metadata;
                    pathToHandleMap[filePath] = handle;
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

                metaMap[handle] = metadata;
                pathToHandleMap[filePath] = handle;
            }
           
            SerializeAssetRegistry();

            HE_INFO("import Asset from {}, loadToMemeory = {} ", metadata.filePath.string(), loadToMemeory);

            return handle;
        }

        return 0;
    }

    Asset AssetManager::CreateNewAsset(const std::filesystem::path& filePath)
    {
        HE_PROFILE_FUNCTION();

        AssetMetadata metadata;
        Asset asset = assetImporter.CreateNewAsset(filePath);

        metadata.filePath = filePath;
        metadata.type = assetImporter.GetAssetTypeFromFileExtension(filePath.extension());
        HE_ASSERT(metadata.type != AssetType::None);

        if (asset)
        {
            {
                std::scoped_lock<std::mutex> lock(metaMutex);

                auto assetID = asset.Get<AssetID>().id;

                assetMap[assetID] = asset;
                metaMap[assetID] = metadata;
                pathToHandleMap[filePath] = assetID;
            }

            SerializeAssetRegistry();

            for (auto& [id, subscriber] : subscribers)
                subscriber->OnAssetCreated(asset);

            return asset;
        }

        return {};
    }

    void AssetManager::AddMemoryOnlyAsset(Asset asset, AssetType type)
    {
        HE_PROFILE_FUNCTION();

        AssetMetadata metadata;

        metadata.filePath = "";
        metadata.type = type;

        HE_ASSERT(metadata.type != AssetType::None);

        if (asset)
        {
            {
                std::scoped_lock<std::mutex> lock(metaMutex);

                auto assetID = asset.Get<AssetID>().id;

                assetMap[assetID] = asset;
                metaMap[assetID] = metadata;
            }

            for (auto& [id, subscriber] : subscribers)
                subscriber->OnAssetCreated(asset);
        }
    }

    void AssetManager::SaveAsset(AssetHandle handle)
    {
        HE_PROFILE_FUNCTION();

        Asset asset = GetAsset(handle);
        const AssetMetadata& meta = GetMetadata(handle);

        assetImporter.SaveAsset(asset, meta.filePath);

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

        if (!metaMap.contains(handle))
        {
            HE_ERROR("[AssetManager] : ReloadAsset {} : invalid asset handle", (uint64_t)handle);
            return;
        }

        if (assetMap.contains(handle))
            UnloadAsset(handle);

        const AssetMetadata& metadata = GetMetadata(handle);
        Asset asset = assetImporter.ImportAsset(metadata.filePath, desc.importMode);

        if (!asset)
        {
            HE_ERROR("[AssetManager] :  asset reload failed!");
            return;
        }

        {
            std::scoped_lock<std::mutex> lock(registryMutex);
            assetMap[handle] = asset;
        }

        for (auto& [id, subscriber] : subscribers)
            subscriber->OnAssetReloaded(asset);
    }

    void AssetManager::RemoveAsset(AssetHandle handle)
    {
        HE_PROFILE_FUNCTION();

        if (!metaMap.contains(handle))
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

            if (assetMap.contains(handle))
                assetMap.erase(handle);

            metaMap.erase(handle);
        }

        SerializeAssetRegistry();
    }

    void AssetManager::ChangeAssetPath(AssetHandle handle, const std::filesystem::path& newPath)
    {
        if (metaMap.contains(handle))
        {
            {
                std::scoped_lock<std::mutex> lock(metaMutex);

                pathToHandleMap.erase(metaMap.at(handle).filePath);
                pathToHandleMap[newPath] = handle;
            }

            metaMap.at(handle).filePath = newPath;

            SerializeAssetRegistry();
        }
    }

    const AssetMetadata& AssetManager::GetMetadata(AssetHandle handle) const
    {
        if (metaMap.contains(handle))
            return metaMap.at(handle);

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

    Asset AssetManager::GetAsset(AssetHandle handle)
    {
        if (!IsAssetHandleValid(handle))
            return {};

        Asset asset = {};
        if (IsAssetLoaded(handle))
        {
            asset = assetMap.at(handle);
        }
        else
        {
            const AssetMetadata& metadata = GetMetadata(handle);
            asset = assetImporter.ImportAsset(metadata.filePath, desc.importMode);
            if (asset)
            {
                {
                    std::scoped_lock<std::mutex> lock(registryMutex);
                    assetMap[handle] = asset;
                }
                
                asset.Get<AssetID>().id = handle;
            }
        }

        return asset;
    }

    void AssetManager::UnloadAsset(AssetHandle handle)
    {
        HE_PROFILE_FUNCTION();

        auto asset = FindAsset(handle);
        if (!asset)
        {
            HE_ERROR("[AssetManager] : UnloadAsset {} : Asset not loaded", (uint64_t)handle);
            return;
        }

        HE_TRACE("Unload {}", magic_enum::enum_name<AssetType>(asset.GetType()));

        for (auto& [id, subscriber] : subscribers)
            subscriber->OnAssetUnloaded(asset);

        if (asset.Has<MeshSource>())
        {
            auto& ms = asset.Get<MeshSource>();

            for (int i = 0; auto handle : ms.GetDependencies())
                UnloadAsset(handle);
        }

        // remove memeory only asset
        if (GetMetadata(handle).filePath.empty())
            metaMap.erase(handle);

        DestroyAsset(asset);
    }

    void AssetManager::UnloadAllAssets()
    {
        HE_PROFILE_FUNCTION();

        auto view = registry.view<AssetID>();
        for (auto a : view)
        {
            Asset asset = { a , this };
            UnloadAsset(asset.GetHandle());
        }
    }

    void AssetManager::OnAssetLoaded(Asset asset)
    {
        HE_PROFILE_FUNCTION();

        for (auto& [id, subscriber] : subscribers)
            subscriber->OnAssetLoaded(asset);
    }

    Asset AssetManager::CreateAsset(AssetType type)
    {
        std::scoped_lock<std::mutex> lock(registryMutex);

        Asset asset = { registry.create(), this };

        auto& ic = asset.Add<AssetID>();
        auto& as = asset.Add<AssetState>();

        assetMap[ic.id] = asset;

        return asset;
    }

    void AssetManager::DestroyAsset(Asset asset)
    {
        if (!asset)
            return;

        std::scoped_lock<std::mutex> lock(registryMutex);

        assetMap.erase(asset.GetHandle());
        registry.destroy(asset);
    }

    Asset AssetManager::FindAsset(AssetHandle handle)
    {
        if (assetMap.contains(handle))
            return { assetMap.at(handle), this };

        return {};
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
            oss << "\t\"metaMap\" : [\n";
            for (const auto& [handle, metadata] : metaMap)
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

        auto ar = doc["metaMap"].get_array();
        if (!ar.error())
        {
            for (auto metaData : ar)
            {
                auto handle = metaData["handle"].get_uint64().value();
                auto& metadata = metaMap[handle];
                metadata.filePath = std::filesystem::path(metaData["filePath"].get_c_str().value()).lexically_normal();

                auto type = magic_enum::enum_cast<AssetType>(metaData["type"].get_c_str().value());
                if (type.has_value()) metadata.type = type.value();
                pathToHandleMap[metadata.filePath] = handle;
            }
        }

        return true;
    }
}