// Encodings (tasks A025): base64 per RFC 4648, FNV-1a per its published
// parameters, PNG per its signature, and the audio ring's contract.

#include "support.hpp"

using namespace cutenes::api;

TEST(ApiMedia, Base64MatchesRfc4648Vectors)
{
    // RFC 4648 section 10 test vectors.
    const std::pair<const char*, const char*> vectors[] = {{"", ""}, {"f", "Zg=="},
        {"fo", "Zm8="}, {"foo", "Zm9v"}, {"foob", "Zm9vYg=="}, {"fooba", "Zm9vYmE="},
        {"foobar", "Zm9vYmFy"}};
    for (const auto& [plain, encoded] : vectors) {
        const std::string p = plain;
        const std::vector<std::uint8_t> bytes(p.begin(), p.end());
        EXPECT_EQ(media::base64Encode(bytes), encoded);
        EXPECT_EQ(media::base64Decode(encoded), bytes);
    }
    EXPECT_FALSE(media::base64Decode("Zg="));
    EXPECT_FALSE(media::base64Decode("Z==="));
    EXPECT_FALSE(media::base64Decode("Zm9v!A=="));
}

TEST(ApiMedia, Fnv1aMatchesPublishedVectors)
{
    // FNV-1a 32-bit reference values (Noll, "FNV test vectors").
    EXPECT_EQ(media::fnv1a32(nullptr, 0), 0x811C9DC5u);
    const std::uint8_t a[] = {'a'};
    EXPECT_EQ(media::fnv1a32(a, 1), 0xE40C292Cu);
    const std::string foobar = "foobar";
    EXPECT_EQ(media::fnv1a32(reinterpret_cast<const std::uint8_t*>(foobar.data()), 6),
        0xBF9CF968u);
    EXPECT_EQ(media::hashHex(0x0000ABCDu), "0000abcd");
}

TEST(ApiMedia, PngHasSignatureAndDimensions)
{
    std::vector<std::uint32_t> pixels(media::kFramePixels, 0x00FF8000u);
    const auto png = media::encodePng(pixels.data());
    ASSERT_GT(png.size(), 33u);
    const std::uint8_t signature[] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    EXPECT_TRUE(std::equal(std::begin(signature), std::end(signature), png.begin()));
    // IHDR: big-endian width and height at offsets 16 and 20.
    EXPECT_EQ((png[16] << 24) | (png[17] << 16) | (png[18] << 8) | png[19], 256);
    EXPECT_EQ((png[20] << 24) | (png[21] << 16) | (png[22] << 8) | png[23], 240);
}

TEST(ApiMedia, AudioRingReturnsWholeChunksAndContinues)
{
    media::AudioRing ring(100); // capacity 1000 samples
    std::vector<std::int16_t> chunk(300, 7);
    ring.onSamples(chunk.data(), chunk.size(), 1000);
    ring.onSamples(chunk.data(), chunk.size(), 2000);
    ring.onSamples(chunk.data(), chunk.size(), 3000);

    auto r = ring.read(0, 700);
    EXPECT_EQ(r.pcm.size(), 600u);
    EXPECT_EQ(r.startMasterCycle, 1000u);
    EXPECT_TRUE(r.truncated);
    EXPECT_EQ(r.next, 3000u);
    r = ring.read(r.next, 700);
    EXPECT_EQ(r.pcm.size(), 300u);
    EXPECT_FALSE(r.truncated);
    EXPECT_EQ(ring.read(r.next, 700).pcm.size(), 0u);

    // Over capacity the oldest chunk goes, and a reader behind it is told.
    ring.onSamples(chunk.data(), chunk.size(), 4000);
    EXPECT_TRUE(ring.read(0, 100).dropped);
    EXPECT_EQ(ring.read(0, 1000).startMasterCycle, 2000u);
    EXPECT_FALSE(ring.read(2000, 100).dropped);
}
