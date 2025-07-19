module;

#include "HydraEngine/Base.h"
#include "entt.hpp"

export module Assets;

import HE;
import nvrhi;
import Math;
import std;
import magic_enum;

#if defined(ASSETS_BUILD_SHAREDLIB)
#   if defined(_MSC_VER)
#       define ASSETS_API __declspec(dllexport)
#   elif defined(__GNUC__)
#       define ASSETS_API __attribute__((visibility("default")))
#   else
#       define ASSETS_API
#       pragma warning "Unknown dynamic link import/export semantics."
#   endif
#else
#   if defined(_MSC_VER)
#       define ASSETS_API __declspec(dllimport)
#   else
#       define ASSETS_API
#   endif
#endif

export namespace entt {

    using entt::null;
    using entt::entity;
    using entt::registry;
    using entt::operator+;
    using entt::operator<;
    using entt::operator<=;
    using entt::operator==;
    using entt::operator>;
    using entt::operator>=;
    using entt::operator!=;

    namespace internal {

        using internal::operator-;
        using internal::operator<;
        using internal::operator<=;
        using internal::operator==;
        using internal::operator>;
        using internal::operator>=;
        using internal::operator!=;
    }
}

export namespace Assets {

    //////////////////////////////////////////////////////////////////////////
    // Basic
    //////////////////////////////////////////////////////////////////////////

    constexpr uint32_t c_Invalid = ~0u;

    template <typename T, typename = std::enable_if_t<std::is_integral_v<T>>>
    struct RandomID
    {
        T id;

        RandomID() : id(s_UniformDistribution(s_Engine)) {}
        RandomID(T id) : id(id) {}
        RandomID(const RandomID&) = default;
        operator T() const { return id; }

    private:
        inline static std::random_device s_RandomDevice;
        inline static std::mt19937_64 s_Engine{ s_RandomDevice() };
        inline static std::uniform_int_distribution<T> s_UniformDistribution{};
    };

    using UUID = RandomID<uint64_t>;
    using AssetHandle = RandomID<uint64_t>;
    using SubscriberHandle = RandomID<uint64_t>;

    struct Rotation
    {
        Rotation() = default;
        Rotation(const Math::float3& euler) { SetEuler(euler); }
        Rotation(const Math::quat& quaternion) { SetQuaternion(quaternion); }
        Rotation(std::initializer_list<float> init_list)
        {
            if (init_list.size() == 3)
            {
                auto it = init_list.begin();
                SetEuler(Math::float3(*it, *(it + 1), *(it + 2)));
            }
            else if (init_list.size() == 4)
            {
                auto it = init_list.begin();
                SetQuaternion(Math::quat(*it, *(it + 1), *(it + 2), *(it + 3)));
            }
        }

        Rotation(const Rotation& other)
            : m_Quaternion(other.m_Quaternion)
            , m_Euler(other.m_Euler)
        {
        }

        inline bool operator==(const Rotation& other) const { return m_Quaternion == other.m_Quaternion && m_Euler == other.m_Euler; }
        inline bool operator!=(const Rotation& other) const { return !(*this == other); }

        inline Rotation& operator=(const Math::float3& euler) { SetEuler(euler); return *this; }
        inline Rotation& operator=(const Math::quat& quaternion) { SetQuaternion(quaternion); return *this; }

        inline Rotation& operator+=(const Math::float3& euler) { Rotate(euler); return *this; }
        inline Rotation& operator+=(const Math::quat& quaternion) { Rotate(quaternion); return *this; }

        inline Rotation& operator-=(const Math::float3& euler) { Rotate(-euler); return *this; }
        inline Rotation& operator-=(const Math::quat& quaternion) { Rotate(inverse(quaternion)); return *this; }

        inline Rotation& operator*=(const Math::quat& quaternion) { Rotate(quaternion); return *this; }
        inline Rotation operator*(const Math::quat& quaternion) const { Rotation result = *this; result.Rotate(quaternion); return result; }

        inline friend std::ostream& operator<<(std::ostream& os, const Rotation& rotation)
        {
            os << "Euler angles: (" << rotation.m_Euler.x << ", " << rotation.m_Euler.y << ", " << rotation.m_Euler.z << ")";
            os << " Quaternion: (" << rotation.m_Quaternion.x << ", " << rotation.m_Quaternion.y << ", " << rotation.m_Quaternion.z << ", " << rotation.m_Quaternion.w << ")";
            return os;
        }

        inline Math::float3 GetEuler() const { return m_Euler; }
        inline Math::quat GetQuaternion() const { return m_Quaternion; }

        inline void SetEuler(const Math::float3& euler)
        {
            m_Euler = euler;
            m_Quaternion = normalize(Math::quat(m_Euler));
        }

        inline void SetQuaternion(const Math::quat& quaternion)
        {
            m_Quaternion = normalize(quaternion);
            m_Euler = eulerAngles(m_Quaternion);
        }

        inline void Rotate(const Math::float3& euler)
        {
            m_Quaternion *= normalize(Math::quat(euler));
            m_Euler = eulerAngles(m_Quaternion);
        }

        inline void Rotate(const Math::quat& quaternion)
        {
            m_Quaternion *= normalize(quaternion);
            m_Euler = eulerAngles(m_Quaternion);
        }

        inline Math::float4x4 GetMatrix() const { return toMat4(m_Quaternion); }

        inline Math::float3 ClampEuler(const Math::float3& euler) const
        {
            Math::float3 clamped = euler;
            for (int i = 0; i < 3; ++i)
            {
                if (clamped[i] > 180.0f) clamped[i] -= 360.0f;
                else if (clamped[i] < -180.0f) clamped[i] += 360.0f;
            }
            return clamped;
        }

    private:
        Math::quat m_Quaternion = { 1.0f, 0.0f, 0.0f, 0.0f };
        Math::float3 m_Euler = { 0.0f, 0.0f, 0.0f };
    };

    struct DescriptorTableManager;
    typedef int DescriptorIndex;

    // Stores a descriptor index in a descriptor table. Releases the descriptor when destroyed.
    struct DescriptorHandle
    {
        ASSETS_API DescriptorHandle();
        ASSETS_API DescriptorHandle(const HE::Ref<DescriptorTableManager>& managerPtr, DescriptorIndex index);
        ASSETS_API ~DescriptorHandle();
        ASSETS_API DescriptorIndex Get() const; // For ResourceDescriptorHeap Index instead of a table relative index,  This value is volatile if the descriptor table resizes and needs to be refetched
        DescriptorIndex GetIndexInHeap() const;
        bool IsValid() const { return m_DescriptorIndex >= 0 && !m_Manager.expired(); }
        void Reset() { m_DescriptorIndex = -1; m_Manager.reset(); }

        // Movable but non-copyable
        DescriptorHandle(const DescriptorHandle&) = delete;
        DescriptorHandle(DescriptorHandle&&) = default;
        DescriptorHandle& operator=(const DescriptorHandle&) = delete;
        DescriptorHandle& operator=(DescriptorHandle&&) = default;

    private:
        std::weak_ptr<DescriptorTableManager> m_Manager;
        DescriptorIndex m_DescriptorIndex;
    };

    struct DescriptorTableManager : public std::enable_shared_from_this<DescriptorTableManager>
    {
        ASSETS_API DescriptorTableManager(nvrhi::IDevice* device, nvrhi::IBindingLayout* layout);
        ASSETS_API ~DescriptorTableManager();
        ASSETS_API DescriptorIndex CreateDescriptor(nvrhi::BindingSetItem item);
        ASSETS_API DescriptorHandle CreateDescriptorHandle(nvrhi::BindingSetItem item);
        ASSETS_API nvrhi::BindingSetItem GetDescriptor(DescriptorIndex index);
        ASSETS_API void ReleaseDescriptor(DescriptorIndex index);
        nvrhi::IDescriptorTable* GetDescriptorTable() const { return m_DescriptorTable; }

    protected:
        // Custom hasher that doesn't look at the binding slot
        struct BindingSetItemHasher
        {
            std::size_t operator()(const nvrhi::BindingSetItem& item) const
            {
                size_t hash = 0;
                nvrhi::hash_combine(hash, item.resourceHandle);
                nvrhi::hash_combine(hash, item.type);
                nvrhi::hash_combine(hash, item.format);
                nvrhi::hash_combine(hash, item.dimension);
                nvrhi::hash_combine(hash, item.rawData[0]);
                nvrhi::hash_combine(hash, item.rawData[1]);
                return hash;
            }
        };

        // Custom equality tester that doesn't look at the binding slot
        struct BindingSetItemsEqual
        {
            bool operator()(const nvrhi::BindingSetItem& a, const nvrhi::BindingSetItem& b) const
            {
                return a.resourceHandle == b.resourceHandle
                    && a.type == b.type
                    && a.format == b.format
                    && a.dimension == b.dimension
                    && a.subresources == b.subresources;
            }
        };

        nvrhi::DeviceHandle m_Device;
        nvrhi::DescriptorTableHandle m_DescriptorTable;
        std::vector<nvrhi::BindingSetItem> m_Descriptors;
        std::unordered_map<nvrhi::BindingSetItem, DescriptorIndex, BindingSetItemHasher, BindingSetItemsEqual> m_DescriptorIndexMap;
        std::vector<bool> m_AllocatedDescriptors;
        int m_SearchStart = 0;
    };

    enum class AssetType : uint8_t
    {
        None,
        Scene,
        Prefab,
        Texture2D,
        MeshSource,
        Material,
        PhysicsMaterial,
        AudioSource,
        AnimationClip,
        Shader,
        Font
    };

    struct AssetMetadata
    {
        AssetType type = AssetType::None;
        std::filesystem::path filePath;

        operator bool() const { return type != AssetType::None; }
    };

    enum class AssetFlags
    {
        None = 0,
        IsMemoryOnly = BIT(0)
    };
    HE_ENUM_CLASS_FLAG_OPERATORS(AssetFlags)

    struct AssetDependencies
    {
        std::vector<AssetHandle> dependencies;
    };

    enum class AssetState : uint8_t
    {
        None,
        Failed,
        Loading,
        Loaded
    };

    enum class AssetImportingMode : uint8_t
    {
        Sync,
        Async,
    };

    //////////////////////////////////////////////////////////////////////////
    // Texture
    ////////////////////////////////////////////////////////////////////////// 

    struct Texture
    {
        nvrhi::TextureHandle texture;
        DescriptorHandle descriptor;
    };

    //////////////////////////////////////////////////////////////////////////
    // Material
    //////////////////////////////////////////////////////////////////////////

    enum class UVSet
    {
        UV0,
        UV1,
    };

    struct Material
    {
        std::string name = "None";
        Math::float4 baseColor = { 1.0f, 1.0f, 1.0f, 1.0f };
        
        float metallic = 0.0f;								  // Range(0, 1)
        float roughness = 0.5f;								  // Range(0, 1)
        float reflectance = 0.35f;                            // Range(0, 1)

        Math::float3 emissiveColor = { 0.0f, 0.0f, 0.0f };
        float emissiveEV = 0.0f;							  // Range(-24, 24)

        AssetHandle baseTextureHandle = 0;
        AssetHandle normalTextureHandle = 0;
        AssetHandle metallicRoughnessTextureHandle = 0;
        AssetHandle emissiveTextureHandle = 0;

        // NOTE: a single transform/uvSet should handle most cases. consider refactoring it later if more flexibility is needed.
        UVSet uvSet = UVSet::UV0;
        glm::vec2 offset = { 0.0f, 0.0f };
        glm::vec2 scale = { 1.0f, 1.0f };
        float rotation = 0.0f;
    };

    //////////////////////////////////////////////////////////////////////////
    // MeshSource
    //////////////////////////////////////////////////////////////////////////

    enum class VertexAttribute : uint8_t
    {
        Position,
        Normal,
        Tangent,
        TexCoord0,
        TexCoord1,
        BoneIndices,
        BoneWeights,
    };

    enum class MeshGeometryPrimitiveType : uint8_t
    {
        Triangles,
        Lines,
        LineStrip,
    };

    enum class MeshType : uint8_t
    {
        Triangles,
        CurvePolytubes,
        CurveDisjointOrthogonalTriangleStrips,
        CurveLinearSweptSpheres,
    };

    ASSETS_API uint32_t GetVertexAttributeSize(VertexAttribute attr);

    struct Mesh;
    struct MeshGeometry
    {
        Mesh* mesh = nullptr;
        MeshGeometryPrimitiveType type = MeshGeometryPrimitiveType::Triangles;
        Math::box3 aabb;
        uint32_t indexOffsetInMesh = 0;
        uint32_t vertexOffsetInMesh = 0;
        uint32_t indexCount = 0;
        uint32_t vertexCount = 0;
        AssetHandle materailHandle = 0;
        uint32_t index = 0;

        template<typename T> T* GetAttribute(VertexAttribute attr);
        template<typename T> std::span<T> GetAttributeSpan(VertexAttribute attr);
        ASSETS_API const nvrhi::BufferRange GetVertexRange(VertexAttribute attr) const;
        ASSETS_API const nvrhi::BufferRange GetIndexRange() const;
        ASSETS_API uint32_t* Getindices();
    };

    struct MeshSource;
    struct Mesh
    {
        std::string name = "None";
        MeshSource* meshSource = nullptr;
        MeshType type = MeshType::Triangles;
        Math::box3 aabb;
        uint32_t indexOffset = 0;
        uint32_t indexCount = 0;
        uint32_t vertexOffset = 0;
        uint32_t vertexCount = 0;
        uint32_t geometryOffset = 0;
        uint32_t geometryCount = 0;
        uint32_t index = 0;
        nvrhi::rt::AccelStructHandle accelStruct;

        template<typename T> T* GetAttribute(VertexAttribute attr);
        template<typename T> std::span<T> GetAttributeSpan(VertexAttribute attr);
        ASSETS_API uint32_t* Getindices();
        ASSETS_API const nvrhi::BufferRange GetIndexRange() const;
        ASSETS_API std::span<MeshGeometry> GetGeometrySpan();
    };

    struct MeshSourecHierarchy;
    struct Node
    {
        std::string name = "None";
        Math::float4x4 transform;
        uint32_t childrenOffset = 0;
        uint32_t childrenCount = 0;
        uint32_t meshIndex = c_Invalid;

        ASSETS_API std::span<Node> GetChildren(MeshSourecHierarchy& meshSourecHierarchy);
    };

    struct MeshSourecHierarchy
    {
        std::vector<Node> nodes;
        Node root;
    };

    struct MeshSource
    {
        std::array<nvrhi::BufferRange, magic_enum::enum_count<VertexAttribute>()> vertexBufferRanges;
        std::vector<uint32_t> cpuIndexBuffer;
        std::vector<uint8_t>  cpuVertexBuffer; // [position][Normal][Tangent][...]
        std::vector<Mesh> meshes;
        std::vector<MeshGeometry> geometries;
        uint32_t vertexCount = 0;
        uint32_t materialCount = 0;
        uint32_t textureCount = 0;

        template<typename T> T* GetAttribute(VertexAttribute attr);
        template<typename T> std::span<T> GetAttributeSpan(VertexAttribute attr);
        bool HasAttribute(VertexAttribute attr) const { return vertexBufferRanges[int(attr)].byteSize != 0; }
        const nvrhi::BufferRange& getVertexBufferRange(VertexAttribute attr) const { return vertexBufferRanges[int(attr)]; }
    };

    const nvrhi::BufferRange MeshGeometry::GetVertexRange(VertexAttribute attr) const
    {
        auto attrSize = GetVertexAttributeSize(attr);
        return nvrhi::BufferRange(mesh->meshSource->getVertexBufferRange(attr).byteOffset + (mesh->vertexOffset + vertexOffsetInMesh) * attrSize, vertexCount * attrSize);
    }

    const nvrhi::BufferRange MeshGeometry::GetIndexRange() const { return nvrhi::BufferRange((mesh->indexOffset + indexOffsetInMesh) * sizeof(uint32_t), indexCount * sizeof(uint32_t)); }

    template<typename T>
    T* MeshGeometry::GetAttribute(VertexAttribute attr) { return reinterpret_cast<T*>(mesh->meshSource->cpuVertexBuffer.data() + (mesh->vertexOffset + vertexOffsetInMesh) * sizeof(T)); }

    template<typename T>
    std::span<T> MeshGeometry::GetAttributeSpan(VertexAttribute attr)
    {
        const auto& range = mesh->meshSource->vertexBufferRanges[int(attr)];
        T* ptr = reinterpret_cast<T*>(mesh->meshSource->cpuVertexBuffer.data() + range.byteOffset + (mesh->vertexOffset + vertexOffsetInMesh) * sizeof(T));
        return std::span<T>(ptr, vertexCount);
    }

    uint32_t* MeshGeometry::Getindices() { return mesh->meshSource->cpuIndexBuffer.data() + mesh->indexOffset + indexOffsetInMesh; }

    std::span<MeshGeometry> Assets::Mesh::GetGeometrySpan() { return std::span<MeshGeometry>(meshSource->geometries.data() + geometryOffset, geometryCount); }

    template<typename T>
    T* Mesh::GetAttribute(VertexAttribute attr) { return reinterpret_cast<T*>(meshSource->cpuVertexBuffer.data() + vertexOffset * sizeof(T)); }

    template<typename T>
    std::span<T> Mesh::GetAttributeSpan(VertexAttribute attr)
    {
        const auto& range = meshSource->vertexBufferRanges[int(attr)];
        T* ptr = reinterpret_cast<T*>(meshSource->cpuVertexBuffer.data() + range.byteOffset + vertexOffset * sizeof(T));
        return std::span<T>(ptr, vertexCount);
    }

    uint32_t* Mesh::Getindices() { return meshSource->cpuIndexBuffer.data() + indexOffset; }

    const nvrhi::BufferRange Mesh::GetIndexRange() const { return nvrhi::BufferRange(indexOffset * sizeof(uint32_t), indexCount * sizeof(uint32_t)); }

    template<typename T>
    T* MeshSource::GetAttribute(VertexAttribute attr)
    {
        const auto& range = vertexBufferRanges[int(attr)];
        return reinterpret_cast<T*>(cpuVertexBuffer.data() + range.byteOffset);
    }

    template<typename T>
    std::span<T> MeshSource::GetAttributeSpan(VertexAttribute attr)
    {
        const auto& range = vertexBufferRanges[int(attr)];
        T* ptr = reinterpret_cast<T*>(cpuVertexBuffer.data() + range.byteOffset);
        return std::span<T>(ptr, vertexCount);
    }

    std::span<Node> Node::GetChildren(MeshSourecHierarchy& meshSourecHierarchy)
    {
        return childrenCount ? std::span<Node>(&meshSourecHierarchy.nodes[childrenOffset], childrenCount) : std::span<Node>();;
    }

    //////////////////////////////////////////////////////////////////////////
    // Scene
    //////////////////////////////////////////////////////////////////////////

    struct IDComponent
    {
        UUID id;
    };

    struct NameComponent
    {
        std::string name;
    };

    struct RelationshipComponent
    {
        std::vector<UUID> children;
        UUID parent = 0;
    };

    struct TransformComponent
    {
        Math::float3 position = { 0.0f, 0.0f, 0.0f };
        Rotation rotation = { 1.0f, 0.0f, 0.0f, 0.0f };
        Math::float3 scale = { 1.0f, 1.0f, 1.0f };

        inline Math::float3   GetRight()     const { return Math::rotate(rotation.GetQuaternion(), Math::float3(1.0f, 0.0f, 0.0f)); }
        inline Math::float3   GetUp()        const { return Math::rotate(rotation.GetQuaternion(), Math::float3(0.0f, 1.0f, 0.0f)); }
        inline Math::float3   GetForward()   const { return Math::rotate(rotation.GetQuaternion(), Math::float3(0.0f, 0.0f, 1.0f)); }
        inline Math::float4x4 GetTransform() const { return Math::translate(Math::float4x4(1.0f), position) * rotation.GetMatrix() * Math::scale(Math::float4x4(1.0f), scale); }

        inline void SetTransform(const Math::float4x4& localTransform)
        {
            Math::vec3 p, s, skew;
            Math::quat quaternion;
            Math::vec4 perspective;

            Math::decompose(localTransform, s, quaternion, p, skew, perspective);

            position = p;
            rotation = quaternion;
            scale = s;
        }
    };

    struct CameraComponent
    {
        enum class ProjectionType { Perspective = 0, Orthographic = 1 };

        bool isPrimary = true;
        ProjectionType projectionType = ProjectionType::Perspective;

        float perspectiveFieldOfView = 60.0f;
        float perspectiveNear = 0.001f;
        float perspectiveFar = 1000.0f;

        float orthographicSize = 5.0f;
        float orthographicNear = 0.1f;
        float orthographicFar = 100.0f;

        struct DepthOfField
        {
            bool enabled = false;
            bool enableVisualFocusDistance = false;
            float apertureRadius = 1.0f;
            float focusFalloff = 0.0f;
            float focusDistance = 10.0f;
        }depthOfField;
    };

    struct MeshComponent
    {
        AssetHandle meshSourceHandle = 0;
        uint32_t meshIndex = 0;

        MeshComponent() = default;
        MeshComponent(AssetHandle pHandle, uint32_t pMeshIndex) : meshSourceHandle(pHandle), meshIndex(pMeshIndex) {}
    };

    struct DirectionalLightComponent
    {
        Math::float3 color = { 0.98f, 0.92f, 0.89f };
        float intensity = 110000.0f;
        float angularRadius = 1.9f;
        float haloSize = 10.0f;
        float haloFalloff = 80.0f;
    };

    struct DynamicSkyLightComponent
    {
        Math::float3 groundColor = { 0.35f, 0.3f, 0.35f };
        Math::float3 horizonSkyColor = { 1.0f, 1.0f, 1.0f };
        Math::float3 zenithSkyColor = { 0.07f, 0.36f, 0.72f };
    };

    template<typename... Component>
    struct ComponentGroup {};

    using AllComponents = ComponentGroup <
        TransformComponent,
        RelationshipComponent,
        CameraComponent,
        MeshComponent,
        DirectionalLightComponent,
        DynamicSkyLightComponent
    >;

    struct Entity;
    struct Scene
    {
        entt::registry registry;
        std::unordered_map<UUID, entt::entity> entityMap;
        std::string name;
        UUID rootID;

        ASSETS_API Entity GetRootEntity();
        ASSETS_API Entity CreateEntity(const std::string& name, UUID parent);
        ASSETS_API Entity CreateEntityWithUUID(UUID uuid, const std::string& name, UUID parent);
        ASSETS_API Entity CreateEntityWithUUID(UUID uuid, UUID parent);
        ASSETS_API void DestroyEntity(Entity entity);
        ASSETS_API void DestroyEntity(UUID id);
        ASSETS_API Entity FindEntity(std::string_view name);
        ASSETS_API Entity FindEntity(UUID id);
        ASSETS_API Math::float4x4 ConvertToWorldSpace(Entity entity);
        ASSETS_API Math::float4x4 ConvertToLocalSpace(Entity entity, Math::float4x4 wt);

        ASSETS_API static void Copy(Assets::Scene& src, Assets::Scene& dst);
    };


    struct Entity
    {
        entt::entity handle{ entt::null };
        Scene* scene = nullptr;

        Entity() = default;
        Entity(entt::entity pHandle, Scene* pScene) : handle(pHandle), scene(pScene) {}
        Entity(const Entity& other) = default;

        bool operator==(const Entity& other) const { return handle == other.handle; }
        bool operator!=(const Entity& other) const { return !(*this == other); }

        operator bool() const { return  scene && scene->registry.valid(handle); }
        operator entt::entity() const { return handle; }
        operator uint32_t() const { return (uint32_t)handle; }

        template<typename T, typename... Args>
        T& AddComponent(Args&&... args)
        {
            HE_VERIFY(!HasComponent<T>(), "Entity already has component!");
            T& component = scene->registry.emplace<T>(handle, std::forward<Args>(args)...);

            return component;
        }

        template<typename T, typename... Args>
        T& AddOrReplaceComponent(Args&&... args)
        {
            T& component = scene->registry.emplace_or_replace<T>(handle, std::forward<Args>(args)...);
            return component;
        }

        template<typename T>
        T& GetComponent()
        {
            HE_ASSERT(HasComponent<T>(), "Entity does not have component!");
            return scene->registry.get<T>(handle);
        }

        template<typename... T>
        bool HasComponent()
        {
            HE_ASSERT(scene && scene->registry.valid(handle), "Entity handle not valid");

            // Check if all specified components exist
            if constexpr (sizeof...(T) == 1)
            {
                return scene->registry.try_get<T...>(handle) != nullptr;
            }
            else
            {
                return std::apply([](auto&&... args) { return ((args != nullptr) && ...); }, scene->registry.try_get<T...>(handle));
            }
        }

        template<typename T>
        void RemoveComponent()
        {
            HE_VERIFY(HasComponent<T>(), "Entity does not have component!");
            scene->registry.remove<T>(handle);
        }

        const UUID GetUUID() { return GetComponent<IDComponent>().id; }
        const std::string& GetName() { return GetComponent<NameComponent>().name; }
        void SetName(const std::string& name) { GetComponent<NameComponent>().name = name; }

        ASSETS_API Entity GetParent();
        ASSETS_API void AddChild(Entity entity);
        ASSETS_API void RemoveChild(Entity entity);
        ASSETS_API void RemoveChild(UUID id);
        ASSETS_API std::vector<UUID>& GetChildren();
        ASSETS_API TransformComponent& GetTransform();
        ASSETS_API Math::float4x4 GetTransformMatrix();
        ASSETS_API void SetLocalTransform(const Math::float4x4& localTransform);
        ASSETS_API void SetWorldTransform(const Math::float4x4& worldTransform);
        ASSETS_API Math::float4x4 GetWorldSpaceTransformMatrix();
    };

    //////////////////////////////////////////////////////////////////////////
    // AssetManager
    //////////////////////////////////////////////////////////////////////////

    struct AssetManager;
    struct Asset
    {
        entt::entity id = entt::null;
        AssetManager* assetManager = nullptr;

        AssetState GetState() { return Get<AssetState>(); };
        AssetHandle GetHandle() { return Get<AssetHandle>(); };

        Asset() = default;
        Asset(entt::entity pHandle, AssetManager* pAssetManager) : id(pHandle), assetManager(pAssetManager) {}
        Asset(const Asset& other) = default;

        bool operator==(const Asset& other) const { return id == other.id; }
        bool operator!=(const Asset& other) const { return !(*this == other); }

        operator bool() const;
        operator entt::entity() const { return id; }
        operator uint32_t() const { return (uint32_t)id; }

        template<typename T, typename... Args> T& Add(Args&&... args);
        template<typename T>                   T& Get();
        template<typename... T>              bool Has();
        template<typename T>                 void Remove();
    };

    struct AssetEventCallback
    {
        virtual ~AssetEventCallback() {}
        virtual void OnAssetUnloaded(Asset asset) {}
        virtual void OnAssetLoaded(Asset asset) {}
        virtual void OnAssetReloaded(Asset asset) {}
        virtual void OnAssetRemoved(AssetHandle handle) {}
        virtual void OnAssetSaved(Asset asset) {}
        virtual void OnAssetCreated(Asset asset) {}
    };

    struct IAssetImporter
    {
        virtual Asset Import(AssetHandle handle, const std::filesystem::path& fielPath) = 0;
        virtual Asset ImportAsync(AssetHandle handle, const std::filesystem::path& filePath) { return {}; };
        virtual Asset Create(AssetHandle handle, const std::filesystem::path& filePath) = 0;
        virtual void Save(Asset asset, const std::filesystem::path& filePath) = 0;
        virtual bool IsSupportAsyncLoading() { return false; }
    };

    struct AssetManager;
    struct AssetImporter
    {
        std::array<HE::Scope<IAssetImporter>, magic_enum::enum_count<AssetType>()> importers;

        void Init(AssetManager* assetManager);
        Asset ImportAsset(AssetHandle handle, const std::filesystem::path& filePath, AssetImportingMode mode);
        Asset CreateAsset(AssetHandle handle, const std::filesystem::path& filePath);
        void SaveAsset(Asset asset, const std::filesystem::path& filePath);
        AssetType GetAssetTypeFromFileExtension(const std::filesystem::path& extension);
    };

    struct AssetManagerDesc
    {
        AssetImportingMode importMode = AssetImportingMode::Async;
        std::filesystem::path assetsDirectory;
        std::filesystem::path assetsRegistryFilePath;
    };

    struct AssetManager
    {
        AssetManagerDesc desc;
        nvrhi::DeviceHandle device;
        entt::registry registry;
        std::map<AssetHandle, Asset> assetMap;
        std::map<AssetHandle, AssetMetadata> metaMap;
        std::unordered_map<std::filesystem::path, AssetHandle> pathToHandleMap;
        std::unordered_map<SubscriberHandle, AssetEventCallback*> subscribers;
        AssetImporter assetImporter;
        uint32_t asyncTaskCount = 0;
        std::mutex registryMutex;
        std::mutex metaMutex;
        std::mutex assetMutex;

        AssetManager() = default;
        ASSETS_API AssetManager(nvrhi::DeviceHandle device, const AssetManagerDesc& desc);
        ASSETS_API void Init(nvrhi::DeviceHandle device, const AssetManagerDesc& desc);
        
        ASSETS_API Asset GetAsset(AssetHandle handle);
        template<typename T> T* GetAsset(AssetHandle handle);
        ASSETS_API Asset FindAsset(AssetHandle handle);

        ASSETS_API Asset CreateAsset(AssetHandle handle);
        ASSETS_API Asset CreateAsset(const std::filesystem::path& filePath);
        ASSETS_API AssetHandle GetOrMakeAsset(const std::filesystem::path& filePath, const std::filesystem::path& newAssetPath, bool overwriteExisting = false);
        ASSETS_API void MarkAsMemoryOnlyAsset(Asset asset, AssetType type);

        ASSETS_API void DestroyAsset(AssetHandle handle);
        ASSETS_API void DestroyAsset(Asset asset);

        ASSETS_API void SaveAsset(AssetHandle handle);
        ASSETS_API void ReloadAsset(AssetHandle handle);
        ASSETS_API void UnloadAsset(AssetHandle handle);
        ASSETS_API void UnloadAllAssets();
        ASSETS_API void RemoveAsset(AssetHandle handle);
        ASSETS_API AssetHandle ImportAsset(const std::filesystem::path& filePath, bool loadToMemeory = true);

        ASSETS_API bool RegisterMetadata(AssetHandle handle, const AssetMetadata& meta);
        ASSETS_API void UnRegisterMetadata(AssetHandle handle);
        ASSETS_API bool UpdateMetadate(AssetHandle handle, const AssetMetadata& metadata);
        ASSETS_API const AssetMetadata& GetMetadata(AssetHandle handle) const;
        ASSETS_API AssetType GetAssetType(AssetHandle handle) const;
        ASSETS_API const std::filesystem::path& GetFilePath(AssetHandle handle) const;
        ASSETS_API AssetHandle GetAssetHandleFromFilePath(const std::filesystem::path& filePath);
        ASSETS_API std::filesystem::path GetAssetFileSystemPath(AssetHandle handle) const;
        ASSETS_API bool IsAssetFilePathValid(const std::filesystem::path& filePath);

        inline bool IsAssetHandleValid(AssetHandle handle) const { return handle != 0 && metaMap.contains(handle); }
        inline bool IsAssetLoaded(AssetHandle handle) const { return assetMap.contains(handle); }

        ASSETS_API SubscriberHandle Subscribe(AssetEventCallback* assetEventCallback);
        ASSETS_API void UnSubscribe(SubscriberHandle handle);

        ASSETS_API void OnAssetLoaded(Asset asset);
        ASSETS_API void Serialize();
        ASSETS_API bool Deserialize();
        ASSETS_API void Reset();
    };

    template<typename T>
    T* AssetManager::GetAsset(AssetHandle handle)
    {
        auto asset = GetAsset(handle);
        if (asset)
        {
            return asset.Has<T>() ? &asset.Get<T>() : nullptr;
        }

        return nullptr;
    }

    inline Asset::operator bool() const { return  assetManager && assetManager->registry.valid(id); }

    template<typename T, typename ...Args>
    T& Asset::Add(Args&& ...args)
    {
        HE_VERIFY(!Has<T>());

       std::scoped_lock<std::mutex> lock(assetManager->assetMutex);
       T& assetData = assetManager->registry.emplace<T>(id, std::forward<Args>(args)...);

       return assetData;
    }
    
    template<typename T>
    T& Asset::Get()
    {
        HE_ASSERT(Has<T>());
        return assetManager->registry.get<T>(id);
    }

    template<typename ...T>
    bool Asset::Has()
    {
        HE_ASSERT(assetManager && assetManager->registry.valid(id), "Asset id not valid");

        // Check if all specified AssetData exist
        if constexpr (sizeof...(T) == 1)
        {
            return assetManager->registry.try_get<T...>(id) != nullptr;
        }
        else
        {
            return std::apply([](auto&&... args) { return ((args != nullptr) && ...); }, assetManager->registry.try_get<T...>(id));
        }
    }
   
    template<typename T>
    void Asset::Remove()
    {
        HE_VERIFY(Has<T>());

        std::scoped_lock<std::mutex> lock(assetManager->assetMutex);
        assetManager->registry.remove<T>(id);
    }

    //////////////////////////////////////////////////////////////////////////
    // Importers
    //////////////////////////////////////////////////////////////////////////

    struct SceneImporter : public IAssetImporter
    {
        AssetManager* assetManager;

        SceneImporter(AssetManager* assetManager);
        Asset Import(AssetHandle handle, const std::filesystem::path& filePath) override;
        Asset ImportAsync(AssetHandle handle, const std::filesystem::path& filePath) override;
        Asset Create(AssetHandle handle, const std::filesystem::path& filePath) override;
        void  Save(Asset asset, const std::filesystem::path& filePath) override;
        bool  IsSupportAsyncLoading() override { return false; }
    };

    struct TextureImporter : public IAssetImporter
    {
        AssetManager* assetManager;

        TextureImporter(AssetManager* assetManager);
        Asset Import(AssetHandle handle, const std::filesystem::path& filePath) override;
        Asset ImportAsync(AssetHandle handle, const std::filesystem::path& filePath) override;
        Asset Create(AssetHandle handle, const std::filesystem::path& filePath) override;
        void  Save(Asset asset, const std::filesystem::path& filePath) override;
        bool  IsSupportAsyncLoading() override { return true; }
    };

    struct MeshSourceImporter : public IAssetImporter
    {
        AssetManager* assetManager;

        MeshSourceImporter(AssetManager* assetManager);
        Asset Import(AssetHandle handle, const std::filesystem::path& filePath) override;
        Asset ImportAsync(AssetHandle handle, const std::filesystem::path& filePath) override;
        Asset Create(AssetHandle handle, const std::filesystem::path& filePath) override;
        void  Save(Asset asset, const std::filesystem::path& filePath) override;
        bool  IsSupportAsyncLoading() override { return true; }
    };

    //////////////////////////////////////////////////////////////////////////
    // Utils
    //////////////////////////////////////////////////////////////////////////

    ASSETS_API nvrhi::TextureHandle LoadTexture(const std::filesystem::path& filePath, nvrhi::IDevice* device, nvrhi::ICommandList* commandList);
    ASSETS_API nvrhi::TextureHandle LoadTexture(HE::Buffer buffer, nvrhi::IDevice* device, nvrhi::ICommandList* commandList, const std::string_view& name = {});

}


export namespace std {

    template <typename T>
    struct hash<Assets::RandomID<T>>
    {
        size_t operator()(const Assets::RandomID<T>& id) const noexcept
        {
            return (T)id;
        }
    };
}