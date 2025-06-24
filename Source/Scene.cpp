#include "HydraEngine/Base.h"

import Assets;
import HE;
import Math;
import std;
import simdjson;

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

        if (entity.HasComponent<MeshComponent>())
        {
            out << "\t\t\t,\n";

            auto& c = entity.GetComponent<MeshComponent>();

            out << "\t\t\t\"MeshComponent\" : {\n";
            out << "\t\t\t\t\"meshSourceHandle\" : " << c.meshSourceHandle << ",\n";
            out << "\t\t\t\t\"meshIndex\" : " << c.meshIndex << "\n";
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

        const auto& meshComponent = element["MeshComponent"];
        if (!meshComponent.error())
        {
            auto& c = deserializedEntity.AddComponent<MeshComponent>();
            if(!meshComponent["meshSourceHandle"].error())
                c.meshSourceHandle = meshComponent["meshSourceHandle"].get_uint64().value();

            if(!meshComponent["meshIndex"].error())
                c.meshIndex = (uint32_t)meshComponent["meshIndex"].get_uint64().value();
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
            HE_ERROR("Unable to open file for writing, {}", filePath.string());
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

    Asset SceneImporter::Import(const std::filesystem::path& filePath)
    {
        Asset asset = assetManager->CreateAsset(AssetType::Scene);
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

    Asset SceneImporter::ImportAsync(const std::filesystem::path& metadata)
    {
        HE_PROFILE_FUNCTION();
        return {};
    }

    void SceneImporter::Save(Asset asset, const std::filesystem::path& filePath)
    {
        Scene& scene = asset.Get<Scene>();
        SerializeScene(scene, assetManager->desc.assetsDirectory / filePath);
    }

    Asset SceneImporter::Create(const std::filesystem::path& filePath)
    {
        Asset asset = assetManager->CreateAsset(AssetType::Scene);
        auto& assetState = asset.Get<AssetState>();
        auto& scene = asset.Add<Scene>();

        SerializeScene(scene, assetManager->desc.assetsDirectory / filePath);
        return asset;
    }

#pragma endregion
}
