#include <gtest/gtest.h>

#include "kira/Anyhow.h"
#include "kira/FileResolver.h"
#include "kira/Reflection.h"
#include "kira/SmallVector.h"

using namespace kira;

TEST(ReflectionTests, CustomStruct) {
    struct TestStruct {
        int a;
        float b;
        std::string c;
    };

    auto st1 = TestStruct{1, 2.0f, "3"};
    auto str = Serialize(st1);
    auto st2 = Deserialize<TestStruct>(str).value();

    EXPECT_EQ(st1.a, st2.a);
    EXPECT_EQ(st1.b, st2.b);
    EXPECT_EQ(st1.c, st2.c);
}

TEST(ReflectionTests, CustomStructWithVector) {
    struct TestStruct {
        int a;
        float b;
        std::vector<std::string> c;
    };

    auto st1 = TestStruct{1, 2.0f, {"3", "4", "5"}};
    auto str = Serialize(st1);
    auto st2 = Deserialize<TestStruct>(str).value();

    EXPECT_EQ(st1.a, st2.a);
    EXPECT_EQ(st1.b, st2.b);
    EXPECT_EQ(st1.c, st2.c);
}

TEST(ReflectionTests, VectorOfCustomStruct) {
    struct TestStruct {
        int a;
        float b;
        std::string c;
    };

    std::vector<TestStruct> st1 = {{1, 2.0f, "3"}, {4, 5.0f, "6"}};
    auto str = Serialize(st1);
    auto st2 = Deserialize<std::vector<TestStruct>>(str).value();

    EXPECT_EQ(st1.size(), st2.size());
    for (size_t i = 0; i < st1.size(); ++i) {
        EXPECT_EQ(st1[i].a, st2[i].a);
        EXPECT_EQ(st1[i].b, st2[i].b);
        EXPECT_EQ(st1[i].c, st2[i].c);
    }
}

TEST(ReflectionTests, Anyhow) {
    Anyhow anyhow{};
    auto str = Serialize(anyhow);
    auto st = Deserialize<Anyhow>(str).value();
    EXPECT_STREQ(anyhow.what(), st.what());
}

TEST(ReflectionTests, FileResolver) {
    FileResolver fr;
    fr.append("test1");
    auto str = Serialize(fr);
    auto st = Deserialize<FileResolver>(str).value();
    EXPECT_EQ(fr.size(), st.size());
}

TEST(ReflectionTests, SmallVector) {
    SmallVector<int> svec{1, 2, 3, 4, 5};
    auto str = Serialize(svec);
    auto st = Deserialize<SmallVector<int>>(str).value();

    EXPECT_EQ(svec.size(), st.size());
    for (size_t i = 0; i < svec.size(); ++i)
        EXPECT_EQ(svec[i], st[i]);
}

TEST(ReflectionTests, SmallVectorNoCopy) {
    using counter_t = std::shared_ptr<bool>;
    std::vector<counter_t> vec;
    vec.reserve(5);
    for (int i = 0; i < 5; ++i)
        vec.emplace_back(std::make_shared<bool>(true));

    SmallVector<counter_t> svec{vec};
    for (auto &i : svec)
        EXPECT_EQ(i.use_count(), 2);
    svec.clear();
    for (auto &i : vec)
        EXPECT_EQ(i.use_count(), 1);

    SmallVector<counter_t> svec2{std::move(vec)};
    for (auto &i : svec2)
        EXPECT_EQ(i.use_count(), 1);

    using counter2_t = std::unique_ptr<bool>;
    std::vector<counter2_t> vec2;
    vec2.reserve(5);
    for (int i = 0; i < 5; ++i)
        vec2.emplace_back(std::make_unique<bool>(true));
    SmallVector<counter2_t> svec3{std::move(vec2)};
    EXPECT_EQ(svec3.size(), 5);

    std::vector<counter2_t> vec3 = std::move(svec3).reflection();
    EXPECT_EQ(vec3.size(), 5);
}

TEST(ReflectionTests, SmallVectorOfCustomStruct) {
    struct TestStruct {
        int a;
        float b;
        std::string c;
    };

    SmallVector<TestStruct> st1 = {{1, 2.0f, "3"}, {4, 5.0f, "6"}};
    auto str = Serialize(st1);
    auto st2 = Deserialize<SmallVector<TestStruct>>(str).value();

    EXPECT_EQ(st1.size(), st2.size());
    for (size_t i = 0; i < st1.size(); ++i) {
        EXPECT_EQ(st1[i].a, st2[i].a);
        EXPECT_EQ(st1[i].b, st2[i].b);
        EXPECT_EQ(st1[i].c, st2[i].c);
    }
}

TEST(ReflectionTests, CustomStructWithSmallVector) {
    struct TestStruct {
        int a;
        float b;
        SmallVector<std::string> c;
    };

    auto st1 = TestStruct{1, 2.0f, {"3", "4", "5"}};
    auto str = Serialize(st1);
    LogWarn("display it here for you: {}", str);
    auto st2 = Deserialize<TestStruct>(str).value();

    EXPECT_EQ(st1.a, st2.a);
    EXPECT_EQ(st1.b, st2.b);
    EXPECT_EQ(st1.c, st2.c);
}
