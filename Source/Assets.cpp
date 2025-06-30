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

    Asset AssetImporter::ImportAsset(AssetHandle handle, const std::filesystem::path& filePath, AssetImportingMode mode)
    {
        auto type = GetAssetTypeFromFileExtension(filePath.extension());

        if (type != AssetType::None)
        {
            auto& importer = importers[int(type)];
            HE::Timer t;

            Asset asset = {};
            switch (mode)
            {
            case AssetImportingMode::Sync:
            {
                asset = importer->Import(handle, filePath);
                break;
            }
            case AssetImportingMode::Async:
            {
                if (importer->IsSupportAsyncLoading())
                    asset = importer->ImportAsync(handle, filePath);
                else
                    asset = importer->Import(handle, filePath);
                break;
            }
            default: HE_ASSERT(false); break;
            }

            if (asset)
            {
                HE_INFO("AssetImporter::ImportAsset [{}][{}][{}ms]", magic_enum::enum_name<AssetType>(type), filePath.string(), t.ElapsedMilliseconds());
                return asset;
            }
            else
            {
                return {};
            }
        }

        HE_ERROR("No importer available for asset : {}", filePath.string());
        return {};
    }

    void AssetImporter::SaveAsset(Asset asset, const std::filesystem::path& filePath)
    {
        auto type = GetAssetTypeFromFileExtension(filePath.extension());

        if (type != AssetType::None)
        {
            auto& importer = importers[int(type)];
            importer->Save(asset, filePath);
            return;
        }

        HE_ERROR("No importer available for asset type: {}", filePath.string());
    }

    Asset AssetImporter::CreateAsset(AssetHandle handle, const std::filesystem::path& filePath)
    {
        auto type = GetAssetTypeFromFileExtension(filePath.extension());
        if (type != AssetType::None)
        {
            auto& importer = importers[int(type)];
            return importer->Create(handle, filePath);
        }

        HE_ERROR("No Creator available for asset type: {}", magic_enum::enum_name<AssetType>(type));
        return {};
    }

    AssetManager::AssetManager(nvrhi::DeviceHandle pDevice, const AssetManagerDesc& pDesc)
        : device(pDevice)
        , desc(pDesc)
    {
        assetImporter.Init(this);
    }

    void AssetManager::Init(nvrhi::DeviceHandle pDevice, const AssetManagerDesc& pDesc)
    {
      device = pDevice;
      desc = pDesc;
      assetImporter.Init(this);
    }

    Asset AssetManager::GetAsset(AssetHandle handle)
    {
        if (!IsAssetHandleValid(handle))
            return {};

        Asset asset = FindAsset(handle);
        if (!asset)
        {
            const AssetMetadata& metadata = GetMetadata(handle);
            asset = assetImporter.ImportAsset(handle, metadata.filePath, desc.importMode);
        }

        return asset;
    }

    Asset AssetManager::FindAsset(AssetHandle handle)
    {
        if (assetMap.contains(handle))
            return { assetMap.at(handle), this };

        return {};
    }

    Asset AssetManager::CreateAsset(AssetHandle handle)
    {
        Asset asset;

        std::scoped_lock<std::mutex> lock(registryMutex);
        asset = { registry.create(), this };

        asset.Add<AssetHandle>(handle);
        asset.Add<AssetState>(AssetState::None);
        asset.Add<AssetFlags>(AssetFlags::None);

        assetMap[handle] = asset;

        return asset;
    }

    Asset AssetManager::CreateAsset(const std::filesystem::path& filePath)
    {
        HE_PROFILE_FUNCTION();

        AssetHandle handle;
        Asset asset = assetImporter.CreateAsset(handle, filePath);

        if (asset)
        {
            AssetMetadata metadata;
            metadata.filePath = filePath;
            metadata.type = assetImporter.GetAssetTypeFromFileExtension(filePath.extension());
            HE_VERIFY(metadata.type != AssetType::None);

            RegisterMetadata(handle, metadata);
            Serialize();

            for (auto& [id, subscriber] : subscribers)
                subscriber->OnAssetCreated(asset);

            return asset;
        }

        return {};
    }

    AssetHandle AssetManager::GetOrMakeAsset(const std::filesystem::path& filePath, const std::filesystem::path& newAssetPath, bool overwriteExisting)
    {
        HE_PROFILE_FUNCTION();

        std::filesystem::path absolute = desc.assetsDirectory / newAssetPath;

        if (std::filesystem::exists(absolute) && !overwriteExisting)
        {
            return ImportAsset(newAssetPath, false);
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

    void AssetManager::MarkAsMemoryOnlyAsset(Asset asset, AssetType type)
    {
        HE_PROFILE_FUNCTION();

        if (!asset || type == AssetType::None)
            return;

        auto handle = asset.GetHandle();
        auto& flags = asset.Get<AssetFlags>();
        flags |= AssetFlags::IsMemoryOnly;

        AssetMetadata metadata;
        metadata.filePath = "";
        metadata.type = type;
        RegisterMetadata(handle, metadata); // NOTE : This marks the asset as valid
    }

    void AssetManager::DestroyAsset(AssetHandle handle)
    {
        Asset asset = FindAsset(handle);
        DestroyAsset(asset);
    }

    void AssetManager::DestroyAsset(Asset asset)
    {
        if (!asset)
            return;

        std::scoped_lock<std::mutex> lock(registryMutex);
        assetMap.erase(asset.GetHandle());
        registry.destroy(asset);
    }

    void AssetManager::SaveAsset(AssetHandle handle)
    {
        HE_PROFILE_FUNCTION();

        if (!IsAssetHandleValid(handle))
            return;

        Asset asset = GetAsset(handle);
        const AssetMetadata& meta = GetMetadata(handle);
        assetImporter.SaveAsset(asset, meta.filePath);

        for (auto& [id, subscriber] : subscribers)
            subscriber->OnAssetSaved(asset);
    }

    void AssetManager::ReloadAsset(AssetHandle handle)
    {
        HE_PROFILE_FUNCTION();

        if (!IsAssetHandleValid(handle))
        {
            HE_ERROR("AssetManager::ReloadAsset {} : invalid asset handle", (uint64_t)handle);
            return;
        }

        if (IsAssetLoaded(handle))
            UnloadAsset(handle);

        const auto& metadata = GetMetadata(handle);
        Asset asset = assetImporter.ImportAsset(handle, metadata.filePath, desc.importMode);

        if (!asset)
        {
            HE_ERROR("AssetManager::ReloadAsset : asset reload failed!");
            return;
        }

        for (auto& [id, subscriber] : subscribers)
            subscriber->OnAssetReloaded(asset);
    }

    void AssetManager::UnloadAsset(AssetHandle handle)
    {
        HE_PROFILE_FUNCTION();

        auto asset = FindAsset(handle);
        if (!asset)
        {
            HE_ERROR("[AssetManager] : UnloadAsset {} : Asset not loaded", magic_enum::enum_name<AssetType>(GetAssetType(handle)), (uint64_t)handle);
            return;
        }

        HE_TRACE("Unload {}", magic_enum::enum_name<AssetType>(GetAssetType(handle)));

        for (auto& [id, subscriber] : subscribers)
            subscriber->OnAssetUnloaded(asset);

        if (HE::HasFlags(asset.Get<AssetFlags>(), AssetFlags::IsMemoryOnly))
            UnRegisterMetadata(handle);

        if (asset.Has<AssetDependencies>())
        {
            for (auto handle : asset.Get<AssetDependencies>().dependencies)
                UnloadAsset(handle);
        }

        DestroyAsset(asset);
    }

    void AssetManager::UnloadAllAssets()
    {
        HE_PROFILE_FUNCTION();

        {
            for (auto a : registry.view<Scene>())
                UnloadAsset(Asset(a, this).GetHandle());
        }

        {
            for (auto a : registry.view<MeshSource>())
                UnloadAsset(Asset(a, this).GetHandle());
        }

        {
            for (auto a : registry.view<Material>())
                UnloadAsset(Asset(a, this).GetHandle());
        }

        {
            for (auto a : registry.view<Texture>())
                UnloadAsset(Asset(a, this).GetHandle());
        }
    }

    void AssetManager::RemoveAsset(AssetHandle handle)
    {
        HE_PROFILE_FUNCTION();

        if (!IsAssetHandleValid(handle))
        {
            HE_ERROR("[AssetManager] : RemoveAsset {} : invalid asset handle", (uint64_t)handle);
            return;
        }

        for (auto& [id, subscriber] : subscribers)
            subscriber->OnAssetRemoved(handle);

        DestroyAsset(handle);
        UnRegisterMetadata(handle);
        Serialize();
    }

    AssetHandle AssetManager::ImportAsset(const std::filesystem::path& filePath, bool loadToMemeory)
    {
        if (IsAssetFilePathValid(filePath))
            return pathToHandleMap.at(filePath);

        auto type = assetImporter.GetAssetTypeFromFileExtension(filePath.extension());

        if (type == AssetType::None)
        {
            HE_ERROR("AssetManager::ImportAsset {} is not supported asset", filePath.string());
            return 0;
        }

        AssetHandle handle;

        if (loadToMemeory)
        {
            Asset asset = assetImporter.ImportAsset(handle, filePath, desc.importMode);

            if (!asset)
            {
                HE_ERROR("AssetManager::ImportAsset : Failed {}", filePath.string());
                return 0;
            }
        }

        AssetMetadata metadata;
        metadata.filePath = filePath;
        metadata.type = type;
        RegisterMetadata(handle, metadata);

        Serialize();

        HE_INFO("import Asset from {}, loadToMemeory = {} ", metadata.filePath.string(), loadToMemeory);

        return handle;
    }

    bool AssetManager::RegisterMetadata(AssetHandle handle, const AssetMetadata& meta)
    {
        if (metaMap.contains(handle))
        {
            HE_ERROR("AssetManager::RegisterAssetMetaData : asset {} : {}, already exists", (uint64_t)handle, meta.filePath.string());
            return false;
        }

        std::scoped_lock<std::mutex> lock(metaMutex);

        metaMap[handle] = meta;
        pathToHandleMap[meta.filePath] = handle;

        return true;
    }

    void AssetManager::UnRegisterMetadata(AssetHandle handle)
    {
        std::scoped_lock<std::mutex> lock(metaMutex);

        if (metaMap.contains(handle))
            metaMap.erase(handle);

        auto& path = GetMetadata(handle).filePath;
        if (pathToHandleMap.contains(path))
            pathToHandleMap.erase(path);
    }

    bool AssetManager::UpdateMetadate(AssetHandle handle, const AssetMetadata& metadata)
    {
        if (IsAssetHandleValid(handle))
        {
            HE_ERROR("AssetManager::UpdateMetadate : invalid AssetHandle {} ", (uint64_t)handle);
            return false;
        }

        UnRegisterMetadata(handle);
        RegisterMetadata(handle, metadata);

        Serialize();

        return true;
    }

    const AssetMetadata& AssetManager::GetMetadata(AssetHandle handle) const
    {
        if (metaMap.contains(handle))
            return metaMap.at(handle);

        static AssetMetadata s_NullMetadata;
        return s_NullMetadata;
    }

    AssetType AssetManager::GetAssetType(AssetHandle handle) const
    {
        if (IsAssetHandleValid(handle))
            return metaMap.at(handle).type;

        return AssetType::None;
    }

    const std::filesystem::path& AssetManager::GetFilePath(AssetHandle handle) const
    {
        return GetMetadata(handle).filePath;
    }

    AssetHandle AssetManager::GetAssetHandleFromFilePath(const std::filesystem::path& filePath)
    {
        if (IsAssetFilePathValid(filePath))
            return pathToHandleMap.at(filePath);

        return 0;
    }

    std::filesystem::path AssetManager::GetAssetFileSystemPath(AssetHandle handle) const
    {
        return desc.assetsDirectory / GetMetadata(handle).filePath;
    }

    bool AssetManager::IsAssetFilePathValid(const std::filesystem::path& filePath)
    {
        return pathToHandleMap.contains(filePath);
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

    void AssetManager::OnAssetLoaded(Asset asset)
    {
        HE_PROFILE_FUNCTION();

        for (auto& [id, subscriber] : subscribers)
            subscriber->OnAssetLoaded(asset);
    }

    void AssetManager::Serialize()
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

    bool AssetManager::Deserialize()
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