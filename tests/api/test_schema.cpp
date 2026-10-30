// The argument validator (tasks A023): JSON Schema 2020-12 semantics for the
// keyword subset the catalog uses.

#include "support.hpp"

#include <cutenes/api/schema.hpp>

using namespace cutenes::api;

TEST(ApiSchema, TypesBoundsAndRequired)
{
    const Json sch = schema::object({{"n", schema::integer(1, 10)}, {"s", schema::string()}}, {"n"});
    EXPECT_FALSE(validate(sch, {{"n", 1}}));
    EXPECT_FALSE(validate(sch, {{"n", 10}, {"s", "x"}}));
    EXPECT_EQ(validate(sch, Json::object())->path, "/n");
    EXPECT_EQ(validate(sch, {{"n", 0}})->path, "/n");
    EXPECT_EQ(validate(sch, {{"n", 11}})->path, "/n");
    EXPECT_EQ(validate(sch, {{"n", 1.5}})->path, "/n");
    EXPECT_EQ(validate(sch, {{"n", "1"}})->path, "/n");
    EXPECT_EQ(validate(sch, {{"n", 1}, {"extra", 1}})->path, "/extra");
    EXPECT_EQ(validate(sch, Json::array())->path, "");
}

TEST(ApiSchema, UnsignedAndNegativeBounds)
{
    const Json sch = schema::object({{"v", schema::u8()}});
    EXPECT_TRUE(validate(sch, {{"v", -1}}));
    EXPECT_TRUE(validate(sch, {{"v", std::uint64_t{1} << 63}}));
    EXPECT_FALSE(validate(sch, {{"v", std::uint64_t{255}}}));
}

TEST(ApiSchema, EnumPatternArraysAndBase64)
{
    const Json sch = schema::object({{"e", schema::stringEnum({"a", "b"})},
        {"p", schema::pattern("^[a-z]{1,3}$")}, {"list", schema::array(schema::u8(), 2)},
        {"b", schema::bytes(2)}, {"buttons", schema::buttons()}});
    EXPECT_FALSE(validate(sch, {{"e", "a"}, {"p", "abc"}, {"list", {1, 2}}, {"b", "AAA="},
                                   {"buttons", {"A", "Start"}}}));
    EXPECT_EQ(validate(sch, {{"e", "c"}})->path, "/e");
    EXPECT_EQ(validate(sch, {{"p", "abcd"}})->path, "/p");
    EXPECT_EQ(validate(sch, {{"list", {1, 2, 3}}})->path, "/list");
    EXPECT_EQ(validate(sch, {{"list", {1, 256}}})->path, "/list/1");
    EXPECT_EQ(validate(sch, {{"b", "AAAA"}})->path, "/b");  // 3 bytes > 2
    EXPECT_EQ(validate(sch, {{"b", "A*=="}})->path, "/b");  // not base64
    EXPECT_EQ(validate(sch, {{"buttons", {"Turbo"}}})->path, "/buttons/0");
}

TEST(ApiSchema, ShapeCheckRejectsOpenObjectsAndUnknownKeywords)
{
    EXPECT_FALSE(checkSchemaShape(schema::object({{"a", schema::u8()}}, {"a"})));
    EXPECT_TRUE(checkSchemaShape({{"type", "object"}, {"properties", Json::object()}}));
    EXPECT_TRUE(checkSchemaShape(schema::object({}, {"missing"})));
    EXPECT_TRUE(checkSchemaShape({{"type", "object"}, {"properties", Json::object()},
        {"additionalProperties", false}, {"oneOf", Json::array()}}));
    EXPECT_TRUE(checkSchemaShape(schema::u8()));
}
