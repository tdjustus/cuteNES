// The frozen surface (tasks A027, research R9): the headless catalog must
// equal tests/api/catalog.golden.json. A change means a deliberate surface
// version bump and a regenerated golden:
//   CUTENES_UPDATE_GOLDEN=1 ctest --test-dir build -R ApiCatalogGolden

#include "support.hpp"

#include <cstdlib>
#include <fstream>

using namespace cutenes::api;
using namespace cutenes::api::test;

TEST(ApiCatalogGolden, CatalogMatchesTheGolden)
{
    Session s(testConfig());
    const Json catalog = {{"surface", surfaceVersion()}, {"commands", s.registry().catalog()}};
    const std::string path = sourceDir() + "/tests/api/catalog.golden.json";
    if (std::getenv("CUTENES_UPDATE_GOLDEN") != nullptr) {
        std::ofstream(path) << catalog.dump(2) << "\n";
        GTEST_SKIP() << "golden rewritten: " << path;
    }
    std::ifstream in(path);
    ASSERT_TRUE(in) << "missing " << path;
    const Json golden = Json::parse(in);
    if (golden != catalog) {
        EXPECT_NE(golden["surface"], catalog["surface"])
            << "the catalog changed without a surface version bump (research R9)";
        ADD_FAILURE() << "the catalog differs from " << path
                      << "; bump kSurfaceMajor/kSurfaceMinor and regenerate it";
    }
}
