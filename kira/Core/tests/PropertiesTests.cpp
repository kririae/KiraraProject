#include <gtest/gtest.h>

#include "kira/Properties.h"

using namespace kira;

class PropertiesTests : public ::testing::Test {
    void SetUp() override {
        table = toml::parse(source);
        props = Properties(toml::parse(source), source);
    }

    void TearDown() override {
        table.clear();
        props.clear();
    }

protected:
    toml::table table;
    Properties props;
    std::string_view source = R"([camera]
position = [0.85727, 0.8234, 1.9649]
focal_length = 20e-3
sub = { b = 2 }

[film]
resolution = [1280, 
720]
denoise = false
num_samples = 512
a = 1
sub = { b = 2 }

[integrator]
type = 'path'
max_depth = 64
rr_depth = 8
rr_prob = 0.1

[[primitive]]
type = 'trimesh'
path = 'geometry/orange_box.ply'
face_normals = true
bsdf = 'base_0'
light = { type = 'area', emission = [1.0, 0.275, 0.054] })";
};

TEST_F(PropertiesTests, ContainsAndTypeOf) {
    EXPECT_TRUE(props.contains("camera"));
    EXPECT_TRUE(props.contains("film"));
    EXPECT_TRUE(props.contains("integrator"));
    EXPECT_TRUE(props.contains("primitive"));
    EXPECT_FALSE(props.contains("non_existent"));

    EXPECT_TRUE(props.is_type_of<Properties>("camera"));
    EXPECT_TRUE(props.is_type_of<Properties>("film"));
    EXPECT_TRUE(props.is_type_of<Properties>("integrator"));
    EXPECT_TRUE(props.is_type_of<MutablePropertiesView>("camera"));
    EXPECT_TRUE(props.is_type_of<MutablePropertiesView>("film"));
    EXPECT_TRUE(props.is_type_of<MutablePropertiesView>("integrator"));
    EXPECT_TRUE(props.is_type_of<ImmutablePropertiesView>("camera"));
    EXPECT_TRUE(props.is_type_of<ImmutablePropertiesView>("film"));
    EXPECT_TRUE(props.is_type_of<ImmutablePropertiesView>("integrator"));
    EXPECT_FALSE(props.is_type_of<Properties>("primitive"));
    EXPECT_FALSE(props.is_type_of<Properties>("non_existent"));
    EXPECT_FALSE(props.is_type_of<int>("camera"));
}

TEST_F(PropertiesTests, GetProperties) {
    auto camera = props.get<Properties>("camera");
    EXPECT_TRUE(camera.contains("position"));
    EXPECT_TRUE(camera.contains("focal_length"));

    auto film = props.get<Properties>("film");
    EXPECT_TRUE(film.contains("resolution"));
    EXPECT_TRUE(film.contains("denoise"));
    EXPECT_TRUE(film.contains("num_samples"));
    EXPECT_TRUE(film.contains("a"));

    auto integrator = props.get<Properties>("integrator");
    EXPECT_TRUE(integrator.contains("type"));
    EXPECT_TRUE(integrator.contains("max_depth"));
    EXPECT_TRUE(integrator.contains("rr_depth"));
    EXPECT_TRUE(integrator.contains("rr_prob"));

    EXPECT_THROW(props.get<Properties>("non_existent"), Anyhow);
}

TEST_F(PropertiesTests, GetPropertiesView) {
    auto camera = props.get<MutablePropertiesView>("camera");
    EXPECT_TRUE(camera.contains("position"));
    EXPECT_TRUE(camera.contains("focal_length"));

    auto film = props.get<ImmutablePropertiesView>("film");
    EXPECT_TRUE(film.contains("resolution"));
    EXPECT_TRUE(film.contains("denoise"));
    EXPECT_TRUE(film.contains("num_samples"));
    EXPECT_TRUE(film.contains("a"));

    EXPECT_THROW(props.get<MutablePropertiesView>("non_existent"), Anyhow);
}

TEST_F(PropertiesTests, GetBasicTypes) {
    auto camera = props.get<Properties>("camera");
    EXPECT_FLOAT_EQ(camera.get<double>("focal_length"), 20e-3);
    EXPECT_FLOAT_EQ(camera.get<float>("focal_length"), 20e-3);

    auto film = props.get<Properties>("film");
    EXPECT_EQ(film.get<bool>("denoise"), false);
    EXPECT_EQ(film.get<int>("num_samples"), 512);
    EXPECT_EQ(film.get<int>("a"), 1);

    auto integrator = props.get<Properties>("integrator");
    EXPECT_EQ(integrator.get<std::string>("type"), "path");
    EXPECT_EQ(integrator.get<int>("max_depth"), 64);
    EXPECT_EQ(integrator.get<int>("rr_depth"), 8);
    EXPECT_FLOAT_EQ(integrator.get<double>("rr_prob"), 0.1);

    EXPECT_THROW(film.get<int>("non_existent"), Anyhow);
    EXPECT_THROW(film.get<std::string>("num_samples"), Anyhow);
    EXPECT_THROW(integrator.get<std::string>("rr_prob"), Anyhow);
    EXPECT_THROW(integrator.get<int>("rr_prob"), Anyhow);
}

TEST_F(PropertiesTests, GetOrBasicTypes) {
    auto camera = props.get<Properties>("camera");
    EXPECT_FLOAT_EQ(camera.get_or<double>("focal_length", 30e-3), 20e-3);
    EXPECT_FLOAT_EQ(camera.get_or<double>("non_existent", 30e-3), 30e-3);

    auto film = props.get<Properties>("film");
    EXPECT_EQ(film.get_or<bool>("denoise", true), false);
    EXPECT_EQ(film.get_or<int>("num_samples", 1024), 512);
    EXPECT_EQ(film.get_or<int>("non_existent", 2048), 2048);

    EXPECT_THROW({ film.get_or<std::string>("num_samples", ""); }, Anyhow);
    EXPECT_THROW({ film.get_or<std::filesystem::path>("num_samples", {}); }, Anyhow);

    auto filmView = props.get<ImmutablePropertiesView>("film");
    EXPECT_EQ(filmView.get_or<bool>("denoise", true), false);
    EXPECT_EQ(filmView.get_or<int>("num_samples", 1024), 512);
    EXPECT_EQ(filmView.get_or<int>("non_existent", 2048), 2048);
}

TEST_F(PropertiesTests, MutablePropertiesView) {
    auto mutableView = props.get<MutablePropertiesView>("camera");
    EXPECT_EQ(mutableView.get<double>("focal_length"), 20e-3);

    auto immutableView = props.get<ImmutablePropertiesView>("film");
    EXPECT_EQ(immutableView.get<int>("num_samples"), 512);

    // Immutable from a immutable
    auto immutableView2 = immutableView.get<ImmutablePropertiesView>("sub");

    // Immutable from a mutable
    auto immutableView3 = mutableView.get<ImmutablePropertiesView>("sub");
}

TEST_F(PropertiesTests, UseQuery) {
    auto camera = props.get<Properties>("camera");
    EXPECT_FALSE(props.is_used("camera"));
    props.mark_used("camera");
    EXPECT_TRUE(props.is_used("camera"));

    auto sub = camera.get<MutablePropertiesView>("sub");
    EXPECT_FALSE(sub.is_used("b"));
    sub.mark_used("b");
    EXPECT_TRUE(sub.is_used("b"));

    sub.mark_used("non_existent");
    EXPECT_TRUE(sub.is_used("non_existent"));

    EXPECT_FALSE(camera.is_all_used());
    camera.mark_used("position");
    camera.mark_used("focal_length");

    SmallVector<std::string> unusedKeys;
    camera.for_each_unused([&unusedKeys](std::string_view key) {
        unusedKeys.push_back(std::string(key));
    });
    EXPECT_EQ(unusedKeys.size(), 1);
    EXPECT_EQ(unusedKeys[0], "sub");
    EXPECT_FALSE(camera.is_used("sub"));
    camera.mark_used("sub");
    EXPECT_TRUE(camera.is_used("sub"));
    EXPECT_TRUE(camera.is_all_used());
}

TEST_F(PropertiesTests, SetExists) {
    EXPECT_THROW(props.set<bool>("camera", true, false), Anyhow);
    EXPECT_FALSE(props.is_used("camera"));
    props.set<bool>("camera", true, true);
    EXPECT_EQ(props.contains("camera"), true);
    EXPECT_EQ(props.get<bool>("camera"), true);
    EXPECT_FALSE(props.is_used("camera"));

    props.mark_used("camera");
    EXPECT_TRUE(props.is_used("camera"));
}

TEST_F(PropertiesTests, SetNotExists) {
    props.set<bool>("not_existent", true, true);
    EXPECT_FALSE(props.is_used("not_existent"));
    EXPECT_THROW(props.set<int>("not_existent", 42, false), Anyhow);
    EXPECT_TRUE(props.contains("not_existent"));
    EXPECT_TRUE(props.get<bool>("not_existent"));
    EXPECT_FALSE(props.is_used("not_existent"));
    props.mark_used("not_existent");
    EXPECT_TRUE(props.is_used("not_existent"));
}

TEST_F(PropertiesTests, SetComprehensive) {
    auto newProps = Properties();

    newProps.set<bool>("bool_true", true);
    newProps.set<bool>("bool_false", false);

    newProps.set<int>("int32_max", std::numeric_limits<int>::max());
    newProps.set<int>("int32_min", std::numeric_limits<int>::min());
    newProps.set<int64_t>("int64_max", std::numeric_limits<int64_t>::max());
    newProps.set<int64_t>("int64_min", std::numeric_limits<int64_t>::min());
    newProps.set<uint32_t>("uint32_max", std::numeric_limits<uint32_t>::max());

    newProps.set<float>("float_pi", 3.14159f);
    newProps.set<float>("float_max", std::numeric_limits<float>::max());
    newProps.set<float>("float_min", std::numeric_limits<float>::min());
    newProps.set<double>("double_pi", 3.14159265358979323846);
    newProps.set<double>("double_max", std::numeric_limits<double>::max());
    newProps.set<double>("double_min", std::numeric_limits<double>::min());

    newProps.set<std::string>("string_empty", "");
    newProps.set<std::string>("string_hello", "Hello, World!");

    EXPECT_TRUE(newProps.get<bool>("bool_true"));
    EXPECT_FALSE(newProps.get<bool>("bool_false"));
    EXPECT_EQ(newProps.get<int>("int32_max"), std::numeric_limits<int>::max());
    EXPECT_EQ(newProps.get<int>("int32_min"), std::numeric_limits<int>::min());
    EXPECT_EQ(newProps.get<int64_t>("int64_max"), std::numeric_limits<int64_t>::max());
    EXPECT_EQ(newProps.get<int64_t>("int64_min"), std::numeric_limits<int64_t>::min());
    EXPECT_EQ(newProps.get<uint32_t>("uint32_max"), std::numeric_limits<uint32_t>::max());
    EXPECT_EQ(newProps.get<float>("float_pi"), 3.14159f);
    EXPECT_EQ(newProps.get<float>("float_max"), std::numeric_limits<float>::max());
    EXPECT_EQ(newProps.get<float>("float_min"), std::numeric_limits<float>::min());
    EXPECT_EQ(newProps.get<double>("double_pi"), 3.14159265358979323846);
    EXPECT_EQ(newProps.get<double>("double_max"), std::numeric_limits<double>::max());
    EXPECT_EQ(newProps.get<double>("double_min"), std::numeric_limits<double>::min());
    EXPECT_EQ(newProps.get<std::string>("string_empty"), "");
    EXPECT_EQ(newProps.get<std::string>("string_hello"), "Hello, World!");

    auto camera = props.get<Properties>("camera");
    auto cameraImmutable = props.get<ImmutablePropertiesView>("camera");
    auto cameraMutable = props.get<MutablePropertiesView>("camera");

    newProps.set<Properties>("camera_props", camera);
    auto newCameraRef = newProps.get<ImmutablePropertiesView>("camera_props");
    EXPECT_EQ(
        cameraImmutable.get<double>("focal_length"), newCameraRef.get<double>("focal_length")
    );

    // Clone the camera properties
    newProps.set<ImmutablePropertiesView>("camera_immutable", cameraImmutable);

    // Clone the camera properties
    newProps.set<MutablePropertiesView>("camera_mutable", cameraMutable);

    newCameraRef = newProps.get<ImmutablePropertiesView>("camera_immutable");
    EXPECT_EQ(
        cameraImmutable.get<double>("focal_length"), newCameraRef.get<double>("focal_length")
    );
    auto newCameraRef2 = newProps.get<MutablePropertiesView>("camera_mutable");
    EXPECT_EQ(cameraMutable.get<double>("focal_length"), newCameraRef2.get<double>("focal_length"));
}

TEST_F(PropertiesTests, SetView) {
    auto camera = props.get<MutablePropertiesView>("camera");
    auto cameraMutable = props.get<MutablePropertiesView>("camera");

    cameraMutable.set<double>("focal_length", 50e-3);
    EXPECT_EQ(camera.get<double>("focal_length"), 50e-3);

    auto subView = cameraMutable.get<MutablePropertiesView>("sub");
    subView.set<int>("num_samples", 1024);

    EXPECT_EQ(camera.get<double>("focal_length"), 50e-3);

    auto newSubView = camera.get<ImmutablePropertiesView>("sub");
    EXPECT_EQ(newSubView.get<int>("num_samples"), 1024);
    EXPECT_EQ(subView.get<int>("num_samples"), 1024);

    // Add results to the original view
    cameraMutable.set<float>("pi", 3.14159f);
    EXPECT_EQ(camera.get<float>("pi"), 3.14159f);
}
