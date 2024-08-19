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

[film]
resolution = [1280, 
720]
denoise = false
num_samples = 512
a = 1

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

    EXPECT_TRUE(props.type_of<Properties>("camera"));
    EXPECT_TRUE(props.type_of<Properties>("film"));
    EXPECT_TRUE(props.type_of<Properties>("integrator"));
    EXPECT_TRUE(props.type_of<MutablePropertiesView>("camera"));
    EXPECT_TRUE(props.type_of<MutablePropertiesView>("film"));
    EXPECT_TRUE(props.type_of<MutablePropertiesView>("integrator"));
    EXPECT_TRUE(props.type_of<ImmutablePropertiesView>("camera"));
    EXPECT_TRUE(props.type_of<ImmutablePropertiesView>("film"));
    EXPECT_TRUE(props.type_of<ImmutablePropertiesView>("integrator"));
    EXPECT_FALSE(props.type_of<Properties>("primitive"));
    EXPECT_FALSE(props.type_of<Properties>("non_existent"));
    EXPECT_FALSE(props.type_of<int>("camera"));
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

    EXPECT_THROW(film.get_or<std::string>("num_samples", ""), Anyhow);
}

TEST_F(PropertiesTests, MutablePropertiesView) {
    auto mutableView = props.get<MutablePropertiesView>("camera");
    EXPECT_EQ(mutableView.get<double>("focal_length"), 20e-3);

    auto immutableView = props.get<ImmutablePropertiesView>("film");
    EXPECT_EQ(immutableView.get<int>("num_samples"), 512);
}
