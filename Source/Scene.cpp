#include "HydraEngine/Base.h"

import Assets;
import HE;
import Math;
import std;
import simdjson;
import magic_enum;

namespace Assets {

#pragma region Entity

    Entity Entity::GetParent()
    {
        auto& rc = GetComponent<RelationshipComponent>();

        if (rc.parent)
        {
            return scene->FindEntity(rc.parent);
        }

        return {};
    }

    void Entity::AddChild(Entity entity)
    {
        auto& rc = GetComponent<RelationshipComponent>();

        auto parent = entity.GetParent();
        if (parent)
        {
            parent.RemoveChild(entity);
        }

        rc.children.push_back(entity.GetUUID());
        entity.GetComponent<RelationshipComponent>().parent = GetUUID();
    }

    void Entity::RemoveChild(Entity entity)
    {
        auto id = entity.GetUUID();
        RemoveChild(id);
    }

    void Entity::RemoveChild(UUID id)
    {
        auto& rc = GetComponent<RelationshipComponent>();

        for (size_t i = 0; i < rc.children.size(); i++)
        {
            if (rc.children[i] == id)
            {
                rc.children.erase(i + rc.children.begin());
                break;
            }
        }
    }

    std::vector<UUID>& Entity::GetChildren()
    {
        auto& rc = GetComponent<RelationshipComponent>();
        return  rc.children;
    }

    TransformComponent& Entity::GetTransform()
    {
        return GetComponent<TransformComponent>();
    }

    Math::float4x4 Entity::GetTransformMatrix()
    {
        return GetComponent<TransformComponent>().GetTransform();
    }

    void Entity::SetLocalTransform(const Math::float4x4& localTransform)
    {
        HE_ASSERT(HasComponent<TransformComponent>());

        Math::vec3 position, scale, skew;
        Math::quat quaternion;
        Math::vec4 perspective;

        Math::decompose(localTransform, scale, quaternion, position, skew, perspective);

        auto& t = GetComponent<TransformComponent>();
        t.position = position;
        t.rotation = quaternion;
        t.scale = scale;
    }

    void Entity::SetWorldTransform(const Math::float4x4& worldTransform)
    {
        Math::float4x4 lt = scene->ConvertToLocalSpace({ handle, scene }, worldTransform);
        SetLocalTransform(lt);
    }

    Math::float4x4 Entity::GetWorldSpaceTransformMatrix()
    {
        return scene->ConvertToWorldSpace({ handle, scene });
    }

#pragma endregion

#pragma region Scene

    Entity Scene::GetRootEntity()
    {
        return FindEntity(rootID);
    }

    Entity Scene::CreateEntity(const std::string& name, UUID parent)
    {
        return CreateEntityWithUUID(UUID(), name, parent);
    }

    Entity Scene::CreateEntityWithUUID(UUID id, UUID parent)
    {
        return CreateEntityWithUUID(id, "new Entity", parent);
    }

    Entity Scene::CreateEntityWithUUID(UUID id, const std::string& name, UUID parent)
    {
        Entity entity = Entity{ registry.create(), this };

        auto& ic = entity.AddComponent<IDComponent>(id);
        auto& nc = entity.AddComponent<NameComponent>(name);
        entity.AddComponent<RelationshipComponent>().parent = parent;
        entity.AddComponent<TransformComponent>();

        Entity parentEntity = FindEntity(parent);
        if (parentEntity)
            parentEntity.AddChild(entity);

        nc.name = name.empty() ? "Entity" : name;

        entityMap[id] = entity;

        return entity;
    }

    void Scene::DestroyEntity(Entity entity)
    {
        if (!entity)
            return;

        Entity parent = entity.GetParent();
        if (parent)
            parent.RemoveChild(entity);

        auto children = entity.GetChildren();
        for (auto id : children)
        {
            Entity childEntity = FindEntity(id);
            DestroyEntity(childEntity);
        }

        entityMap.erase(entity.GetUUID());
        registry.destroy(entity);
    }

    void Scene::DestroyEntity(UUID id)
    {
        Entity e = FindEntity(id);
        DestroyEntity(e);
    }

    Entity Scene::FindEntity(std::string_view name)
    {
        auto view = registry.view<NameComponent>();
        for (auto entity : view)
        {
            const auto& nc = view.get<NameComponent>(entity);
            if (nc.name == name)
                return { entity, this };
        }
        return {};
    }

    Entity Scene::FindEntity(UUID uuid)
    {
        if (entityMap.contains(uuid))
            return { entityMap.at(uuid), this };

        return {};
    }

    Math::float4x4 Scene::ConvertToWorldSpace(Entity entity)
    {
        Entity paretn = entity.GetParent();
        if (paretn && paretn.GetComponent<RelationshipComponent>().parent)
            return ConvertToWorldSpace(paretn) * entity.GetTransformMatrix();

        return entity.GetTransformMatrix();
    }

    Math::float4x4 Scene::ConvertToLocalSpace(Entity entity, Math::float4x4 wt)
    {
        return Math::inverse(entity.GetParent().GetWorldSpaceTransformMatrix()) * wt;
    }


    template<typename... Component>
    static void CopyComponent(entt::registry& dst, entt::registry& src, const std::unordered_map<Assets::UUID, entt::entity>& enttMap)
    {
        ([&]()
            {
                auto view = src.view<Component>();
                for (auto srcEntity : view)
                {
                    entt::entity dstEntity = enttMap.at(src.get<Assets::IDComponent>(srcEntity).id);
                    auto& srcComponent = src.get<Component>(srcEntity);
                    dst.emplace_or_replace<Component>(dstEntity, srcComponent);
                }
            }(), ...);
    }

    template<typename... Component>
    static void CopyComponent(Assets::ComponentGroup<Component...>, entt::registry& dst, entt::registry& src, const std::unordered_map<Assets::UUID, entt::entity>& enttMap)
    {
        CopyComponent<Component...>(dst, src, enttMap);
    }

    template<typename... Component>
    static void CopyComponentIfExists(Assets::Entity& dst, Assets::Entity& src)
    {
        ([&]()
            {
                if (src.HasComponent<Component>())
                {
                    dst.AddOrReplaceComponent<Component>(src.GetComponent<Component>());
                }
            }(), ...);
    }

    template<typename... Component>
    static void CopyComponentIfExists(Assets::ComponentGroup<Component...>, Assets::Entity& dst, Assets::Entity& src)
    {
        CopyComponentIfExists<Component...>(dst, src);
    }

    void Scene::Copy(Assets::Scene& src, Assets::Scene& dst)
    {
        dst.rootID = src.rootID;
        dst.name = src.name + " copy";

        auto& srcSceneRegistry = src.registry;
        auto& dstSceneRegistry = dst.registry;
        std::unordered_map<Assets::UUID, entt::entity> enttMap;

        auto view = srcSceneRegistry.view<Assets::IDComponent>();
        for (auto e : view)
        {
            Assets::UUID uuid = srcSceneRegistry.get<Assets::IDComponent>(e).id;
            const auto& name = srcSceneRegistry.get<Assets::NameComponent>(e).name;
            const auto& parent = srcSceneRegistry.get<Assets::RelationshipComponent>(e).parent;
            Assets::Entity newEntity = dst.CreateEntityWithUUID(uuid, name, parent);
            enttMap[uuid] = (entt::entity)newEntity;
        }

        // Copy components (except IDComponent and NameComponent)
        CopyComponent(Assets::AllComponents{}, dstSceneRegistry, srcSceneRegistry, enttMap);
    }

#pragma endregion

#pragma region SceneImporter

    void SerializeEntity(std::ostringstream& out, Entity entity)
    {
        if (entity.HasComponent<IDComponent>())
        {
            auto& c = entity.GetComponent<IDComponent>();

            out << "\t\t\t\"IDComponent\" : {\n";
            out << "\t\t\t\t\"id\" : " << c.id << "\n";
            out << "\t\t\t},\n";
        }

        if (entity.HasComponent<NameComponent>())
        {
            auto& c = entity.GetComponent<NameComponent>();

            out << "\t\t\t\"NameComponent\" : {\n";
            out << "\t\t\t\t\"name\" : \"" << c.name << "\"\n";
            out << "\t\t\t},\n";
        }

        if (entity.HasComponent<RelationshipComponent>())
        {
            auto& c = entity.GetComponent<RelationshipComponent>();

            out << "\t\t\t\"RelationshipComponent\" : {\n";
            out << "\t\t\t\t\"parent\" : " << c.parent << ",\n";
            out << "\t\t\t\t\"children\" : [\n";

            for (int i = 0; i < c.children.size(); i++)
            {
                out << "\t\t\t\t\t" << c.children[i];
                if (i < (c.children.size() - 1)) out << ",";
                out << "\n";
            }

            out << "\t\t\t\t]\n";
            out << "\t\t\t},\n";
        }

        if (entity.HasComponent<TransformComponent>())
        {
            auto& c = entity.GetComponent<TransformComponent>();

            out << "\t\t\t\"TransformComponent\" : {\n";
            out << "\t\t\t\t\"position\" : " << c.position << ",\n";
            out << "\t\t\t\t\"rotation\" : " << c.rotation.GetEuler() << ",\n";
            out << "\t\t\t\t\"scale\" : " << c.scale << "\n";
            out << "\t\t\t}\n";
        }

        if (entity.HasComponent<CameraComponent>())
        {
            out << "\t\t\t,\n";

            auto& c = entity.GetComponent<CameraComponent>();

            out << "\t\t\t\"CameraComponent\" : {\n";
            
            out << "\t\t\t\t\"isPrimary\" : " << (c.isPrimary ? "true" : "false") << ",\n";
            out << "\t\t\t\t\"projectionType\" : \"" << magic_enum::enum_name<CameraComponent::ProjectionType>(c.projectionType) << "\",\n";
            
            out << "\t\t\t\t\"perspectiveFieldOfView\" : "  << c.perspectiveFieldOfView << ",\n";
            out << "\t\t\t\t\"perspectiveNear\" : " << c.perspectiveNear << ",\n";
            out << "\t\t\t\t\"perspectiveFar\" : "  << c.perspectiveFar << ",\n";

            out << "\t\t\t\t\"orthographicSize\" : "  << c.orthographicSize << ",\n";
            out << "\t\t\t\t\"orthographicNear\" : " << c.orthographicNear << ",\n";
            out << "\t\t\t\t\"orthographicFar\" : "  << c.orthographicFar << ",\n";

            out << "\t\t\t\t\"depthOfField.enabled\" : "  << (c.depthOfField.enabled ? "true" : "false") << ",\n";
            out << "\t\t\t\t\"depthOfField.enableVisualFocusDistance\" : "  << (c.depthOfField.enableVisualFocusDistance ? "true" : "false") << ",\n";
            out << "\t\t\t\t\"depthOfField.apertureRadius\" : " << c.depthOfField.apertureRadius << ",\n";
            out << "\t\t\t\t\"depthOfField.focusFalloff\" : " << c.depthOfField.focusFalloff << ",\n";
            out << "\t\t\t\t\"depthOfField.focusDistance\" : " << c.depthOfField.focusDistance << "\n";

            out << "\t\t\t}\n";
        }

        if (entity.HasComponent<MeshComponent>())
        {
            out << "\t\t\t,\n";

            auto& c = entity.GetComponent<MeshComponent>();

            out << "\t\t\t\"MeshComponent\" : {\n";
            out << "\t\t\t\t\"meshSourceHandle\" : " << c.meshSourceHandle << ",\n";
            out << "\t\t\t\t\"meshIndex\" : " << c.meshIndex << "\n";
            out << "\t\t\t}\n";
        }

        if (entity.HasComponent<DirectionalLightComponent>())
        {
            out << "\t\t\t,\n";

            auto& c = entity.GetComponent<DirectionalLightComponent>();

            out << "\t\t\t\"DirectionalLightComponent\" : {\n";
            out << "\t\t\t\t\"color\" : " << c.color << ",\n";
            out << "\t\t\t\t\"intensity\" : " << c.intensity << ",\n";
            out << "\t\t\t\t\"angularRadius\" : " << c.angularRadius << ",\n";
            out << "\t\t\t\t\"haloSize\" : " << c.haloSize << ",\n";
            out << "\t\t\t\t\"haloFalloff\" : " << c.haloFalloff << "\n";
            out << "\t\t\t}\n";
        }

        if (entity.HasComponent<DynamicSkyLightComponent>())
        {
            out << "\t\t\t,\n";

            auto& c = entity.GetComponent<DynamicSkyLightComponent>();

            out << "\t\t\t\"DynamicSkyLightComponent\" : {\n";
            out << "\t\t\t\t\"color\" : " << c.groundColor << ",\n";
            out << "\t\t\t\t\"color\" : " << c.horizonSkyColor << ",\n";
            out << "\t\t\t\t\"color\" : " << c.zenithSkyColor << "\n";
            out << "\t\t\t}\n";
        }
    }

    void DeserializEntity(simdjson::dom::element element, Scene& scene)
    {
        UUID id = element["IDComponent"]["id"].get_uint64().value();
        std::string name = element["NameComponent"]["name"].get_c_str().value();
        UUID parent = element["RelationshipComponent"]["parent"].get_uint64().value();
        auto array = element["RelationshipComponent"]["children"].get_array().value();
        
        std::vector<UUID> children;
        children.resize(array.size());
        for (int i = 0; i < array.size(); i++) 
        {
            children[i] = array.at(i).get_uint64().value();
        }

        Entity deserializedEntity = scene.CreateEntityWithUUID(id, name, parent);

        const auto& transformComponent = element["TransformComponent"];
        if (!transformComponent.error())
        {
            auto& c = deserializedEntity.GetComponent<TransformComponent>();
            
            if (!transformComponent["position"].error())
            {
                c.position = {
                    (float)transformComponent["position"].get_array().at(0).get_double().value(),
                    (float)transformComponent["position"].get_array().at(1).get_double().value(),
                    (float)transformComponent["position"].get_array().at(2).get_double().value()
                };
            }

            if (!transformComponent["rotation"].error())
            {
                c.rotation = Math::quat({
                    (float)transformComponent["rotation"].get_array().at(0).get_double().value(),
                    (float)transformComponent["rotation"].get_array().at(1).get_double().value(),
                    (float)transformComponent["rotation"].get_array().at(2).get_double().value()
                    });
            }

            if (!transformComponent["scale"].error())
            {
                c.scale = {
                    (float)transformComponent["scale"].get_array().at(0).get_double().value(),
                    (float)transformComponent["scale"].get_array().at(1).get_double().value(),
                    (float)transformComponent["scale"].get_array().at(2).get_double().value()
                };
            }
        }

        const auto& cameraComponent = element["CameraComponent"];
        if (!cameraComponent.error())
        {
            auto& c = deserializedEntity.AddComponent<CameraComponent>();
            
            if (!cameraComponent["projectionType"].error())
                c.projectionType = magic_enum::enum_cast<CameraComponent::ProjectionType>(cameraComponent["projectionType"].get_c_str().value()).value();

            if (!cameraComponent["perspectiveFieldOfView"].error())
                c.perspectiveFieldOfView = (float)cameraComponent["perspectiveFieldOfView"].get_double().value();

            if (!cameraComponent["perspectiveNear"].error())
                c.perspectiveNear = (float)cameraComponent["perspectiveNear"].get_double().value();

            if (!cameraComponent["perspectiveFar"].error())
                c.perspectiveFar = (float)cameraComponent["perspectiveFar"].get_double().value();


            if (!cameraComponent["orthographicSize"].error())
                c.orthographicSize = (float)cameraComponent["orthographicSize"].get_double().value();

            if (!cameraComponent["orthographicNear"].error())
                c.orthographicNear = (float)cameraComponent["orthographicNear"].get_double().value();

            if (!cameraComponent["orthographicFar"].error())
                c.orthographicFar = (float)cameraComponent["orthographicFar"].get_double().value();


            if (!cameraComponent["isPrimary"].error())
                c.isPrimary = cameraComponent["isPrimary"].get_bool().value();

            if (!cameraComponent["depthOfField.enabled"].error())
                c.depthOfField.enabled = cameraComponent["depthOfField.enabled"].get_bool().value();

            if (!cameraComponent["depthOfField.enableVisualFocusDistance"].error())
                c.depthOfField.enableVisualFocusDistance = cameraComponent["depthOfField.enableVisualFocusDistance"].get_bool().value();

            if (!cameraComponent["depthOfField.apertureRadius"].error())
                c.depthOfField.apertureRadius = (float)cameraComponent["depthOfField.apertureRadius"].get_double().value();

            if (!cameraComponent["depthOfField.focusFalloff"].error())
                c.depthOfField.focusFalloff = (float)cameraComponent["depthOfField.focusFalloff"].get_double().value();

            if (!cameraComponent["depthOfField.focusDistance"].error())
                c.depthOfField.focusDistance = (float)cameraComponent["depthOfField.focusDistance"].get_double().value();
        }

        const auto& meshComponent = element["MeshComponent"];
        if (!meshComponent.error())
        {
            auto& c = deserializedEntity.AddComponent<MeshComponent>();
            if(!meshComponent["meshSourceHandle"].error())
                c.meshSourceHandle = meshComponent["meshSourceHandle"].get_uint64().value();

            if(!meshComponent["meshIndex"].error())
                c.meshIndex = (uint32_t)meshComponent["meshIndex"].get_uint64().value();
        }

        const auto& directionalLightComponent = element["DirectionalLightComponent"];
        if (!directionalLightComponent.error())
        {
            auto& c = deserializedEntity.AddComponent<DirectionalLightComponent>();

            if (!directionalLightComponent["color"].error())
            {
                c.color = {
                    (float)directionalLightComponent["color"].get_array().at(0).get_double().value(),
                    (float)directionalLightComponent["color"].get_array().at(1).get_double().value(),
                    (float)directionalLightComponent["color"].get_array().at(2).get_double().value()
                };
            }

            if (!directionalLightComponent["intensity"].error())
                c.intensity = (float)directionalLightComponent["intensity"].get_double().value();

            if (!directionalLightComponent["angularRadius"].error())
                c.angularRadius = (float)directionalLightComponent["angularRadius"].get_double().value();

            if (!directionalLightComponent["haloSize"].error())
                c.haloSize = (float)directionalLightComponent["haloSize"].get_double().value();

            if (!directionalLightComponent["haloFalloff"].error())
                c.haloFalloff = (float)directionalLightComponent["haloFalloff"].get_double().value();
        }

        const auto& dynamicSkyLightComponent = element["DynamicSkyLightComponent"];
        if (!dynamicSkyLightComponent.error())
        {
            auto& c = deserializedEntity.AddComponent<DynamicSkyLightComponent>();

            if (!dynamicSkyLightComponent["groundColor"].error())
            {
                c.groundColor = {
                    (float)dynamicSkyLightComponent["groundColor"].get_array().at(0).get_double().value(),
                    (float)dynamicSkyLightComponent["groundColor"].get_array().at(1).get_double().value(),
                    (float)dynamicSkyLightComponent["groundColor"].get_array().at(2).get_double().value()
                };
            }

            if (!dynamicSkyLightComponent["horizonSkyColor"].error())
            {
                c.horizonSkyColor = {
                  (float)dynamicSkyLightComponent["horizonSkyColor"].get_array().at(0).get_double().value(),
                  (float)dynamicSkyLightComponent["horizonSkyColor"].get_array().at(1).get_double().value(),
                  (float)dynamicSkyLightComponent["horizonSkyColor"].get_array().at(2).get_double().value()
                };
            }

            if (!dynamicSkyLightComponent["zenithSkyColor"].error())
            {
                c.zenithSkyColor = {
                    (float)dynamicSkyLightComponent["zenithSkyColor"].get_array().at(0).get_double().value(),
                    (float)dynamicSkyLightComponent["zenithSkyColor"].get_array().at(1).get_double().value(),
                    (float)dynamicSkyLightComponent["zenithSkyColor"].get_array().at(2).get_double().value()
                };
            }
        }
    }

    void SerializeScene(Scene& scene,const std::filesystem::path& filePath)
    {
        std::function<void(Entity entity, std::ostringstream& out)> serialize = [&](Entity entity, std::ostringstream& out) {

            const auto& children = entity.GetChildren();

            if (scene.rootID != entity.GetUUID()) out << ",\n";

            out << "\t\t{\n";
            SerializeEntity(out, entity);
            out << "\t\t}";

            for (size_t i = 0; i < children.size(); ++i)
            {
                auto e = scene.FindEntity(children[i]);
                serialize(e, out);
            }
        };

        std::ofstream file(filePath);
        if (!file.is_open())
        {
            HE_ERROR("SerializeScene : Unable to open file for writing, {}", filePath.string());
        }

        std::ostringstream out;
        out << "{\n";

        out << "\t\"name\" : \"" << scene.name << "\",\n";
        out << "\t\"id\" : " << scene.rootID << ",\n";
        out << "\t\"entities\" : [\n";

        auto root = scene.FindEntity(scene.rootID);
        if (!root)
        {
            root = scene.CreateEntityWithUUID(scene.rootID, "root", 0);
        }

        serialize(root, out);

        out << "\n\t]\n";

        out << "}\n";

        file << out.str();
        file.close();
    }

    bool DeserializeScene(Scene& scene, const std::filesystem::path& filePath)
    {
        if (!std::filesystem::exists(filePath))
        {
            HE_ERROR("Unable to open file for reaading, {}", filePath.string());
            return false;
        }

        static simdjson::dom::parser parser;
        auto doc = parser.load(filePath.string());

        if (doc["name"].error() || doc["id"].error())
            return false;

        scene.name = filePath.stem().string();
        scene.rootID = doc["id"].get_uint64().value();

        auto entities = doc["entities"].get_array();
        if (!entities.error())
        {
            for (auto e : entities)
            {
                DeserializEntity(e, scene);
            }
        }

        return true;
    }

    SceneImporter::SceneImporter(AssetManager* pAssetManager)
        : assetManager(pAssetManager)
    {
    }

    Asset SceneImporter::Import(AssetHandle handle, const std::filesystem::path& filePath)
    {
        Asset asset = assetManager->CreateAsset(handle);
        auto& assetState = asset.Get<AssetState>();
        auto& scene = asset.Add<Scene>();

        assetState = AssetState::Loading;

        if (DeserializeScene(scene, assetManager->desc.assetsDirectory / filePath))
        {
            assetState = AssetState::Loaded;
            assetManager->OnAssetLoaded(asset);

            return asset;
        }

        return {};
    }

    Asset SceneImporter::ImportAsync(AssetHandle handle, const std::filesystem::path& filePath)
    {
        HE_PROFILE_FUNCTION();
        return {};
    }

    void SceneImporter::Save(Asset asset, const std::filesystem::path& filePath)
    {
        auto& scene = asset.Get<Scene>();
        SerializeScene(scene, assetManager->desc.assetsDirectory / filePath);
    }

    Asset SceneImporter::Create(AssetHandle handle, const std::filesystem::path& filePath)
    {
        Asset asset = assetManager->CreateAsset(handle);
        auto& assetState = asset.Get<AssetState>();
        auto& scene = asset.Add<Scene>();

        SerializeScene(scene, assetManager->desc.assetsDirectory / filePath);
        return asset;
    }

#pragma endregion
}
