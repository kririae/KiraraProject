#include <gtest/gtest.h>

#include <numbers>

#include "kira/Properties.h"

using namespace kira;

class PropertiesTests : public ::testing::Test {
protected:
    std::string_view source = R"(
x = 1
y = 2

[camera]
position = [0.85727, 0.8234, 1.9649]
focal_length = 20e-3
sub = { b = 2 }

[film]
resolution = [1280, 720]
denoise = false
num_samples = 512

[[primitive]]
type = 'trimesh'
path = 'geometry/orange_box.ply'
face_normals = true
light = { type = 'area', emission = [1.0, 0.275, 0.054] })";

    Properties props{toml::parse(source), source};
};

TEST_F(PropertiesTests, ReadsValuesAndViews) {
    EXPECT_TRUE(props.contains("camera"));
    EXPECT_TRUE(props.contains("primitive"));
    EXPECT_FALSE(props.contains("missing"));

    EXPECT_TRUE(props.is_type_of<int>("x"));
    EXPECT_TRUE(props.is_type_of<Properties>("camera"));
    EXPECT_TRUE(props.is_type_of<PropertiesArray>("primitive"));
    EXPECT_FALSE(props.is_type_of<int>("camera"));

    auto camera = props.get_view("camera");
    EXPECT_FLOAT_EQ(camera.get<float>("focal_length"), 20e-3f);

    auto film = props.get<Properties>("film");
    EXPECT_EQ(film.get<bool>("denoise"), false);
    EXPECT_EQ(film.get<int>("num_samples"), 512);

    auto primitives = props.get_array_view("primitive");
    ASSERT_EQ(primitives.size(), 1);
    auto primitive = primitives.get_view(0);
    EXPECT_EQ(primitive.get<std::string>("type"), "trimesh");
    EXPECT_EQ(primitive.get<std::filesystem::path>("path"), "geometry/orange_box.ply");
}

TEST_F(PropertiesTests, PropertyProcessorsReturnAliasingHandles) {
    auto film = props.get<Properties>("film");
    film.set("num_samples", 1024);
    EXPECT_EQ(props.get_view("film").get<int>("num_samples"), 1024);

    auto primitives = props.get<PropertiesArray>("primitive");
    primitives.get_view(0).set("type", "mesh");
    EXPECT_EQ(props.get_array_view("primitive").get_view(0).get<std::string>("type"), "mesh");
}

TEST_F(PropertiesTests, GetDoesNotMarkUseDoes) {
    EXPECT_FALSE(props.is_used("camera"));
    auto camera = props.get_view("camera");
    EXPECT_FALSE(props.is_used("camera"));
    EXPECT_FALSE(camera.is_used("focal_length"));

    EXPECT_FLOAT_EQ(camera.get_or<double>("focal_length", 1.0), 20e-3);
    EXPECT_FALSE(camera.is_used("focal_length"));
    EXPECT_EQ(camera.get_or<int>("missing", 42), 42);
    EXPECT_FALSE(camera.is_used("missing"));

    EXPECT_FLOAT_EQ(camera.use<double>("focal_length"), 20e-3);
    EXPECT_TRUE(camera.is_used("focal_length"));

    EXPECT_EQ(camera.use_or<int>("missing", 7), 7);
    EXPECT_FALSE(camera.is_used("missing"));

    auto sub = camera.use_view("sub");
    EXPECT_TRUE(camera.is_used("sub"));
    EXPECT_EQ(sub.use<int>("b"), 2);
    EXPECT_TRUE(sub.is_all_used());

    auto primitives = props.use_array_view("primitive");
    EXPECT_TRUE(props.is_used("primitive"));
    EXPECT_EQ(primitives.get_view(0).get<std::string>("type"), "trimesh");
}

TEST_F(PropertiesTests, FailedUseDoesNotMark) {
    EXPECT_THROW(props.use<int>("camera"), kira::Anyhow);
    EXPECT_FALSE(props.is_used("camera"));

    EXPECT_THROW(
        {
            auto view = props.use_view("x");
            (void)view;
        },
        kira::Anyhow
    );
    EXPECT_FALSE(props.is_used("x"));

    EXPECT_THROW(
        {
            auto view = props.use_array_view("camera");
            (void)view;
        },
        kira::Anyhow
    );
    EXPECT_FALSE(props.is_used("camera"));
}

TEST_F(PropertiesTests, MarkUsedOnlyTouchesExistingKeys) {
    EXPECT_FALSE(props.mark_used("missing"));
    EXPECT_FALSE(props.mark_unused("missing"));
    EXPECT_FALSE(props.is_used("missing"));

    EXPECT_TRUE(props.mark_used("x"));
    EXPECT_TRUE(props.is_used("x"));
    EXPECT_TRUE(props.mark_unused("x"));
    EXPECT_FALSE(props.is_used("x"));
}

TEST_F(PropertiesTests, ForEachUnusedOnlyReportsCurrentKeys) {
    Properties small;
    small.set("a", 1);
    small.set("b", 2);
    EXPECT_FALSE(small.mark_used("missing"));
    EXPECT_TRUE(small.mark_used("a"));

    kira::SmallVector<std::string> unused;
    small.for_each_unused([&](std::string_view key) { unused.push_back(std::string{key}); });

    ASSERT_EQ(unused.size(), 1);
    EXPECT_EQ(unused[0], "b");
}

TEST_F(PropertiesTests, CopyAliasesCloneDetaches) {
    auto copy = props;
    copy.set("x", 42);
    EXPECT_EQ(props.get<int>("x"), 42);

    EXPECT_TRUE(copy.mark_used("x"));
    EXPECT_TRUE(props.is_used("x"));

    auto clone = props.clone();
    clone.set("x", 7);
    EXPECT_EQ(clone.get<int>("x"), 7);
    EXPECT_EQ(props.get<int>("x"), 42);
    EXPECT_FALSE(clone.is_used("x"));
}

TEST_F(PropertiesTests, ViewsKeepRootAlive) {
    Properties camera;
    PropertiesArray primitives;

    {
        Properties local{toml::parse(source), source};
        camera = local.get_view("camera");
        primitives = local.get_array_view("primitive");
    }

    EXPECT_FLOAT_EQ(camera.get<float>("focal_length"), 20e-3f);
    EXPECT_EQ(primitives.get_view(0).get<std::string>("type"), "trimesh");
}

TEST_F(PropertiesTests, SettingValuesClearsUsageOnReplacedNodes) {
    EXPECT_TRUE(props.mark_used("camera"));
    props.set("camera", true);
    EXPECT_TRUE(props.get<bool>("camera"));
    EXPECT_FALSE(props.is_used("camera"));

    auto film = props.get_view("film");
    EXPECT_TRUE(film.mark_used("num_samples"));
    film.set("num_samples", 1024);
    EXPECT_EQ(film.get<int>("num_samples"), 1024);
    EXPECT_FALSE(film.is_used("num_samples"));
}

TEST_F(PropertiesTests, SetWithoutOverwritePreservesExistingNode) {
    EXPECT_TRUE(props.mark_used("x"));
    EXPECT_THROW(props.set("x", 42, false), kira::Anyhow);
    EXPECT_EQ(props.get<int>("x"), 1);
    EXPECT_TRUE(props.is_used("x"));

    props.set("z", 3, false);
    EXPECT_EQ(props.get<int>("z"), 3);
    EXPECT_FALSE(props.is_used("z"));
}

TEST_F(PropertiesTests, ReplacingSubtreeClearsNestedUsage) {
    auto camera = props.get_view("camera");
    auto sub = camera.get_view("sub");
    EXPECT_TRUE(sub.mark_used("b"));
    EXPECT_TRUE(sub.is_used("b"));

    Properties replacement;
    replacement.set("b", 3);
    camera.set("sub", replacement);

    auto newSub = camera.get_view("sub");
    EXPECT_EQ(newSub.get<int>("b"), 3);
    EXPECT_FALSE(newSub.is_used("b"));
}

TEST_F(PropertiesTests, ClearRemovesUsageUnderCurrentHandle) {
    auto camera = props.get_view("camera");
    auto sub = camera.get_view("sub");
    EXPECT_TRUE(camera.mark_used("focal_length"));
    EXPECT_TRUE(sub.mark_used("b"));

    camera.clear();
    camera.set("focal_length", 50e-3);

    Properties replacement;
    replacement.set("b", 3);
    camera.set("sub", replacement);

    EXPECT_FALSE(camera.is_used("focal_length"));
    EXPECT_FALSE(camera.get_view("sub").is_used("b"));
}

TEST_F(PropertiesTests, ArrayClearRemovesNestedUsage) {
    Properties item;
    item.set("x", 1);

    PropertiesArray array;
    array.push_back(item);
    EXPECT_TRUE(array.get_view(0).mark_used("x"));
    EXPECT_TRUE(array.get_view(0).is_used("x"));

    array.clear();
    item.set("x", 2);
    array.push_back(item);
    EXPECT_FALSE(array.get_view(0).is_used("x"));
}

TEST_F(PropertiesTests, ArrayHandlesAliasAndClone) {
    PropertiesArray array;
    array.push_back(1);
    array.push_back(2);

    auto copy = array;
    copy.set(0, 10);
    copy.push_back(3);
    EXPECT_EQ(array.size(), 3);
    EXPECT_EQ(array.get<int>(0), 10);

    auto clone = array.clone();
    clone.set(0, 100);
    EXPECT_EQ(clone.get<int>(0), 100);
    EXPECT_EQ(array.get<int>(0), 10);
}

TEST_F(PropertiesTests, ArrayViewsShareNestedTables) {
    Properties item;
    item.set("x", 1);

    PropertiesArray array;
    array.push_back(item);

    auto view = array.get_view(0);
    view.set("x", 2);
    EXPECT_EQ(array.get_view(0).get<int>("x"), 2);

    EXPECT_TRUE(view.mark_used("x"));
    EXPECT_TRUE(array.get_view(0).is_used("x"));

    Properties replacement;
    replacement.set("x", 3);
    array.set(0, replacement);
    EXPECT_EQ(array.get_view(0).get<int>("x"), 3);
    EXPECT_FALSE(array.get_view(0).is_used("x"));
}

TEST_F(PropertiesTests, SetAcceptsSupportedScalarTypes) {
    Properties values;
    values.set("bool", true);
    values.set("int", 42);
    values.set("float", std::numbers::pi_v<float>);
    values.set("double", std::numbers::pi);
    values.set("string", "hello");
    values.set("path", std::filesystem::path{"a/b"});

    EXPECT_TRUE(values.get<bool>("bool"));
    EXPECT_EQ(values.get<int>("int"), 42);
    EXPECT_FLOAT_EQ(values.get<float>("float"), std::numbers::pi_v<float>);
    EXPECT_DOUBLE_EQ(values.get<double>("double"), std::numbers::pi);
    EXPECT_EQ(values.get<std::string>("string"), "hello");
    EXPECT_EQ(values.get<std::filesystem::path>("path"), std::filesystem::path{"a/b"});
}

TEST_F(PropertiesTests, SerializesCurrentTable) {
    Properties values;
    values.set("x", 1);
    values.set("name", "orange");

    auto parsed = toml::parse(values.to_toml());
    EXPECT_EQ(parsed["x"].value<int>(), 1);
    EXPECT_EQ(parsed["name"].value<std::string>(), "orange");
    EXPECT_FALSE(values.to_json().empty());
    EXPECT_FALSE(values.to_yaml().empty());
}

TEST_F(PropertiesTests, ThrowsOnMissingOrWrongType) {
    EXPECT_THROW(props.get<int>("missing"), kira::Anyhow);
    EXPECT_THROW(props.get<int>("camera"), kira::Anyhow);
    EXPECT_THROW(
        {
            auto view = props.get_view("x");
            (void)view;
        },
        kira::Anyhow
    );
    EXPECT_THROW(
        {
            auto view = props.get_array_view("camera");
            (void)view;
        },
        kira::Anyhow
    );

    auto primitives = props.get_array_view("primitive");
    EXPECT_THROW(primitives.get<int>(0), kira::Anyhow);
    EXPECT_THROW(primitives.get<int>(10), kira::Anyhow);
    EXPECT_THROW(
        {
            auto view = primitives.get_array_view(0);
            (void)view;
        },
        kira::Anyhow
    );
}

TEST_F(PropertiesTests, EmptyDetectsEmptyTable) {
    Properties empty;
    EXPECT_TRUE(empty.empty());
    empty.set("a", 1);
    EXPECT_FALSE(empty.empty());
}

TEST_F(PropertiesTests, GetOrThrowsOnTypeMismatch) {
    EXPECT_THROW(props.get_or<int>("camera", 0), kira::Anyhow);
}

TEST_F(PropertiesTests, GetViewNoArgReturnsSelfView) {
    auto view = props.get_view();
    view.set("x", 42);
    EXPECT_EQ(props.get<int>("x"), 42);
    EXPECT_TRUE(view.mark_used("x"));
    EXPECT_TRUE(props.is_used("x"));
}

TEST_F(PropertiesTests, SmallVectorConstructor) {
    kira::SmallVector<std::string> lines{"x = 1", "y = 2"};
    auto table = toml::parse("x = 1\ny = 2");
    Properties props{table, lines};
    EXPECT_EQ(props.get<int>("x"), 1);
    EXPECT_EQ(props.get<int>("y"), 2);
}

TEST_F(PropertiesTests, Int64AndUint32RoundTrip) {
    Properties values;
    values.set("i64", int64_t{-1});
    values.set("u32", uint32_t{42});
    EXPECT_EQ(values.get<int64_t>("i64"), -1);
    EXPECT_EQ(values.get<uint32_t>("u32"), 42u);
}

TEST_F(PropertiesTests, MoveSemantics) {
    Properties a;
    a.set("x", 1);
    Properties b{std::move(a)};
    EXPECT_EQ(b.get<int>("x"), 1);

    Properties c;
    c = std::move(b);
    EXPECT_EQ(c.get<int>("x"), 1);
}

TEST_F(PropertiesTests, ArrayEmptyDetectsEmptyArray) {
    PropertiesArray array;
    EXPECT_TRUE(array.empty());
    array.push_back(1);
    EXPECT_FALSE(array.empty());
}

TEST_F(PropertiesTests, ArrayIsTypeOf) {
    PropertiesArray array;
    array.push_back(42);
    array.push_back(3.14);

    EXPECT_TRUE(array.is_type_of<int>(0));
    EXPECT_TRUE(array.is_type_of<double>(0));
    EXPECT_FALSE(array.is_type_of<std::string>(0));
    EXPECT_FALSE(array.is_type_of<int>(10));
}

TEST_F(PropertiesTests, ArrayGetOrOutOfBounds) {
    PropertiesArray array;
    array.push_back(1);
    EXPECT_EQ(array.get_or(0, 42), 1);
    EXPECT_EQ(array.get_or(5, 42), 42);
}

TEST_F(PropertiesTests, ArrayGetOrTypeMismatch) {
    PropertiesArray array;
    array.push_back(42);
    EXPECT_THROW(array.get_or<std::string>(0, ""), kira::Anyhow);
}

TEST_F(PropertiesTests, ArrayGetViewNoArg) {
    PropertiesArray array;
    array.push_back(1);
    auto view = array.get_array_view();
    view.set(0, 42);
    EXPECT_EQ(array.get<int>(0), 42);
}

TEST_F(PropertiesTests, ArrayTomlArrayConstructor) {
    toml::array arr;
    arr.push_back(1);
    arr.push_back(2);
    PropertiesArray array{std::move(arr)};
    EXPECT_EQ(array.size(), 2);
    EXPECT_EQ(array.get<int>(0), 1);
    EXPECT_EQ(array.get<int>(1), 2);
}

TEST_F(PropertiesTests, ArrayMoveSemantics) {
    PropertiesArray a;
    a.push_back(1);
    PropertiesArray b{std::move(a)};
    EXPECT_EQ(b.get<int>(0), 1);

    PropertiesArray c;
    c = std::move(b);
    EXPECT_EQ(c.get<int>(0), 1);
}
