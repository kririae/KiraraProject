#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "Core/Math.h"

namespace {
using DynamicMatrixd = krd::Matrix<double, Eigen::Dynamic, Eigen::Dynamic>;

kira::Properties ParseProps(std::string_view source) {
    return kira::Properties{toml::parse(source), source};
}

template <typename T>
void ExpectCannotReadAsMatrix(std::string_view source, std::string_view expectedMsg = {}) {
    auto props = ParseProps(source);
    EXPECT_FALSE(props.is_type_of<T>("m"));
    if (!expectedMsg.empty()) {
        try {
            props.get<T>("m");
            FAIL() << "Expected kira::Anyhow";
        } catch (kira::Anyhow const &e) {
            EXPECT_THAT(e.what(), testing::HasSubstr(expectedMsg));
        }
    } else {
        EXPECT_THROW((void)props.get<T>("m"), kira::Anyhow);
    }
}
} // namespace

TEST(MathTests, SerializesFixedMatrixAsNestedArray) {
    krd::Matrix2d mat;
    mat << 1.0, 2.0, 3.0, 4.0;

    kira::Properties props;
    props.set("m", mat);

    auto rows = props.get_array_view("m");
    ASSERT_EQ(rows.size(), 2);

    auto row0 = rows.get_array_view(0);
    auto row1 = rows.get_array_view(1);
    ASSERT_EQ(row0.size(), 2);
    ASSERT_EQ(row1.size(), 2);

    EXPECT_DOUBLE_EQ(row0.get<double>(0), 1.0);
    EXPECT_DOUBLE_EQ(row0.get<double>(1), 2.0);
    EXPECT_DOUBLE_EQ(row1.get<double>(0), 3.0);
    EXPECT_DOUBLE_EQ(row1.get<double>(1), 4.0);
}

TEST(MathTests, DeserializesFixedMatrix) {
    auto props = ParseProps("m = [[1.0, 2.0], [3.0, 4.0]]");

    auto mat = props.get<krd::Matrix2d>("m");

    EXPECT_EQ(mat.rows(), 2);
    EXPECT_EQ(mat.cols(), 2);
    EXPECT_DOUBLE_EQ(mat(0, 0), 1.0);
    EXPECT_DOUBLE_EQ(mat(0, 1), 2.0);
    EXPECT_DOUBLE_EQ(mat(1, 0), 3.0);
    EXPECT_DOUBLE_EQ(mat(1, 1), 4.0);
}

TEST(MathTests, DeserializesDynamicMatrixWithRuntimeDimensions) {
    auto props = ParseProps("m = [[1.0, 2.0, 3.0], [4.0, 5.0, 6.0]]");

    auto mat = props.get<DynamicMatrixd>("m");

    EXPECT_EQ(mat.rows(), 2);
    EXPECT_EQ(mat.cols(), 3);
    EXPECT_DOUBLE_EQ(mat(0, 0), 1.0);
    EXPECT_DOUBLE_EQ(mat(0, 1), 2.0);
    EXPECT_DOUBLE_EQ(mat(0, 2), 3.0);
    EXPECT_DOUBLE_EQ(mat(1, 0), 4.0);
    EXPECT_DOUBLE_EQ(mat(1, 1), 5.0);
    EXPECT_DOUBLE_EQ(mat(1, 2), 6.0);
}

TEST(MathTests, SerializesAndDeserializesVectorAsColumnMatrix) {
    krd::Vector3f vec{1.0F, 2.0F, 3.0F};

    kira::Properties props;
    props.set("m", vec);

    auto rows = props.get_array_view("m");
    ASSERT_EQ(rows.size(), 3);
    EXPECT_FLOAT_EQ(rows.get_array_view(0).get<float>(0), 1.0F);
    EXPECT_FLOAT_EQ(rows.get_array_view(1).get<float>(0), 2.0F);
    EXPECT_FLOAT_EQ(rows.get_array_view(2).get<float>(0), 3.0F);

    auto parsed = props.get<krd::Vector3f>("m");
    EXPECT_EQ(parsed.rows(), 3);
    EXPECT_EQ(parsed.cols(), 1);
    EXPECT_FLOAT_EQ(parsed(0), 1.0F);
    EXPECT_FLOAT_EQ(parsed(1), 2.0F);
    EXPECT_FLOAT_EQ(parsed(2), 3.0F);
}

TEST(MathTests, RejectsNonArrayMatrixNode) {
    ExpectCannotReadAsMatrix<krd::Matrix2d>("m = 1", "Expected an array, but got integer");
}

TEST(MathTests, RejectsEmptyMatrixArray) {
    ExpectCannotReadAsMatrix<DynamicMatrixd>("m = []", "Expected a non-empty array");
}

TEST(MathTests, RejectsWrongFixedRowCount) {
    ExpectCannotReadAsMatrix<krd::Matrix2d>("m = [[1.0, 2.0]]", "Expected 2 row(s), but got 1");
}

TEST(MathTests, RejectsFirstRowThatIsNotArray) {
    ExpectCannotReadAsMatrix<DynamicMatrixd>("m = [1.0]", "Expected an array for row 0, but got float");
}

TEST(MathTests, RejectsEmptyFirstRow) {
    ExpectCannotReadAsMatrix<DynamicMatrixd>("m = [[]]", "Row 0 is empty");
}

TEST(MathTests, RejectsWrongFixedColumnCount) {
    ExpectCannotReadAsMatrix<krd::Matrix2d>("m = [[1.0], [2.0]]",
                                            "Expected 2 column(s), but row 0 has 1");
}

TEST(MathTests, RejectsLaterRowThatIsNotArray) {
    ExpectCannotReadAsMatrix<DynamicMatrixd>("m = [[1.0], 2.0]",
                                             "Expected an array for row 1, but got float");
}

TEST(MathTests, RejectsRaggedRows) {
    ExpectCannotReadAsMatrix<DynamicMatrixd>("m = [[1.0, 2.0], [3.0]]",
                                             "Row 1 has 1 column(s), but expected 2");
}

TEST(MathTests, RejectsElementWithWrongType) {
    ExpectCannotReadAsMatrix<krd::Matrix2d>("m = [[1.0, 'x'], [3.0, 4.0]]",
                                            "Expected a matrix element, but got string at [0, 1]");
}
