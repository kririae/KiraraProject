// Edit-sequence tests for the OptiX runtime.
//
// A test-side scene model (`Spec`) describes a scene. A `Rig` keeps one live Context and one
// OptixHandler for a whole test, and applies each edit twice: to the live context through the
// public API, and to the model. After a frame (collectGarbage, sync, clearDirty) the rig renders
// one sample per pixel and requires the image to be bit-exact with a fresh Context and handler
// built from the model.
//
// The fuzz test draws operations from a seeded std::mt19937. A run is fully determined by
// (seed, steps). On failure the test prints the seed, the step and every operation applied so
// far. Environment variables select a longer run:
//
//   FLUX_FUZZ_SEEDS       number of seeds to run (default 3)
//   FLUX_FUZZ_STEPS       steps per seed (default 30)
//   FLUX_FUZZ_FIRST_SEED  first seed; the others follow it (default 1)
//   FLUX_FUZZ_VERBOSE     when set, print the operations of every seed
//
// Why the comparison is exact: the live buffer is cleared before it renders, and the sample
// offset of both handlers stays zero, so both sides render sample batch index 0 of the same
// sequence. Emitting primitives take light slots in Context ID order, after the point light and
// the environment map, so the model keeps primitives in creation order and the fresh context
// creates them in that order. Indices differ between the sides (the live context has holes and
// reused indices) and must not change the image. This assumes that no two instances are hit at
// exactly the same distance, which holds for random geometry. Image textures read fixture files,
// so the fresh side loads its own images with the same pixels.

#include <gtest/gtest.h>

#include <Eigen/Geometry>
#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <format>
#include <iostream>
#include <map>
#include <memory>
#include <numbers>
#include <optional>
#include <random>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "TestUtils.h"
#include "flux/Integrator/PathIntegrator.h"
#include "flux/Optix/OptixAccumulationBuffer.h"
#include "flux/Optix/OptixHandler.h"
#include "flux/Sampling/Sampler.h"
#include "flux/Scene/Camera.h"
#include "flux/Scene/Context.h"
#include "flux/Scene/EnvMapLight.h"
#include "flux/Scene/Geometry.h"
#include "flux/Scene/Light.h"
#include "flux/Scene/Primitive.h"
#include "flux/Scene/TriangleMesh.h"
#include "flux/Shading/BSDF.h"
#include "flux/Shading/EDF.h"
#include "flux/Shading/Texture.h"
#include "kira/Anyhow.h"

#ifndef FLUX_TEST_OPTIX_IR
#error "FLUX_TEST_OPTIX_IR must name the test OptiX IR module"
#endif

#ifndef FLUX_TEST_OUTPUT_DIR
#error "FLUX_TEST_OUTPUT_DIR must name the test output directory"
#endif

#ifndef FLUX_TEST_FIXTURES_DIR
#error "FLUX_TEST_FIXTURES_DIR must name the test fixtures directory"
#endif

namespace {
using flux::Spectrum;
using flux::Vec3f;
using Xf = std::array<float, 12>;
using Rotation = std::array<float, 9>;

constexpr std::uint32_t imageSize = 8;
constexpr float pi = std::numbers::pi_v<float>;

/// An image file a texture may read, relative to the fixtures, and its color components.
struct ImageFile {
    char const *path;
    std::size_t components;
};

/// Image files a texture may read. Two textures of one file and color space share an image.
constexpr std::array imageFiles{
    ImageFile{"Texture2x2.ppm", 3},
    ImageFile{"Texture3x3.ppm", 3},
    ImageFile{"SRGB.ppm", 3},
    ImageFile{"SRGBFilter2x2.ppm", 3},
    ImageFile{"Shared/Textures/Checker.png", 3},
    ImageFile{"Gray2x2.pgm", 1},
};

/// Address modes a texture may use. Texture coordinates reach outside [0, 1].
constexpr std::array addressModes{"wrap", "clamp", "mirror", "border"};

/// The kind of a context object the rig created.
enum class Kind : std::uint8_t {
    Primitive,
    Mesh,
    BSDF,
    EDF,
    Point,
    Env,
    Integrator,
    Sampler,
    ImageTexture,
    ConstantTexture,
};

/// Returns a row-major affine transform: rotation about Z, scale, optional flip about X, and
/// translation \p t. A flipped primitive faces down.
[[nodiscard]] Xf makeTransform(float scale, float angle, bool flip, Vec3f const &t) {
    auto const c = std::cos(angle) * scale;
    auto const s = std::sin(angle) * scale;
    if (flip)
        return {c, s, 0.0F, t.x(), s, -c, 0.0F, t.y(), 0.0F, 0.0F, -scale, t.z()};
    return {c, -s, 0.0F, t.x(), s, c, 0.0F, t.y(), 0.0F, 0.0F, scale, t.z()};
}

[[nodiscard]] Rotation makeRotation(Vec3f const &angles) {
    using Eigen::AngleAxisf;
    using Eigen::Vector3f;
    auto const product =
        (AngleAxisf{angles.x(), Vector3f::UnitX()} * AngleAxisf{angles.y(), Vector3f::UnitY()} *
         AngleAxisf{angles.z(), Vector3f::UnitZ()})
            .toRotationMatrix();

    Rotation result{};
    Eigen::Map<Eigen::Matrix<float, 3, 3, Eigen::RowMajor>>{result.data()} = product;
    return result;
}

[[nodiscard]] std::string text(Xf const &xf) {
    return std::format(
        "[{:.2f} {:.2f} {:.2f} {:.2f} | {:.2f} {:.2f} {:.2f} {:.2f} | {:.2f} {:.2f} {:.2f} {:.2f}]",
        xf[0], xf[1], xf[2], xf[3], xf[4], xf[5], xf[6], xf[7], xf[8], xf[9], xf[10], xf[11]
    );
}

[[nodiscard]] std::string text(Vec3f const &v) {
    return std::format("({:.2f} {:.2f} {:.2f})", v.x(), v.y(), v.z());
}

[[nodiscard]] std::string text(Rotation const &r) {
    return std::format(
        "[{:.2f} {:.2f} {:.2f} | {:.2f} {:.2f} {:.2f} | {:.2f} {:.2f} {:.2f}]", r[0], r[1], r[2],
        r[3], r[4], r[5], r[6], r[7], r[8]
    );
}

// ---------------------------------------------------------------------------------------------
// The scene model.

struct Spec {
    struct Mesh {
        std::array<Vec3f, 3> vertices;
    };

    /// An image texture: a file of \c imageFiles, its color space, and how it is read.
    struct Image {
        std::size_t file{};
        bool srgb{};
        bool point{};
        std::size_t addressMode{};
        /// Component mapping, such as "zyx1", or empty for the image's default.
        std::string mapping;
        /// Texture scale, or empty for one.
        std::optional<flux::Vec4f> scale;
        /// Texture bias, or empty for zero.
        std::optional<flux::Vec4f> bias;
    };

    /// An environment map with a constant color or an image, scaled by \c scale and rotated by
    /// \c rotation.
    struct Env {
        Spectrum scale;
        std::optional<Image> image;
        Rotation rotation{1.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 0.0F, 1.0F};
    };

    /// A diffuse or principled BSDF, whose color is \c color or, when set, \c image.
    struct BSDF {
        bool principled{};
        Spectrum color;
        float roughness{};
        std::optional<Image> image;
    };

    struct Prim {
        int mesh{};
        std::optional<int> bsdf;
        std::optional<int> edf;
        Xf transform{};
        bool visible{true};
    };

    struct Point {
        Vec3f position;
        Spectrum intensity;
    };

    struct Integrator {
        std::uint32_t maxDepth{4};
        bool reorder{false};
    };

    /// Context IDs of the objects of a built scene, by model key.
    struct Ids {
        std::map<int, std::size_t> prims, meshes, bsdfs, edfs;
        std::optional<std::size_t> point, env;
        std::size_t integrator{}, sampler{};
    };

    std::map<int, Mesh> meshes;
    std::map<int, BSDF> bsdfs;
    std::map<int, Spectrum> edfs;

    /// Primitives in creation order, which is Context ID order.
    std::vector<std::pair<int, Prim>> prims;
    std::optional<Point> point;

    /// The active environment map, or nothing.
    std::optional<Env> env;
    Integrator integrator;
    bool sobol{false};
    int nextKey{0};

    [[nodiscard]] Prim &prim(int key) {
        for (auto &entry : prims)
            if (entry.first == key)
                return entry.second;
        throw std::logic_error("model: unknown primitive");
    }

    /// Drops the meshes, BSDFs and EDFs that no primitive references, as collectGarbage does.
    void collect() {
        std::set<int> usedMeshes, usedBSDFs, usedEDFs;
        for (auto const &[key, prim] : prims) {
            usedMeshes.insert(prim.mesh);
            if (prim.bsdf)
                usedBSDFs.insert(*prim.bsdf);
            if (prim.edf)
                usedEDFs.insert(*prim.edf);
        }
        std::erase_if(meshes, [&](auto const &e) { return !usedMeshes.contains(e.first); });
        std::erase_if(bsdfs, [&](auto const &e) { return !usedBSDFs.contains(e.first); });
        std::erase_if(edfs, [&](auto const &e) { return !usedEDFs.contains(e.first); });
    }

    /// Creates the scene in \p context and makes its integrator, sampler and map active.
    Ids build(flux::Context &context) const;
};

[[nodiscard]] flux::Ref<flux::TriangleMesh>
createMesh(flux::Context &context, Spec::Mesh const &mesh) {
    return context.create<flux::TriangleMesh>(flux::TriangleMesh::Data{
        .vertices = flux::test::sharedBuffer(mesh.vertices[0], mesh.vertices[1], mesh.vertices[2]),
        .triangles = flux::test::sharedBuffer(flux::Vec3u{0, 1, 2}),
        .texCoords = flux::test::sharedBuffer(
            flux::Vec2f{-0.5F, -0.5F}, flux::Vec2f{1.5F, -0.5F}, flux::Vec2f{-0.5F, 1.5F}
        ),
    });
}

[[nodiscard]] std::string text(Spec::Image const &image) {
    return std::format(
        "{} {} {} {} {}{}{}", imageFiles[image.file].path, image.srgb ? "srgb" : "linear",
        image.point ? "point" : "linear", addressModes[image.addressMode],
        image.mapping.empty() ? "default" : image.mapping,
        image.scale ? std::format(
                          " scale ({:.2f} {:.2f} {:.2f} {:.2f})", image.scale->x(),
                          image.scale->y(), image.scale->z(), image.scale->w()
                      )
                    : "",
        image.bias ? std::format(
                         " bias ({:.2f} {:.2f} {:.2f} {:.2f})", image.bias->x(), image.bias->y(),
                         image.bias->z(), image.bias->w()
                     )
                   : ""
    );
}

/// Returns the properties of an ImageTexture that reads \p image.
[[nodiscard]] kira::Properties imageTextureProperties(Spec::Image const &image) {
    kira::Properties props;
    props.set("path", std::filesystem::path(FLUX_TEST_FIXTURES_DIR) / imageFiles[image.file].path);
    props.set("color_space", std::string(image.srgb ? "srgb" : "linear"));
    props.set("filter_mode", std::string(image.point ? "point" : "linear"));
    props.set("address_mode", std::string(addressModes[image.addressMode]));
    if (!image.mapping.empty())
        props.set("component_mapping", image.mapping);
    if (image.scale)
        props.set("scale", *image.scale);
    if (image.bias)
        props.set("bias", *image.bias);
    return props;
}

/// Returns the properties of an inline texture that reads \p image.
[[nodiscard]] kira::Properties textureProperties(Spec::Image const &image) {
    auto props = imageTextureProperties(image);
    props.set("type", "image");
    return props;
}

[[nodiscard]] flux::Ref<flux::BSDF> createBSDF(flux::Context &context, Spec::BSDF const &bsdf) {
    kira::Properties props;
    auto const colorName = bsdf.principled ? "base_color" : "R";
    if (bsdf.image)
        props.set(colorName, textureProperties(*bsdf.image));
    else
        props.set(colorName, bsdf.color);
    if (bsdf.principled) {
        props.set("type", "principled");
        props.set("roughness", bsdf.roughness);
    } else {
        props.set("type", "diffuse");
    }
    return context.create<flux::BSDF>(props);
}

[[nodiscard]] flux::Ref<flux::ConstantEDF>
createEDF(flux::Context &context, Spectrum const &radiance) {
    kira::Properties props;
    props.set("radiance", radiance);
    return context.create<flux::ConstantEDF>(props);
}

[[nodiscard]] flux::Ref<flux::Primitive> createPrimitive(
    flux::Context &context, std::size_t mesh, std::optional<std::size_t> bsdf,
    std::optional<std::size_t> edf, Xf const &transform, bool visible
) {
    kira::Properties props;
    props.set("geometry_ctx_id", static_cast<std::int64_t>(mesh));
    if (bsdf)
        props.set("bsdf_ctx_id", static_cast<std::int64_t>(*bsdf));
    if (edf)
        props.set("edf_ctx_id", static_cast<std::int64_t>(*edf));
    auto primitive = context.create<flux::Primitive>(props);
    primitive->setTransform(transform);
    primitive->setVisible(visible);
    return primitive;
}

[[nodiscard]] flux::Ref<flux::PointLight>
createPoint(flux::Context &context, Spec::Point const &point) {
    kira::Properties props;
    props.set("position", point.position);
    props.set("intensity", point.intensity);
    return context.create<flux::PointLight>(props);
}

[[nodiscard]] flux::Ref<flux::EnvMapLight> createEnv(flux::Context &context, Spec::Env const &env) {
    kira::Properties props;
    props.set("scale", env.scale);
    if (env.image)
        props.set("texture", textureProperties(*env.image));
    auto light = context.create<flux::EnvMapLight>(props);
    light->setRotation(env.rotation);
    return light;
}

[[nodiscard]] flux::Ref<flux::PathIntegrator>
createIntegrator(flux::Context &context, Spec::Integrator const &integrator) {
    kira::Properties props;
    props.set("max_depth", integrator.maxDepth);
    props.set("shader_reorder", integrator.reorder);
    return context.create<flux::PathIntegrator>(props);
}

[[nodiscard]] flux::Ref<flux::Sampler> createSampler(flux::Context &context, bool sobol) {
    if (sobol)
        return context.create<flux::SobolSampler>(kira::Properties{});
    return context.create<flux::IndependentSampler>(kira::Properties{});
}

Spec::Ids Spec::build(flux::Context &context) const {
    Ids ids;
    auto active = createIntegrator(context, integrator);
    ids.integrator = active->getContextId();
    context.setActiveIntegrator(active);
    auto sampler = createSampler(context, sobol);
    ids.sampler = sampler->getContextId();
    context.setActiveSampler(sampler);

    for (auto const &[key, mesh] : meshes)
        ids.meshes[key] = createMesh(context, mesh)->getContextId();
    for (auto const &[key, bsdf] : bsdfs)
        ids.bsdfs[key] = createBSDF(context, bsdf)->getContextId();
    for (auto const &[key, radiance] : edfs)
        ids.edfs[key] = createEDF(context, radiance)->getContextId();
    for (auto const &[key, prim] : prims) {
        std::optional<std::size_t> bsdf, edf;
        if (prim.bsdf)
            bsdf = ids.bsdfs.at(*prim.bsdf);
        if (prim.edf)
            edf = ids.edfs.at(*prim.edf);
        ids.prims[key] =
            createPrimitive(
                context, ids.meshes.at(prim.mesh), bsdf, edf, prim.transform, prim.visible
            )
                ->getContextId();
    }
    if (point)
        ids.point = createPoint(context, *point)->getContextId();
    if (env) {
        auto map = createEnv(context, *env);
        ids.env = map->getContextId();
        context.setActiveEnvMap(map);
    }
    return ids;
}

/// Keys of the base scene.
struct Base {
    Spec spec;
    int floor{}, light{}, occluder{}, pillar{};
    int meshTri{}, meshFloor{}, meshOccluder{}, meshPillar{};
    int gray{}, checker{}, green{}, red{};
    int lamp{};
};

[[nodiscard]] Base makeBase() {
    Base b;
    auto &s = b.spec;
    auto const tri = Spec::Mesh{{Vec3f{0, 0, 0}, Vec3f{1, 0, 0}, Vec3f{0, 1, 0}}};
    b.meshTri = s.nextKey++;
    s.meshes[b.meshTri] = tri;
    b.meshFloor = s.nextKey++;
    s.meshes[b.meshFloor] = {{Vec3f{-3, -3, 0}, Vec3f{5, -3, 0}, Vec3f{-3, 5, 0}}};
    b.meshOccluder = s.nextKey++;
    s.meshes[b.meshOccluder] = {{Vec3f{0, 0, 0}, Vec3f{1, 0.1F, 0}, Vec3f{0.2F, 1, 0}}};
    b.meshPillar = s.nextKey++;
    s.meshes[b.meshPillar] = {{Vec3f{0, 0, 0}, Vec3f{1, 0, 0.1F}, Vec3f{0, 1, 0.2F}}};
    b.gray = s.nextKey++;
    s.bsdfs[b.gray] = {false, Spectrum{0.5F}, 0.0F};
    b.checker = s.nextKey++;
    s.bsdfs[b.checker] = {false, Spectrum{0.5F}, 0.0F, Spec::Image{0, false, true, 0}};
    b.green = s.nextKey++;
    s.bsdfs[b.green] = {false, Spectrum{0.2F, 0.7F, 0.3F}, 0.0F};
    b.red = s.nextKey++;
    s.bsdfs[b.red] = {true, Spectrum{0.8F, 0.2F, 0.2F}, 0.4F};
    b.lamp = s.nextKey++;
    s.edfs[b.lamp] = Spectrum{4.0F};

    b.floor = s.nextKey++;
    s.prims.push_back(
        {b.floor,
         {b.meshFloor, b.checker, std::nullopt, makeTransform(1, 0, false, {0, 0, 0}), true}}
    );
    b.light = s.nextKey++;
    s.prims.push_back(
        {b.light,
         {b.meshTri, b.gray, b.lamp, makeTransform(0.8F, 0.0F, true, {-0.4F, -0.4F, 0.8F}), true}}
    );
    b.occluder = s.nextKey++;
    s.prims.push_back(
        {b.occluder,
         {b.meshOccluder, b.green, std::nullopt,
          makeTransform(0.6F, 0.3F, false, {0.1F, 0.1F, 0.4F}), true}}
    );
    b.pillar = s.nextKey++;
    s.prims.push_back(
        {b.pillar,
         {b.meshPillar, b.red, std::nullopt, makeTransform(0.5F, 1.0F, false, {0.5F, -0.1F, 0.2F}),
          true}}
    );
    s.point = Spec::Point{Vec3f{0.5F, 0.5F, 0.9F}, Spectrum{2.0F}};
    s.integrator = {4, false};
    return b;
}

// ---------------------------------------------------------------------------------------------
// Rendering and comparison.

/// All film channels of one render, as floats.
struct Image {
    std::vector<float> values;

    [[nodiscard]] bool operator==(Image const &other) const {
        return values.size() == other.values.size() &&
               std::memcmp(values.data(), other.values.data(), values.size() * sizeof(float)) == 0;
    }
};

[[nodiscard]] flux::Ref<flux::Camera> makeCamera() {
    kira::Properties cameraProps;
    cameraProps.set("position", Vec3f{0.25F, 0.25F, 1.6F});
    cameraProps.set("look_at", Vec3f{0.25F, 0.25F, 0.0F});
    cameraProps.set("fov", 60.0F);
    return flux::Camera::create(cameraProps);
}

[[nodiscard]] std::unique_ptr<flux::OptixAccumulationBuffer>
makeBuffer(flux::OptixHandler &handler) {
    return std::make_unique<flux::OptixAccumulationBuffer>(
        handler, flux::FilmSpec{flux::Vec2u{imageSize, imageSize}, flux::FilmChannels::All}
    );
}

/// Clears \p buffer, then renders one sample per pixel into it.
/// Returns every channel of the result.
[[nodiscard]] Image renderImage(
    flux::OptixHandler &handler, flux::Camera const &camera, flux::OptixAccumulationBuffer &buffer
) {
    buffer.clear();
    handler.render(camera, buffer, 1);
    flux::Film film{flux::FilmSpec{flux::Vec2u{imageSize, imageSize}, flux::FilmChannels::All}};
    buffer.download(film);
    Image image;
    for (auto const &c : film.getChannel<flux::ColorChannel>())
        image.values.insert(image.values.end(), {c.x(), c.y(), c.z()});
    for (auto const &c : film.getChannel<flux::NormalChannel>())
        image.values.insert(image.values.end(), {c.x(), c.y(), c.z()});
    for (auto const &c : film.getChannel<flux::AlbedoChannel>())
        image.values.insert(image.values.end(), {c.x(), c.y(), c.z()});
    return image;
}

[[nodiscard]] std::string describeMismatch(Image const &live, Image const &fresh) {
    if (live.values.size() != fresh.values.size())
        return "image sizes differ";
    std::size_t count = 0, first = live.values.size();
    for (std::size_t i = 0; i < live.values.size(); ++i)
        if (std::memcmp(&live.values[i], &fresh.values[i], sizeof(float)) != 0) {
            ++count;
            first = std::min(first, i);
        }
    auto const channelSize = std::size_t{imageSize} * imageSize * 3;
    auto const pixel = (first % channelSize) / 3;
    return std::format(
        "{} of {} floats differ; first: channel {} pixel ({}, {}) component {}: live {} fresh {}",
        count, live.values.size(), first / channelSize, pixel % imageSize, pixel / imageSize,
        first % 3, live.values[first], fresh.values[first]
    );
}

// ---------------------------------------------------------------------------------------------
// The rig: a live context, and the model it must match.

class Rig {
public:
    explicit Rig(Spec initial, std::filesystem::path modulePath = FLUX_TEST_OPTIX_IR)
        : model(std::move(initial)), context(flux::Context::create()), camera(makeCamera()) {
        ids = model.build(*context);
        if (model.env)
            envImage = model.env->image;
        track();
        handler = std::make_unique<flux::OptixHandler>(context, modulePath);
        buffer = makeBuffer(*handler);

        // Start in the host's steady state: synced and cleared.
        context->clearDirty();
    }

    /// Replaces the handler with a new one on the same context, as a host does after a failure.
    void restartHandler(std::filesystem::path modulePath = FLUX_TEST_OPTIX_IR) {
        note("restart the handler");
        buffer.reset();
        handler = std::make_unique<flux::OptixHandler>(context, modulePath);
        buffer = makeBuffer(*handler);
    }

    Spec model;
    flux::Ref<flux::Context> context;
    Spec::Ids ids;
    std::unique_ptr<flux::OptixHandler> handler;
    flux::Ref<flux::Camera> camera;

    /// Destroyed before \c handler.
    std::unique_ptr<flux::OptixAccumulationBuffer> buffer;
    std::vector<std::string> log;
    bool ok{true};

public:
    // Edits. Each one applies to the context and to the model.

    void move(int key, Xf const &transform) {
        note("move prim {} to {}", key, text(transform));
        prim(key)->setTransform(transform);
        model.prim(key).transform = transform;
    }

    /// Assigns the current transform again, which must change nothing.
    void reassign(int key) {
        note("reassign the transform of prim {}", key);
        auto const version = context->getVersion();
        prim(key)->setTransform(model.prim(key).transform);
        if (context->getVersion() != version)
            report(std::format("reassigning the transform of prim {} changed the version", key));
    }

    void rebindBSDF(int key, std::optional<int> bsdf) {
        note("rebind the BSDF of prim {} to {}", key, bsdf ? std::to_string(*bsdf) : "none");
        prim(key)->setBSDF(bsdf ? context->get<flux::BSDF>(ids.bsdfs.at(*bsdf)) : nullptr);
        model.prim(key).bsdf = bsdf;
    }

    void rebindEDF(int key, std::optional<int> edf) {
        note("rebind the EDF of prim {} to {}", key, edf ? std::to_string(*edf) : "none");
        prim(key)->setEDF(edf ? context->get<flux::EDF>(ids.edfs.at(*edf)) : nullptr);
        model.prim(key).edf = edf;
    }

    void setRadiance(int edf, Spectrum const &radiance) {
        note("set the radiance of EDF {} to {}", edf, text(radiance));
        context->get<flux::ConstantEDF>(ids.edfs.at(edf))->setRadiance(radiance);
        model.edfs.at(edf) = radiance;
    }

    void setVisible(int key, bool visible) {
        note("{} prim {}", visible ? "show" : "hide", key);
        prim(key)->setVisible(visible);
        model.prim(key).visible = visible;
    }

    void removePrimitive(int key) {
        note("remove prim {}", key);
        context->remove(ids.prims.at(key));
        ids.prims.erase(key);
        std::erase_if(model.prims, [&](auto const &e) { return e.first == key; });
    }

    int addMesh(Spec::Mesh const &mesh) {
        auto const key = model.nextKey++;
        note("add mesh {} {} {}", key, text(mesh.vertices[0]), text(mesh.vertices[1]));
        ids.meshes[key] = enroll(Kind::Mesh, *createMesh(*context, mesh));
        model.meshes[key] = mesh;
        return key;
    }

    int addBSDF(Spec::BSDF const &bsdf) {
        auto const key = model.nextKey++;
        note(
            "add {} BSDF {} {}", bsdf.principled ? "principled" : "diffuse", key,
            bsdf.image ? text(*bsdf.image) : text(bsdf.color)
        );
        ids.bsdfs[key] = enroll(Kind::BSDF, *createBSDF(*context, bsdf));
        model.bsdfs[key] = bsdf;
        return key;
    }

    int addEDF(Spectrum const &radiance) {
        auto const key = model.nextKey++;
        note("add EDF {} {}", key, text(radiance));
        ids.edfs[key] = enroll(Kind::EDF, *createEDF(*context, radiance));
        model.edfs[key] = radiance;
        return key;
    }

    /// Adds a primitive over existing meshes, BSDFs and EDFs. It is created last.
    int addPrimitive(
        int mesh, std::optional<int> bsdf, std::optional<int> edf, Xf const &transform,
        bool visible = true
    ) {
        auto const key = model.nextKey++;
        note(
            "add prim {} (mesh {}, bsdf {}, edf {}) at {}{}", key, mesh,
            bsdf ? std::to_string(*bsdf) : "none", edf ? std::to_string(*edf) : "none",
            text(transform), visible ? "" : " hidden"
        );
        std::optional<std::size_t> bsdfId, edfId;
        if (bsdf)
            bsdfId = ids.bsdfs.at(*bsdf);
        if (edf)
            edfId = ids.edfs.at(*edf);
        ids.prims[key] = enroll(
            Kind::Primitive,
            *createPrimitive(*context, ids.meshes.at(mesh), bsdfId, edfId, transform, visible)
        );
        model.prims.push_back({key, {mesh, bsdf, edf, transform, visible}});
        return key;
    }

    /// Replaces the mesh of a primitive with a new one. The old mesh is collected at the frame.
    void replaceMesh(int key, Spec::Mesh const &mesh) {
        auto const meshKey = addMesh(mesh);
        note("bind mesh {} to prim {}", meshKey, key);
        prim(key)->setGeometry(context->get<flux::Geometry>(ids.meshes.at(meshKey)));
        model.prim(key).mesh = meshKey;
    }

    void movePoint(Vec3f const &position) {
        note("move the point light to {}", text(position));
        context->get<flux::PointLight>(*ids.point)->setPosition(position);
        model.point->position = position;
    }

    void setIntensity(Spectrum const &intensity) {
        note("set the point intensity to {}", text(intensity));
        context->get<flux::PointLight>(*ids.point)->setIntensity(intensity);
        model.point->intensity = intensity;
    }

    void addPoint(Spec::Point const &point) {
        note("add a point light at {}", text(point.position));
        ids.point = enroll(Kind::Point, *createPoint(*context, point));
        model.point = point;
    }

    void removePoint() {
        note("remove the point light");
        context->remove(*ids.point);
        ids.point.reset();
        model.point.reset();
    }

    /// Makes an environment map with \p scale active. A parked map is reused with its image.
    /// Otherwise a new map reads \p image, or no image.
    void setEnv(Spectrum const &scale, std::optional<Spec::Image> const &image = std::nullopt) {
        flux::Ref<flux::EnvMapLight> map;
        if (ids.env) {
            note("activate the parked environment map with scale {}", text(scale));
            map = context->get<flux::EnvMapLight>(*ids.env);
            map->setScale(scale);
        } else {
            note(
                "activate a new environment map with scale {} and {}", text(scale),
                image ? text(*image) : "no image"
            );
            envImage = image;
            map = createEnv(*context, {scale, envImage});
            ids.env = enroll(Kind::Env, *map);
        }
        context->setActiveEnvMap(map);
        model.env = Spec::Env{scale, envImage, map->getRotation()};
    }

    void scaleEnv(Spectrum const &scale) {
        note("set the environment scale to {}", text(scale));
        context->get<flux::EnvMapLight>(*ids.env)->setScale(scale);
        model.env->scale = scale;
    }

    /// Gives the active environment map a new texture that reads \p image, or a constant one.
    void retextureEnv(std::optional<Spec::Image> const &image) {
        note("retexture the environment map with {}", image ? text(*image) : "no image");
        flux::Ref<flux::Texture const> texture;
        if (image) {
            texture = context->create<flux::ImageTexture>(imageTextureProperties(*image));
        } else {
            kira::Properties props;
            props.set("value", Spectrum{1.0F});
            texture = context->create<flux::ConstantTexture>(props);
        }
        trackTextures();
        context->get<flux::EnvMapLight>(*ids.env)->setTexture(texture);
        envImage = image;
        model.env->image = image;
    }

    void rotateEnv(Rotation const &rotation) {
        note("set the environment rotation to {}", text(rotation));
        context->get<flux::EnvMapLight>(*ids.env)->setRotation(rotation);
        model.env->rotation = rotation;
    }

    /// Makes the environment map inactive, and removes its object when \p remove is set.
    void unsetEnv(bool remove) {
        note("deactivate the environment map{}", remove ? " and remove it" : "");
        context->setActiveEnvMap({});
        if (remove) {
            context->remove(*ids.env);
            ids.env.reset();
            envImage.reset();
        }
        model.env.reset();
    }

    void switchIntegrator(Spec::Integrator const &integrator, bool removeOld) {
        note(
            "switch the integrator to depth {} reorder {}{}", integrator.maxDepth,
            integrator.reorder, removeOld ? ", remove the old one" : ""
        );
        auto const object = createIntegrator(*context, integrator);
        context->setActiveIntegrator(object);
        if (removeOld)
            context->remove(ids.integrator);
        ids.integrator = enroll(Kind::Integrator, *object);
        model.integrator = integrator;
    }

    void switchSampler(bool sobol, bool removeOld) {
        note(
            "switch the sampler to {}{}", sobol ? "sobol" : "independent",
            removeOld ? ", remove the old one" : ""
        );
        auto const object = createSampler(*context, sobol);
        context->setActiveSampler(object);
        if (removeOld)
            context->remove(ids.sampler);
        ids.sampler = enroll(Kind::Sampler, *object);
        model.sobol = sobol;
    }

public:
    // Control.

    /// Syncs without a clear, so the tracked changes keep the synced edits and gain later ones.
    void syncOnly() {
        note("sync without clearDirty");
        context->collectGarbage();
        model.collect();
        prune();
        checkObjects();
        handler->sync();
    }

    /// Clears the tracked changes without a sync. A runtime must not depend on them.
    void clearOnly() {
        note("clearDirty without a sync");
        clearAndCheck();
    }

    /// Collects, syncs \p syncs times, checks the tracked changes, and clears.
    void settle(int syncs = 1) {
        note("frame with {} sync{}", syncs, syncs == 1 ? "" : "s");
        context->collectGarbage();
        model.collect();
        prune();
        checkObjects();
        for (int i = 0; i < syncs; ++i)
            handler->sync();
        checkChanges();
        clearAndCheck();
    }

    /// Renders the live side and a fresh side built from the model, and requires them to match.
    ///
    /// The live buffer is cleared first, so its batch is sample 0, like the fresh one.
    Image compare() {
        auto live = renderImage(*handler, *camera, *buffer);

        auto fresh = flux::Context::create();
        (void)model.build(*fresh);
        flux::OptixHandler freshHandler(fresh, std::filesystem::path(FLUX_TEST_OPTIX_IR));
        auto freshBuffer = makeBuffer(freshHandler);
        auto const expected = renderImage(freshHandler, *makeCamera(), *freshBuffer);
        if (!(live == expected))
            report(
                "the live image differs from a fresh context: " + describeMismatch(live, expected)
            );
        return live;
    }

    /// Settles, then compares.
    Image frame(int syncs = 1) {
        settle(syncs);
        return compare();
    }

    void beginStep(int index) { log.push_back(std::format("-- step {}", index)); }

    void report(std::string const &what) {
        if (ok) {
            std::string all;
            for (auto const &line : log)
                all += "  " + line + "\n";
            ADD_FAILURE() << what << "\noperations so far:\n" << all;
        } else {
            ADD_FAILURE() << what;
        }
        ok = false;
    }

private:
    template <typename... Args> void note(std::format_string<Args...> format, Args &&...args) {
        log.push_back(std::format(format, std::forward<Args>(args)...));
    }

    [[nodiscard]] flux::Ref<flux::Primitive> prim(int key) {
        return context->get<flux::Primitive>(ids.prims.at(key));
    }

    /// Records the kinds of the objects of the built model.
    void track() {
        for (auto const &[key, id] : ids.prims)
            kinds[id] = Kind::Primitive;
        for (auto const &[key, id] : ids.meshes)
            kinds[id] = Kind::Mesh;
        for (auto const &[key, id] : ids.bsdfs)
            kinds[id] = Kind::BSDF;
        for (auto const &[key, id] : ids.edfs)
            kinds[id] = Kind::EDF;
        if (ids.point)
            kinds[*ids.point] = Kind::Point;
        if (ids.env)
            kinds[*ids.env] = Kind::Env;
        kinds[ids.integrator] = Kind::Integrator;
        kinds[ids.sampler] = Kind::Sampler;
        trackTextures();
    }

    /// Records the kind of \p object, and of the image textures created with it, and returns
    /// its ID.
    std::size_t enroll(Kind kind, flux::ContextObject const &object) {
        kinds[object.getContextId()] = kind;
        trackTextures();
        return object.getContextId();
    }

    /// Records every texture that has no kind yet. BSDFs and maps create their own.
    void trackTextures() {
        for (auto const &texture : context->getObjects<flux::ImageTexture>())
            kinds.try_emplace(texture->getContextId(), Kind::ImageTexture);
        for (auto const &texture : context->getObjects<flux::ConstantTexture>())
            kinds.try_emplace(texture->getContextId(), Kind::ConstantTexture);
    }

    /// Forgets the keys that the model collected.
    void prune() {
        std::erase_if(ids.meshes, [&](auto const &e) { return !model.meshes.contains(e.first); });
        std::erase_if(ids.bsdfs, [&](auto const &e) { return !model.bsdfs.contains(e.first); });
        std::erase_if(ids.edfs, [&](auto const &e) { return !model.edfs.contains(e.first); });
    }

    template <typename T, typename Map> void checkSet(char const *name, Map const &map) {
        std::set<std::size_t> live, expected;
        for (auto const &object : context->getObjects<T>())
            live.insert(object->getContextId());
        for (auto const &[key, id] : map)
            expected.insert(id);
        if (live != expected)
            report(
                std::format(
                    "the context holds {} {} objects, the model {}", live.size(), name,
                    expected.size()
                )
            );
    }

    /// Requires the context to hold exactly the objects of the model.
    void checkObjects() {
        checkSet<flux::Primitive>("primitive", ids.prims);
        checkSet<flux::Geometry>("geometry", ids.meshes);
        checkSet<flux::BSDF>("BSDF", ids.bsdfs);
        checkSet<flux::EDF>("EDF", ids.edfs);

        // Each textured BSDF and environment map owns one image texture, which leaves with it.
        // Textures of one file and color space share one image.
        std::vector<Spec::Image const *> images;
        for (auto const &[key, bsdf] : model.bsdfs)
            if (bsdf.image)
                images.push_back(&*bsdf.image);
        if (envImage)
            images.push_back(&*envImage);
        auto const textures = context->getObjects<flux::ImageTexture>().size();
        if (textures != images.size())
            report(
                std::format(
                    "the context holds {} image textures, the model {}", textures, images.size()
                )
            );
        std::set<std::pair<std::size_t, bool>> files;
        for (auto const *image : images)
            files.emplace(image->file, image->srgb);
        auto const assets = context->getImageAssetPool().size();
        if (assets != files.size())
            report(std::format("the context holds {} images, the model {}", assets, files.size()));
    }

    [[nodiscard]] bool inScene(std::size_t id) const {
        try {
            (void)context->get<flux::ContextObject>(id);
            return true;
        } catch (std::out_of_range const &) { return false; }
    }

    [[nodiscard]] static bool hasIndex(flux::Context const &context, std::size_t id) {
        return context.findIndex<flux::Primitive>(id) || context.findIndex<flux::Geometry>(id) ||
               context.findIndex<flux::BSDF>(id) || context.findIndex<flux::EDF>(id) ||
               context.findIndex<flux::ImageTexture>(id);
    }

    /// Checks the tracked changes before the clear.
    void checkChanges() {
        for (auto const id : context->getAddedIds())
            if (!inScene(id))
                report(std::format("added id {} is not in the scene", id));
        for (auto const id : context->getDirtiedIds())
            if (!inScene(id))
                report(std::format("dirtied id {} is not in the scene", id));
        for (auto const id : context->getRemovedIds()) {
            if (inScene(id))
                report(std::format("removed id {} is still in the scene", id));
            if (context->getAddedIds().contains(id) || context->getDirtiedIds().contains(id))
                report(std::format("removed id {} is also added or dirtied", id));

            // An indexed object keeps its index until the clear, and other objects have none.
            auto const iterator = kinds.find(id);
            if (iterator == kinds.end()) {
                report(std::format("removed id {} was never created by the rig", id));
                continue;
            }
            auto const kept = [&]() -> std::optional<bool> {
                switch (iterator->second) {
                case Kind::Primitive: return context->findIndex<flux::Primitive>(id).has_value();
                case Kind::Mesh: return context->findIndex<flux::Geometry>(id).has_value();
                case Kind::BSDF: return context->findIndex<flux::BSDF>(id).has_value();
                case Kind::EDF: return context->findIndex<flux::EDF>(id).has_value();
                case Kind::ImageTexture:
                    return context->findIndex<flux::ImageTexture>(id).has_value();
                case Kind::ConstantTexture:
                case Kind::Point:
                case Kind::Env:
                case Kind::Integrator:
                case Kind::Sampler: return std::nullopt;
                }
                return std::nullopt;
            }();
            if (kept && !*kept)
                report(std::format("removed id {} lost its index before the clear", id));
            if (!kept && hasIndex(*context, id))
                report(std::format("removed id {} of an unindexed kind has an index", id));
        }
    }

    void clearAndCheck() {
        auto const removed = context->getRemovedIds();
        context->clearDirty();
        if (context->getClearedVersion() != context->getVersion())
            report("clearDirty did not store the version");
        if (!context->getAddedIds().empty() || !context->getDirtiedIds().empty() ||
            !context->getRemovedIds().empty() ||
            context->getDirtyBits() != flux::Context::DirtyBits::None)
            report("clearDirty left changes behind");
        for (auto const id : removed)
            if (hasIndex(*context, id))
                report(std::format("removed id {} kept its index after the clear", id));
        for (auto const &[key, id] : ids.prims)
            if (flux::getDirtyBits(*prim(key)) != flux::Primitive::DirtyBits::None)
                report(std::format("prim {} is dirty after the clear", key));
    }

    std::map<std::size_t, Kind> kinds;

    /// Image of the environment map \c ids.env, active or parked.
    std::optional<Spec::Image> envImage;
};

[[nodiscard]] bool skipWithoutCuda() { return !flux::test::hasCudaMemoryPoolSupport(); }

// ---------------------------------------------------------------------------------------------
// The generator.

class Gen {
public:
    explicit Gen(std::uint32_t seed) : rng_(seed) {}

    [[nodiscard]] std::size_t below(std::size_t n) { return rng_() % n; }
    [[nodiscard]] bool chance(unsigned percent) { return below(100) < percent; }
    [[nodiscard]] float range(float lo, float hi) {
        auto const unit = static_cast<float>(rng_() >> 8U) / 16777216.0F;
        return lo + (hi - lo) * unit;
    }
    [[nodiscard]] Spectrum spectrum(float lo, float hi) {
        auto const r = range(lo, hi);
        auto const g = range(lo, hi);
        auto const b = range(lo, hi);
        return {r, g, b};
    }
    [[nodiscard]] Vec3f point(float xy0, float xy1, float z0, float z1) {
        auto const x = range(xy0, xy1);
        auto const y = range(xy0, xy1);
        auto const z = range(z0, z1);
        return {x, y, z};
    }
    [[nodiscard]] Xf transform() {
        auto const scale = range(0.4F, 1.4F);
        auto const angle = range(0.0F, 6.28F);
        auto const flip = chance(50);
        return makeTransform(scale, angle, flip, point(-0.6F, 1.1F, 0.1F, 1.0F));
    }
    [[nodiscard]] Spec::Mesh mesh() {
        return {
            {point(-0.3F, 1.0F, -0.05F, 0.05F), point(-0.3F, 1.0F, -0.05F, 0.05F),
             point(-0.3F, 1.0F, -0.05F, 0.05F)}
        };
    }
    [[nodiscard]] Spec::BSDF makeBSDF(unsigned texturePercent = 50) {
        auto const principled = chance(30);
        auto const color = spectrum(0.1F, 0.9F);
        auto const roughness = range(0.1F, 1.0F);
        std::optional<Spec::Image> image;
        if (chance(texturePercent))
            image = this->image();
        return {principled, color, roughness, image};
    }
    [[nodiscard]] Spec::Image image() {
        // Only a color image may be sRGB.
        auto const file = below(imageFiles.size());
        auto const srgb = imageFiles[file].components >= 3 && chance(50);
        auto const point = chance(30);
        auto const addressMode = below(addressModes.size());

        // Select only the components the file has, or a constant.
        std::string mapping;
        if (chance(40)) {
            auto const selectors = std::string("xyz").substr(0, imageFiles[file].components) + "01";
            for (int i = 0; i < 4; ++i)
                mapping += selectors[below(selectors.size())];
        }
        return {file, srgb, point, addressMode, mapping};
    }

    template <typename Map> [[nodiscard]] int key(Map const &map) {
        auto iterator = map.begin();
        std::advance(iterator, static_cast<std::ptrdiff_t>(below(map.size())));
        return iterator->first;
    }
    [[nodiscard]] int primKey(Spec const &model) {
        return model.prims[below(model.prims.size())].first;
    }

private:
    std::mt19937 rng_;
};

/// One edit with the condition under which it is valid for a model.
struct Op {
    char const *name;
    unsigned weight;
    bool (*valid)(Spec const &);
    void (*run)(Rig &, Gen &);
};

[[nodiscard]] bool anyVisible(Spec const &m, bool visible) {
    return std::ranges::any_of(m.prims, [&](auto const &e) { return e.second.visible == visible; });
}

/// Returns the keys of the primitives whose visibility is \p visible.
[[nodiscard]] std::vector<int> primKeys(Spec const &m, bool visible) {
    std::vector<int> keys;
    for (auto const &[key, prim] : m.prims)
        if (prim.visible == visible)
            keys.push_back(key);
    return keys;
}

[[nodiscard]] std::optional<int> pickOrAddBSDF(Rig &rig, Gen &gen) {
    // None, an existing BSDF, or a new one.
    auto const roll = gen.below(10);
    if (roll == 0)
        return std::nullopt;
    if (roll < 3 || rig.model.bsdfs.empty())
        return rig.addBSDF(gen.makeBSDF());
    return gen.key(rig.model.bsdfs);
}

[[nodiscard]] std::optional<int> pickOrAddEDF(Rig &rig, Gen &gen) {
    auto const roll = gen.below(10);
    if (roll < 4)
        return std::nullopt;
    if (roll < 7 || rig.model.edfs.empty())
        return rig.addEDF(gen.spectrum(0.5F, 6.0F));
    return gen.key(rig.model.edfs);
}

// clang-format off
const std::array ops = {
    Op{"move", 3, [](Spec const &m) { return !m.prims.empty(); },
       [](Rig &r, Gen &g) {
           auto const prim = g.primKey(r.model);
           r.move(prim, g.transform());
       }},
    Op{"reassign", 1, [](Spec const &m) { return !m.prims.empty(); },
       [](Rig &r, Gen &g) { r.reassign(g.primKey(r.model)); }},
    Op{"rebind BSDF", 2, [](Spec const &m) { return !m.prims.empty(); },
       [](Rig &r, Gen &g) {
           auto const prim = g.primKey(r.model);
           r.rebindBSDF(prim, pickOrAddBSDF(r, g));
       }},
    Op{"rebind EDF", 2, [](Spec const &m) { return !m.prims.empty(); },
       [](Rig &r, Gen &g) {
           auto const prim = g.primKey(r.model);
           r.rebindEDF(prim, pickOrAddEDF(r, g));
       }},
    Op{"retexture", 3, [](Spec const &m) { return !m.prims.empty(); },
       [](Rig &r, Gen &g) {
           // A new textured BSDF, so the old one's image may lose its last texture.
           auto const prim = g.primKey(r.model);
           r.rebindBSDF(prim, r.addBSDF(g.makeBSDF(100)));
       }},
    Op{"set radiance", 2, [](Spec const &m) { return !m.edfs.empty(); },
       [](Rig &r, Gen &g) {
           auto const edf = g.key(r.model.edfs);
           r.setRadiance(edf, g.spectrum(0.5F, 6.0F));
       }},
    Op{"hide", 2, [](Spec const &m) { return anyVisible(m, true); },
       [](Rig &r, Gen &g) {
           auto const keys = primKeys(r.model, true);
           r.setVisible(keys[g.below(keys.size())], false);
       }},
    Op{"show", 2, [](Spec const &m) { return anyVisible(m, false); },
       [](Rig &r, Gen &g) {
           auto const keys = primKeys(r.model, false);
           r.setVisible(keys[g.below(keys.size())], true);
       }},
    Op{"remove primitive", 1, [](Spec const &m) { return !m.prims.empty(); },
       [](Rig &r, Gen &g) { r.removePrimitive(g.primKey(r.model)); }},
    Op{"add primitive", 3, [](Spec const &) { return true; },
       [](Rig &r, Gen &g) {
           // A new mesh, or an instance of an existing one.
           auto const mesh = g.chance(75) || r.model.meshes.empty() ? r.addMesh(g.mesh())
                                                                    : g.key(r.model.meshes);
           auto const bsdf = pickOrAddBSDF(r, g);
           auto const edf = pickOrAddEDF(r, g);
           auto const transform = g.transform();
           auto const visible = g.chance(85);
           (void)r.addPrimitive(mesh, bsdf, edf, transform, visible);
       }},
    Op{"replace mesh", 2, [](Spec const &m) { return !m.prims.empty(); },
       [](Rig &r, Gen &g) {
           auto const prim = g.primKey(r.model);
           r.replaceMesh(prim, g.mesh());
       }},
    Op{"move point light", 1, [](Spec const &m) { return m.point.has_value(); },
       [](Rig &r, Gen &g) { r.movePoint(g.point(-0.5F, 1.2F, 0.2F, 1.2F)); }},
    Op{"set point intensity", 1, [](Spec const &m) { return m.point.has_value(); },
       [](Rig &r, Gen &g) { r.setIntensity(g.spectrum(0.5F, 4.0F)); }},
    Op{"add point light", 1, [](Spec const &m) { return !m.point; },
       [](Rig &r, Gen &g) {
           auto const position = g.point(-0.5F, 1.2F, 0.2F, 1.2F);
           r.addPoint({position, g.spectrum(0.5F, 4.0F)});
       }},
    Op{"remove point light", 1, [](Spec const &m) { return m.point.has_value(); },
       [](Rig &r, Gen &) { r.removePoint(); }},
    Op{"set environment map", 1, [](Spec const &m) { return !m.env; },
       [](Rig &r, Gen &g) {
           auto const scale = g.spectrum(0.05F, 1.0F);
           std::optional<Spec::Image> image;
           if (g.chance(50))
               image = g.image();
           r.setEnv(scale, image);
       }},
    Op{"scale environment map", 1, [](Spec const &m) { return m.env.has_value(); },
       [](Rig &r, Gen &g) { r.scaleEnv(g.spectrum(0.05F, 1.0F)); }},
    Op{"retexture environment map", 1, [](Spec const &m) { return m.env.has_value(); },
       [](Rig &r, Gen &g) {
           std::optional<Spec::Image> image;
           if (g.chance(50))
               image = g.image();
           r.retextureEnv(image);
       }},
    Op{"rotate environment map", 1, [](Spec const &m) { return m.env.has_value(); },
       [](Rig &r, Gen &g) { r.rotateEnv(makeRotation(g.point(-pi, pi, -pi, pi))); }},
    Op{"unset environment map", 1, [](Spec const &m) { return m.env.has_value(); },
       [](Rig &r, Gen &g) { r.unsetEnv(g.chance(50)); }},
    Op{"switch integrator", 1, [](Spec const &) { return true; },
       [](Rig &r, Gen &g) {
           constexpr std::array<std::uint32_t, 4> depths{1, 2, 4, 6};
           auto const depth = depths[g.below(depths.size())];
           auto const reorder = g.chance(50);
           r.switchIntegrator({depth, reorder}, g.chance(50));
       }},
    Op{"switch sampler", 1, [](Spec const &) { return true; },
       [](Rig &r, Gen &g) {
           auto const sobol = g.chance(50);
           r.switchSampler(sobol, g.chance(50));
       }},
};
// clang-format on

/// Applies one random valid edit.
void applyRandomOp(Rig &rig, Gen &gen) {
    unsigned total = 0;
    for (auto const &op : ops)
        if (op.valid(rig.model))
            total += op.weight;
    auto roll = static_cast<unsigned>(gen.below(total));
    for (auto const &op : ops) {
        if (!op.valid(rig.model))
            continue;
        if (roll < op.weight) {
            op.run(rig, gen);
            return;
        }
        roll -= op.weight;
    }
}

/// Runs one step: some edits, then a frame, a double-sync frame, a sync on top of a sync, or a
/// clear without a sync.
void runStep(Rig &rig, Gen &gen, int index) {
    rig.beginStep(index);
    auto const kind = gen.below(10);
    auto const edits = [&](std::size_t count) {
        for (std::size_t i = 0; i < count; ++i)
            applyRandomOp(rig, gen);
    };
    if (kind < 6) {
        edits(1 + gen.below(3));
        (void)rig.frame();
    } else if (kind == 6) {
        edits(1 + gen.below(2));
        (void)rig.frame(2);
    } else if (kind == 7 || kind == 8) {
        (void)rig.frame();
    } else {
        // Sometimes sync first, so the tracked changes keep the synced edits and gain later ones.
        auto const synced = gen.chance(50);
        if (synced)
            rig.syncOnly();
        edits(1 + gen.below(2));

        // Then either sync again on top of the last sync, or clear, which drops the edits made
        // after the sync.
        if (synced && gen.chance(50)) {
            (void)rig.frame();
            return;
        }
        rig.clearOnly();

        // Often sync right away, with no tracked changes since the clear.
        if (gen.chance(60))
            (void)rig.frame();
    }
}

/// Returns the number in the environment variable \p name, or \p fallback when it is unset.
/// \throw std::invalid_argument If the variable is not a number.
[[nodiscard]] std::uint64_t envNumber(char const *name, std::uint64_t fallback) {
    auto const *value = std::getenv(name);
    if (!value)
        return fallback;
    std::uint64_t number{};
    auto const end = value + std::strlen(value);
    auto const [last, error] = std::from_chars(value, end, number);
    if (error != std::errc{} || last != end)
        throw std::invalid_argument(std::format("{} is not a number: '{}'", name, value));
    return number;
}
} // namespace

TEST(OptixFuzzUpdateTests, MatchesAFreshContextAfterRandomEdits) {
    if (skipWithoutCuda())
        GTEST_SKIP() << "Stream-ordered CUDA allocation is unavailable";

    auto const seeds = envNumber("FLUX_FUZZ_SEEDS", 3);
    auto const steps = envNumber("FLUX_FUZZ_STEPS", 30);
    auto const first = envNumber("FLUX_FUZZ_FIRST_SEED", 1);
    auto const verbose = std::getenv("FLUX_FUZZ_VERBOSE") != nullptr;
    for (std::uint64_t seed = first; seed < first + seeds; ++seed) {
        // Name the seed first, so a crash still tells which one to replay.
        std::cerr << std::format("fuzz: seed {}, steps {}", seed, steps) << std::endl;
        SCOPED_TRACE(std::format("seed {} steps {}", seed, steps));
        auto ok = false;
        try {
            Gen gen(static_cast<std::uint32_t>(seed));
            Rig rig(makeBase().spec);
            std::size_t printed = 0;
            for (std::uint64_t step = 0; step < steps && rig.ok; ++step) {
                try {
                    runStep(rig, gen, static_cast<int>(step));
                } catch (std::exception const &e) {
                    rig.report(std::string("exception: ") + e.what());
                }
                for (; verbose && printed < rig.log.size(); ++printed)
                    std::cerr << "seed " << seed << ": " << rig.log[printed] << std::endl;
            }
            ok = rig.ok;
        } catch (std::exception const &e) { ADD_FAILURE() << "exception: " << e.what(); }

        // A failed seed may leave the device unusable, so later seeds would fail for no reason.
        if (!ok) {
            ADD_FAILURE() << "failing run: seed " << seed << ", steps " << steps;
            break;
        }
    }
}

TEST(OptixFuzzUpdateTests, FollowsAHydraStyleEditSequence) {
    if (skipWithoutCuda())
        GTEST_SKIP() << "Stream-ordered CUDA allocation is unavailable";

    auto base = makeBase();
    Rig rig(base.spec);
    auto previous = rig.frame();

    // Applies the next step: the image must change if \p changes, and stay otherwise.
    auto const next = [&](char const *what, bool changes) {
        auto image = rig.frame();
        EXPECT_EQ(!(image == previous), changes) << what;
        previous = std::move(image);
    };

    rig.move(base.occluder, makeTransform(0.6F, 1.1F, false, {0.3F, 0.2F, 0.5F}));
    next("move a primitive", true);

    rig.rebindBSDF(base.floor, base.green);
    next("rebind a BSDF", true);

    rig.setRadiance(base.lamp, Spectrum{8.0F, 6.0F, 2.0F});
    next("change the radiance", true);

    auto const beforeHide = previous;
    rig.setVisible(base.occluder, false);
    next("hide", true);
    rig.setVisible(base.occluder, true);
    next("show", true);
    EXPECT_TRUE(previous == beforeHide) << "showing restores the image";

    // The pillar owns its mesh and BSDF, and both are collected.
    rig.removePrimitive(base.pillar);
    next("remove a primitive", true);
    EXPECT_FALSE(rig.model.meshes.contains(base.meshPillar));
    EXPECT_FALSE(rig.model.bsdfs.contains(base.red));
    EXPECT_EQ(rig.context->getObjects<flux::Geometry>().size(), 3U);

    auto const mesh = rig.addMesh({{Vec3f{0, 0, 0}, Vec3f{0.8F, 0, 0}, Vec3f{0, 0.8F, 0}}});
    (void)rig.addPrimitive(
        mesh, base.gray, std::nullopt, makeTransform(1, 0.5F, false, {0.6F, 0.6F, 0.3F})
    );
    next("add a primitive", true);

    rig.replaceMesh(base.occluder, {{Vec3f{0, 0, 0}, Vec3f{0.5F, 0, 0}, Vec3f{0, 1.2F, 0}}});
    next("replace a mesh", true);
    EXPECT_FALSE(rig.model.meshes.contains(base.meshOccluder));

    rig.movePoint({0.0F, 0.9F, 0.6F});
    rig.setIntensity(Spectrum{3.0F, 1.0F, 1.0F});
    next("move the point light and change its intensity", true);

    auto const beforeEnv = previous;
    rig.setEnv(Spectrum{0.3F, 0.4F, 0.5F});
    next("set the environment map", true);
    rig.scaleEnv(Spectrum{0.9F});
    next("scale the environment map", true);
    rig.unsetEnv(false);
    next("unset the environment map", true);
    EXPECT_TRUE(previous == beforeEnv) << "unsetting restores the image";

    rig.switchIntegrator({1, true}, true);
    next("switch the integrator", true);

    next("no edit", false);
    EXPECT_TRUE(rig.ok);
}

TEST(OptixFuzzUpdateTests, RebuildsAfterAClearWithoutASync) {
    if (skipWithoutCuda())
        GTEST_SKIP() << "Stream-ordered CUDA allocation is unavailable";

    auto base = makeBase();
    for (int variant = 0; variant < 3; ++variant) {
        SCOPED_TRACE(variant);
        Rig rig(base.spec);
        auto const before = rig.frame();

        // The first edit's changes are cleared without a sync.
        rig.move(base.occluder, makeTransform(0.6F, 1.1F, false, {0.3F, 0.2F, 0.5F}));
        rig.clearOnly();
        if (variant == 1)
            rig.setRadiance(base.lamp, Spectrum{1.0F});
        if (variant == 2) {
            // Indices are released and reused across the clears that no sync saw.
            rig.removePrimitive(base.pillar);
            rig.clearOnly();
            auto const mesh = rig.addMesh({{Vec3f{0, 0, 0}, Vec3f{0.8F, 0, 0}, Vec3f{0, 0.8F, 0}}});
            (void)rig.addPrimitive(
                mesh, base.green, std::nullopt, makeTransform(1, 0, false, {0.4F, 0.4F, 0.3F})
            );
            rig.clearOnly();
        }
        auto const after = rig.frame();
        EXPECT_FALSE(after == before);
        EXPECT_TRUE(rig.ok);
    }
}

TEST(OptixFuzzUpdateTests, TwoSyncsWithoutAClearMatchOne) {
    if (skipWithoutCuda())
        GTEST_SKIP() << "Stream-ordered CUDA allocation is unavailable";

    auto base = makeBase();
    Rig once(base.spec);
    Rig twice(base.spec);
    for (auto *rig : {&once, &twice}) {
        (void)rig->frame();
        rig->move(base.occluder, makeTransform(0.6F, 1.1F, false, {0.3F, 0.2F, 0.5F}));
        rig->removePrimitive(base.pillar);
    }
    auto const one = once.frame(1);
    auto const two = twice.frame(2);
    EXPECT_TRUE(one == two);
    EXPECT_TRUE(once.ok && twice.ok);
}

// The environment map's sampling distribution depends on its image, the component mapping, scale,
// and bias of its texture, and its scale, and exists only while an image map is active. Each edit
// below changes one of them, or none, as a rotation does.
TEST(OptixFuzzUpdateTests, FollowsEveryInputOfTheEnvironmentDistribution) {
    if (skipWithoutCuda())
        GTEST_SKIP() << "Stream-ordered CUDA allocation is unavailable";

    auto base = makeBase();
    Rig rig(base.spec);
    auto const image = Spec::Image{.file = 0, .srgb = false, .point = false, .addressMode = 0};
    rig.setEnv(Spectrum{0.5F}, image);
    auto const before = rig.frame();

    // Another map of the same image, whose mapping swaps red and blue.
    auto swapped = image;
    swapped.mapping = "zyx1";
    rig.unsetEnv(true);
    rig.setEnv(Spectrum{0.5F}, swapped);
    auto const afterSwap = rig.frame();
    EXPECT_FALSE(afterSwap == before);

    // A new texture of the same image and mapping, whose scale differs per channel.
    auto scaled = swapped;
    scaled.scale = flux::Vec4f{0.2F, 1.0F, 3.0F, 1.0F};
    rig.retextureEnv(scaled);
    auto const afterScale = rig.frame();
    EXPECT_FALSE(afterScale == afterSwap);

    // A new texture of the same image, mapping, and scale, whose bias differs per channel.
    auto biased = scaled;
    biased.bias = flux::Vec4f{0.5F, 0.0F, 0.1F, 0.0F};
    rig.retextureEnv(biased);
    EXPECT_FALSE(rig.frame() == afterScale);

    auto const scale = Spectrum{0.2F, 0.6F, 0.9F};
    rig.scaleEnv(scale);
    (void)rig.frame();

    rig.rotateEnv(makeRotation({0.5F, pi / 2.0F, 0.0F}));
    (void)rig.frame();

    // A zero scale leaves no distribution to sample, and a positive one brings it back.
    rig.scaleEnv(Spectrum{0.0F});
    (void)rig.frame();
    rig.scaleEnv(scale);
    (void)rig.frame();

    // Another image, with the same mapping and scale.
    auto other = swapped;
    other.file = 4;
    rig.unsetEnv(true);
    rig.setEnv(scale, other);
    (void)rig.frame();

    // A constant map drops the distribution, and the same image builds it again.
    rig.unsetEnv(true);
    rig.setEnv(scale);
    (void)rig.frame();
    rig.unsetEnv(true);
    rig.setEnv(scale, other);
    (void)rig.frame();

    // The same file in the other color space is another ImageAsset.
    auto srgb = other;
    srgb.srgb = true;
    rig.unsetEnv(true);
    rig.setEnv(scale, srgb);
    (void)rig.frame();

    // The same map with new textures: constant, the image in each filter and address mode, which
    // keep its distribution, and another image.
    rig.retextureEnv(std::nullopt);
    (void)rig.frame();
    for (std::size_t mode = 0; mode < addressModes.size(); ++mode) {
        auto again = srgb;
        again.point = mode % 2 == 0;
        again.addressMode = mode;
        rig.retextureEnv(again);
        (void)rig.frame();
    }
    rig.retextureEnv(image);
    (void)rig.frame();
    EXPECT_TRUE(rig.ok);
}

TEST(OptixFuzzUpdateTests, ForgetsWhatASyncWithoutAClearCollects) {
    if (skipWithoutCuda())
        GTEST_SKIP() << "Stream-ordered CUDA allocation is unavailable";

    auto base = makeBase();
    Rig rig(base.spec);
    (void)rig.frame();

    // The green BSDF loses its only primitive, so the sync collects it.
    rig.rebindBSDF(base.occluder, base.red);
    rig.syncOnly();
    EXPECT_FALSE(rig.model.bsdfs.contains(base.green));
    EXPECT_FALSE(rig.ids.bsdfs.contains(base.green));
    (void)rig.frame();
    EXPECT_TRUE(rig.ok);
}

namespace {
/// Copies the OptiX IR to a file that a test can delete.
[[nodiscard]] std::filesystem::path copyModule() {
    auto const directory = std::filesystem::path(FLUX_TEST_OUTPUT_DIR);
    std::filesystem::create_directories(directory);
    auto const copy = directory / "Module.optixir";
    std::filesystem::copy_file(
        FLUX_TEST_OPTIX_IR, copy, std::filesystem::copy_options::overwrite_existing
    );
    return copy;
}
} // namespace

// A sync that fails before the OptiX scene update leaves the handler failed for good. A new handler
// on the same context renders the edits.
// Building the megakernel module reads the IR file. Without that file, the live handler can sync
// only while the module is kept, and a failed sync leaves it failed for good.
// A failed update of the OptiX scene leaves the handler failed for good, even after the cause is
// gone and a clear dropped the tracked changes of the failed edits. A new handler renders them.
TEST(OptixFuzzUpdateTests, FailsForGoodAfterAFailedSync) {
    if (skipWithoutCuda())
        GTEST_SKIP() << "Stream-ordered CUDA allocation is unavailable";

    auto base = makeBase();
    auto const module = copyModule();
    Rig rig(base.spec, module);
    auto const before = rig.frame();

    // Succeed and then fail before a clear, so the clear below drops the failed edits. The
    // sampler switch makes the failing sync rebuild the program after it rebuilt the tables.
    rig.handler->sync();
    auto const saved = module.string() + ".saved";
    std::filesystem::rename(module, saved);
    rig.move(base.occluder, makeTransform(0.6F, 1.1F, false, {0.3F, 0.2F, 0.5F}));
    rig.switchSampler(true, false);
    EXPECT_THROW(rig.handler->sync(), kira::Anyhow);
    EXPECT_TRUE(rig.handler->hasFailed());

    // Drop the tracked changes and restore the module. The failed handler still does nothing.
    rig.clearOnly();
    std::filesystem::rename(saved, module);
    rig.context->collectGarbage();
    rig.handler->sync();
    EXPECT_TRUE(rig.handler->hasFailed());

    rig.restartHandler(module);
    auto const after = rig.compare();
    EXPECT_FALSE(rig.handler->hasFailed());
    EXPECT_FALSE(after == before);
    EXPECT_TRUE(rig.ok);
}
