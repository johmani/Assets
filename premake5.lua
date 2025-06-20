group "Plugins/Assets"

project "Assets"
    kind "SharedLib"
    language "C++"
    cppdialect  "C++latest"
    staticruntime "Off"
    targetdir (binOutputDir)
    objdir (IntermediatesOutputDir)

    LinkHydra(includSourceCode)
    SetHydraFilters()

    includedirs {
        "Include/Assets",
        "ThirdParty/cgltf",
        "ThirdParty/entt",
    }

    files {
    
        "Source/**.cpp",
        "Include/**.h",
        "Include/**.cppm",
        "*.lua",

        "ThirdParty/entt/entt.hpp",
        "ThirdParty/cgltf/*.h",
    }

    defines {
    
        "ASSETS_BUILD_SHAREDLIB",
    }

group "Plugins"
