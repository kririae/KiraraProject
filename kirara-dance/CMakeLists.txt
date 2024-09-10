project(kirara-dance LANGUAGES C CXX)

include(KRR_AddModule)
include(KRR_Message)

krr_message(INFO "Downloading DiligentEngine... This might take long")

CPMAddPackage(
  NAME DiligentEngine
  URL https://github.com/DiligentGraphics/DiligentEngine/releases/download/v2.5.6/DiligentEngine_v2.5.6.zip
  OPTIONS
    "DILIGENT_NO_DIRECT3D11 ON"
    "DILIGENT_NO_DIRECT3D12 ON"
    "DILIGENT_NO_OPENGL ON"
    "DILIGENT_NO_METAL ON"
    "DILIGENT_NO_WEBGPU ON"

    "DILIGENT_BUILD_SAMPLES OFF"
    "DILIGENT_BUILD_TOOLS ON"
)

add_executable(kirara-dance
  src/main.cpp
)

target_include_directories(kirara-dance
  PRIVATE
    ${DiligentCore_SOURCE_DIR}
    ${DiligentTools_SOURCE_DIR}
    ${DiligentFX_SOURCE_DIR}
)

target_link_libraries(kirara-dance
  PRIVATE
    Diligent-BuildSettings
    Diligent-GraphicsEngineVk-shared
    xcb
)
