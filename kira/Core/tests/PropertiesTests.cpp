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

TEST_F(PropertiesTests, PropertiesContainsAndTypeOf) {
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

TEST_F(PropertiesTests, PropertiesGet) {
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

TEST_F(PropertiesTests, PropertiesViewGet) {
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

TEST_F(PropertiesTests, PropertiesGetBasicTypes) {
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

TEST_F(PropertiesTests, PropertiesGetOrBasicTypes) {
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

TEST_F(PropertiesTests, PropertiesUseQuery) {
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

TEST_F(PropertiesTests, PropertiesSetExists) {
    EXPECT_THROW(props.set<bool>("camera", true, false), Anyhow);
    EXPECT_FALSE(props.is_used("camera"));
    props.set<bool>("camera", true, true);
    EXPECT_EQ(props.contains("camera"), true);
    EXPECT_EQ(props.get<bool>("camera"), true);
    EXPECT_FALSE(props.is_used("camera"));

    props.mark_used("camera");
    EXPECT_TRUE(props.is_used("camera"));
}

TEST_F(PropertiesTests, PropertiesSetNotExists) {
    props.set<bool>("not_existent", true, true);
    EXPECT_FALSE(props.is_used("not_existent"));
    EXPECT_THROW(props.set<int>("not_existent", 42, false), Anyhow);
    EXPECT_TRUE(props.contains("not_existent"));
    EXPECT_TRUE(props.get<bool>("not_existent"));
    EXPECT_FALSE(props.is_used("not_existent"));
    props.mark_used("not_existent");
    EXPECT_TRUE(props.is_used("not_existent"));
}

TEST_F(PropertiesTests, PropertiesSetComprehensive) {
    auto newProps = Properties();

    newProps.set<bool>("bool_true", true);
    newProps.set<bool>("bool_false", false);

    newProps.set<int>("int32_max", std::numeric_limits<int>::max());
    newProps.set<int>("int32_min", std::numeric_limits<int>::min());
    newProps.set<int>("int32_lowest", std::numeric_limits<int>::lowest());
    newProps.set<int64_t>("int64_max", std::numeric_limits<int64_t>::max());
    newProps.set<int64_t>("int64_min", std::numeric_limits<int64_t>::min());
    newProps.set<int64_t>("int64_lowest", std::numeric_limits<int64_t>::lowest());
    newProps.set<uint32_t>("uint32_max", std::numeric_limits<uint32_t>::max());

    newProps.set<float>("float_pi", 3.14159f);
    newProps.set<float>("float_max", std::numeric_limits<float>::max());
    newProps.set<float>("float_min", std::numeric_limits<float>::min());
    newProps.set<float>("float_lowest", std::numeric_limits<float>::lowest());
    newProps.set<double>("double_pi", 3.14159265358979323846);
    newProps.set<double>("double_max", std::numeric_limits<double>::max());
    newProps.set<double>("double_min", std::numeric_limits<double>::min());
    newProps.set<double>("double_lowest", std::numeric_limits<double>::lowest());

    newProps.set<std::string>("string_empty", "");
    newProps.set<std::string>("string_hello", "Hello, World!");

    EXPECT_TRUE(newProps.get<bool>("bool_true"));
    EXPECT_FALSE(newProps.get<bool>("bool_false"));
    EXPECT_EQ(newProps.get<int>("int32_max"), std::numeric_limits<int>::max());
    EXPECT_EQ(newProps.get<int>("int32_min"), std::numeric_limits<int>::min());
    EXPECT_EQ(newProps.get<int>("int32_lowest"), std::numeric_limits<int>::lowest());
    EXPECT_EQ(newProps.get<int64_t>("int64_max"), std::numeric_limits<int64_t>::max());
    EXPECT_EQ(newProps.get<int64_t>("int64_min"), std::numeric_limits<int64_t>::min());
    EXPECT_EQ(newProps.get<int64_t>("int64_lowest"), std::numeric_limits<int64_t>::lowest());
    EXPECT_EQ(newProps.get<uint32_t>("uint32_max"), std::numeric_limits<uint32_t>::max());
    EXPECT_EQ(newProps.get<float>("float_pi"), 3.14159f);
    EXPECT_EQ(newProps.get<float>("float_max"), std::numeric_limits<float>::max());
    EXPECT_EQ(newProps.get<float>("float_min"), std::numeric_limits<float>::min());
    EXPECT_EQ(newProps.get<float>("float_lowest"), std::numeric_limits<float>::lowest());
    EXPECT_EQ(newProps.get<double>("double_pi"), 3.14159265358979323846);
    EXPECT_EQ(newProps.get<double>("double_max"), std::numeric_limits<double>::max());
    EXPECT_EQ(newProps.get<double>("double_lowest"), std::numeric_limits<double>::lowest());
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

TEST_F(PropertiesTests, PropertiesViewSet) {
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

TEST_F(PropertiesTests, PropertiesArrayGet) {
    auto primitives = props.get<PropertiesArray>("primitive");
    EXPECT_EQ(primitives.size(), 1);
    EXPECT_FALSE(primitives.empty());

    auto primitiveView = props.get<PropertiesArrayView<true>>("primitive");
    EXPECT_EQ(primitiveView.size(), 1);
    EXPECT_FALSE(primitiveView.empty());

    auto primitiveView2 = props.get<PropertiesArrayView<false>>("primitive");
    EXPECT_EQ(primitiveView2.size(), 1);
    EXPECT_FALSE(primitiveView2.empty());

    EXPECT_ANY_THROW(primitives.get<bool>(0));
    EXPECT_ANY_THROW(primitives.get<bool>(1));
    EXPECT_ANY_THROW(primitiveView.get<bool>(0));
    EXPECT_ANY_THROW(primitiveView.get<bool>(1));
    EXPECT_ANY_THROW(primitiveView2.get<bool>(0));
    EXPECT_ANY_THROW(primitiveView2.get<bool>(1));

    auto checkPrimitive = [](auto const &firstPrimitive) {
        EXPECT_EQ(firstPrimitive.template get<std::string>("type"), "trimesh");
        EXPECT_EQ(firstPrimitive.template get<std::string>("path"), "geometry/orange_box.ply");
        EXPECT_EQ(firstPrimitive.template get<bool>("face_normals"), true);
        EXPECT_EQ(firstPrimitive.template get<std::string>("bsdf"), "base_0");
    };

    checkPrimitive(primitives.get<Properties>(0));
    checkPrimitive(primitives.get<PropertiesView<false>>(0));
    checkPrimitive(primitives.get<PropertiesView<true>>(0));

    checkPrimitive(primitiveView.get<Properties>(0));
    checkPrimitive(primitiveView.get<PropertiesView<false>>(0));
    checkPrimitive(primitiveView.get<PropertiesView<true>>(0));

    checkPrimitive(primitiveView2.get<Properties>(0));
    checkPrimitive(primitiveView2.get<PropertiesView<false>>(0));

    // This should fail because the view is immutable
    // checkPrimitive(primitiveView2.get<PropertiesView<true>>(0));
}

TEST_F(PropertiesTests, PropertiesArrayPushback1) {
    PropertiesArray arr1;
    arr1.push_back(1);
    arr1.push_back(2);
    arr1.push_back(3);
    EXPECT_EQ(arr1.size(), 3);

    props.set<PropertiesArray>("arr1", arr1);

    EXPECT_TRUE(props.is_type_of<PropertiesArray>("arr1"));

    auto retrievedArr = props.get<PropertiesArrayView<true>>("arr1");
    EXPECT_EQ(retrievedArr.size(), 3);
    EXPECT_EQ(retrievedArr.get<int>(0), 1);
    EXPECT_EQ(retrievedArr.get<int>(1), 2);
    EXPECT_EQ(retrievedArr.get<int>(2), 3);

    retrievedArr.push_back(4);
    EXPECT_EQ(retrievedArr.size(), 4);
    EXPECT_EQ(retrievedArr.get<int>(3), 4);

    retrievedArr.clear();
    EXPECT_EQ(retrievedArr.size(), 0);
    EXPECT_TRUE(retrievedArr.empty());

    retrievedArr.push_back<std::string>("string");
    retrievedArr.push_back(3.14);
    retrievedArr.push_back(true);

    EXPECT_EQ(retrievedArr.size(), 3);
    EXPECT_EQ(retrievedArr.get<std::string>(0), "string");
    EXPECT_DOUBLE_EQ(retrievedArr.get<double>(1), 3.14);
    EXPECT_EQ(retrievedArr.get<bool>(2), true);
    EXPECT_THROW(retrievedArr.get<int>(3), Anyhow);

    auto updatedArr = props.get<PropertiesArray>("arr1");
    EXPECT_EQ(updatedArr.size(), 3);
    EXPECT_EQ(updatedArr.get<std::string>(0), "string");
    EXPECT_DOUBLE_EQ(updatedArr.get<double>(1), 3.14);
    EXPECT_EQ(updatedArr.get<bool>(2), true);
}

TEST_F(PropertiesTests, PropertiesArrayPushBack2) {
    PropertiesArray arr;
    arr.push_back(1);
    arr.push_back(2);

    EXPECT_THROW(arr.set<int>(2, 3), Anyhow);
    EXPECT_THROW(arr.set<int>(10, 3), Anyhow);

    for (int i = 0; i < 1000; ++i)
        arr.push_back(i);
    EXPECT_EQ(arr.size(), 1002);
}

TEST_F(PropertiesTests, PropertiesArrayViewSet) {
    PropertiesArray baseArr;
    baseArr.push_back(1);
    baseArr.push_back(2);
    baseArr.push_back(3);
    props.set<PropertiesArray>("base_arr", baseArr);

    // 1. Setting to PropertiesArray doesn't change its base
    {
        auto immutableView = props.get<PropertiesArrayView<false>>("base_arr");
        EXPECT_EQ(immutableView.size(), 3);

        // This should not compile
        // immutableView.set<int>(0, 10);

        auto baseArrAfter = props.get<PropertiesArray>("base_arr");
        EXPECT_EQ(baseArrAfter.size(), 3);
        EXPECT_EQ(baseArrAfter.get<int>(0), 1);
        EXPECT_EQ(baseArrAfter.get<int>(1), 2);
        EXPECT_EQ(baseArrAfter.get<int>(2), 3);
    }

    // 2. Setting to MutablePropertiesArray changes its base
    {
        auto mutableView = props.get<PropertiesArrayView<true>>("base_arr");
        EXPECT_EQ(mutableView.size(), 3);

        mutableView.set<int>(0, 10);
        mutableView.set<int>(1, 20);
        mutableView.push_back(30);

        auto baseArrAfter = props.get<PropertiesArray>("base_arr");
        EXPECT_EQ(baseArrAfter.size(), 4);
        EXPECT_EQ(baseArrAfter.get<int>(0), 10);
        EXPECT_EQ(baseArrAfter.get<int>(1), 20);
        EXPECT_EQ(baseArrAfter.get<int>(2), 3);
        EXPECT_EQ(baseArrAfter.get<int>(3), 30);
    }

    // 3. Setting MutablePropertiesArray to base then changing it doesn't affect its base
    {
        auto mutableView = props.get<PropertiesArrayView<true>>("base_arr");
        PropertiesArray newArr = mutableView.clone();

        newArr.set<int>(0, 100);
        newArr.push_back(200);

        auto baseArrAfter = props.get<PropertiesArray>("base_arr");
        EXPECT_EQ(baseArrAfter.size(), 4);
        EXPECT_EQ(baseArrAfter.get<int>(0), 10);
        EXPECT_EQ(baseArrAfter.get<int>(1), 20);
        EXPECT_EQ(baseArrAfter.get<int>(2), 3);
        EXPECT_EQ(baseArrAfter.get<int>(3), 30);

        EXPECT_EQ(newArr.size(), 5);
        EXPECT_EQ(newArr.get<int>(0), 100);
        EXPECT_EQ(newArr.get<int>(1), 20);
        EXPECT_EQ(newArr.get<int>(2), 3);
        EXPECT_EQ(newArr.get<int>(3), 30);
        EXPECT_EQ(newArr.get<int>(4), 200);
    }
}

TEST_F(PropertiesTests, PropertiesArrayComprehensive) {
    PropertiesArray arr;

    // Test pushing different types
    arr.push_back(42);
    arr.push_back(3.14);
    arr.push_back("Hello");
    arr.push_back(true);

    EXPECT_EQ(arr.size(), 4);

    EXPECT_EQ(arr.get<int>(0), 42);
    EXPECT_DOUBLE_EQ(arr.get<double>(1), 3.14);
    EXPECT_EQ(arr.get<std::string>(2), "Hello");
    EXPECT_EQ(arr.get<bool>(3), true);

    EXPECT_TRUE(arr.is_type_of<int>(0));
    EXPECT_TRUE(arr.is_type_of<double>(1));
    EXPECT_TRUE(arr.is_type_of<std::string>(2));
    EXPECT_TRUE(arr.is_type_of<bool>(3));

    EXPECT_THROW(arr.get<int>(1), Anyhow);
    EXPECT_THROW(arr.get<bool>(2), Anyhow);
    EXPECT_THROW(arr.get<std::string>(3), Anyhow);

    EXPECT_THROW(arr.get<int>(4), Anyhow);

    arr.clear();
    EXPECT_EQ(arr.size(), 0);
    EXPECT_TRUE(arr.empty());

    PropertiesArray nestedArr;
    nestedArr.push_back(1);
    nestedArr.push_back(2);
    arr.push_back(nestedArr);

    EXPECT_EQ(arr.size(), 1);
    auto retrievedNestedArr = arr.get<PropertiesArray>(0);
    EXPECT_EQ(retrievedNestedArr.size(), 2);
    EXPECT_EQ(retrievedNestedArr.get<int>(0), 1);
    EXPECT_EQ(retrievedNestedArr.get<int>(1), 2);

    Properties nestedProps;
    nestedProps.set<int>("key", 100);
    arr.push_back(nestedProps);

    EXPECT_EQ(arr.size(), 2);
    auto retrievedNestedProps = arr.get<Properties>(1);
    EXPECT_EQ(retrievedNestedProps.get<int>("key"), 100);

    props.set<PropertiesArray>("test_array", arr);
    EXPECT_TRUE(props.is_type_of<PropertiesArray>("test_array"));

    auto retrievedArr = props.get<PropertiesArray>("test_array");
    EXPECT_EQ(retrievedArr.size(), 2);

    auto arrView = props.get<PropertiesArrayView<true>>("test_array");
    EXPECT_EQ(arrView.size(), 2);
    auto nestedArrFromView = arrView.get<PropertiesArray>(0);
    EXPECT_EQ(nestedArrFromView.get<int>(0), 1);
    EXPECT_EQ(nestedArrFromView.get<int>(1), 2);

    arrView.push_back("New element");
    EXPECT_EQ(arrView.size(), 3);
    EXPECT_EQ(arrView.get<std::string>(2), "New element");

    auto updatedArr = props.get<PropertiesArray>("test_array");
    EXPECT_EQ(updatedArr.size(), 3);
    EXPECT_EQ(updatedArr.get<std::string>(2), "New element");
}

TEST_F(PropertiesTests, MutableArrayViewFromMutablePropertiesView) {
    Properties baseProps;
    PropertiesArray arr;
    arr.push_back(1);
    arr.push_back(2);
    arr.push_back(3);
    baseProps.set<PropertiesArray>("array", arr);
    props.set<Properties>("base_props", baseProps);

    // Get a MutablePropertiesView of the base properties
    auto mutablePropsView = props.get<MutablePropertiesView>("base_props");

    // Derive a MutableArrayView from the MutablePropertiesView
    auto mutableArrayView = mutablePropsView.get<PropertiesArrayView<true>>("array");

    // Verify initial state
    EXPECT_EQ(mutableArrayView.size(), 3);
    EXPECT_EQ(mutableArrayView.get<int>(0), 1);
    EXPECT_EQ(mutableArrayView.get<int>(1), 2);
    EXPECT_EQ(mutableArrayView.get<int>(2), 3);

    // Modify the array through the MutableArrayView
    mutableArrayView.set<int>(0, 10);
    mutableArrayView.set<int>(1, 20);
    mutableArrayView.push_back(30);

    // Verify changes in the MutableArrayView
    EXPECT_EQ(mutableArrayView.size(), 4);
    EXPECT_EQ(mutableArrayView.get<int>(0), 10);
    EXPECT_EQ(mutableArrayView.get<int>(1), 20);
    EXPECT_EQ(mutableArrayView.get<int>(2), 3);
    EXPECT_EQ(mutableArrayView.get<int>(3), 30);

    // Verify changes propagated to the base Properties
    auto updatedBaseProps = props.get<Properties>("base_props");
    auto updatedArray = updatedBaseProps.get<PropertiesArray>("array");
    EXPECT_EQ(updatedArray.size(), 4);
    EXPECT_EQ(updatedArray.get<int>(0), 10);
    EXPECT_EQ(updatedArray.get<int>(1), 20);
    EXPECT_EQ(updatedArray.get<int>(2), 3);
    EXPECT_EQ(updatedArray.get<int>(3), 30);

    // Make changes to the MutablePropertiesView
    mutablePropsView.set<int>("new_key", 100);

    // Verify changes in the base Properties
    auto finalBaseProps = props.get<Properties>("base_props");
    EXPECT_EQ(finalBaseProps.get<int>("new_key"), 100);

    // Verify the array changes are still present
    auto finalArray = finalBaseProps.get<PropertiesArray>("array");
    EXPECT_EQ(finalArray.size(), 4);
    EXPECT_EQ(finalArray.get<int>(0), 10);
    EXPECT_EQ(finalArray.get<int>(1), 20);
    EXPECT_EQ(finalArray.get<int>(2), 3);
    EXPECT_EQ(finalArray.get<int>(3), 30);
}
