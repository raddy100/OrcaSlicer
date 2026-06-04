#include <catch2/catch_all.hpp>

#include <set>
#include <vector>

// ---------------------------------------------------------------------------
// The two functions under test are the pure, GUI-free pixel<->layer mapping
// helpers defined in src/slic3r/GUI/DrawLayerSlider.cpp (namespace
// Slic3r::GUI). libslic3r_tests does NOT include the GUI headers (which would
// drag in wx), so we forward-declare the exact CONTRACT prototypes here. The
// production translation unit DrawLayerSlider.cpp is compiled into this test
// target via tests/libslic3r/CMakeLists.txt, so these resolve at link time to
// the real implementation (no reimplementation, no production edits).
// ---------------------------------------------------------------------------
namespace Slic3r { namespace GUI {
int layer_slider_y_to_index(int y, int height_px, int top, int bottom, int n);
int layer_slider_index_to_y(int index, int height_px, int top, int bottom, int n);
}} // namespace Slic3r::GUI

using Slic3r::GUI::layer_slider_y_to_index;
using Slic3r::GUI::layer_slider_index_to_y;

// Standard track geometry shared by most cases (matches 02_TESTING.md §3.1).
namespace {
constexpr int H = 200; // widget height in px
constexpr int T = 10;  // top margin
constexpr int B = 10;  // bottom margin
} // namespace

// ---------------------------------------------------------------------------
// §3.1 y_to_index — boundary & clamping
// ---------------------------------------------------------------------------
TEST_CASE("DrawLayerSlider: y_to_index boundary and clamping", "[DrawLayerSlider]")
{
    const int n = 10;

    // 1) Click at very top of track -> last layer (n-1).
    REQUIRE(layer_slider_y_to_index(10, H, T, B, n) == 9);

    // 2) Click at very bottom of track -> layer 0.
    REQUIRE(layer_slider_y_to_index(190, H, T, B, n) == 0);

    // 3) Overscroll above the track -> clamped to last layer.
    REQUIRE(layer_slider_y_to_index(-50, H, T, B, n) == 9);

    // 4) Overscroll below the track -> clamped to layer 0.
    REQUIRE(layer_slider_y_to_index(999, H, T, B, n) == 0);

    // 5) Exact middle -> 5 (half-away-from-zero rounding, per the contract).
    REQUIRE(layer_slider_y_to_index(100, H, T, B, n) == 5);
}

// ---------------------------------------------------------------------------
// §3.2 n == 1 degenerate case
// ---------------------------------------------------------------------------
TEST_CASE("DrawLayerSlider: n == 1 maps any y to layer 0", "[DrawLayerSlider]")
{
    const int n = 1;

    // For any y, the only layer is 0.
    REQUIRE(layer_slider_y_to_index(T, H, T, B, n) == 0);          // top
    REQUIRE(layer_slider_y_to_index(H / 2, H, T, B, n) == 0);      // middle
    REQUIRE(layer_slider_y_to_index(H - B, H, T, B, n) == 0);      // bottom
    REQUIRE(layer_slider_y_to_index(-100, H, T, B, n) == 0);       // overscroll up
    REQUIRE(layer_slider_y_to_index(9999, H, T, B, n) == 0);       // overscroll down

    // index_to_y(0) must land inside the usable track [top, height - bottom],
    // with no division by zero.
    const int y0 = layer_slider_index_to_y(0, H, T, B, n);
    REQUIRE(y0 >= T);
    REQUIRE(y0 <= H - B);
}

// ---------------------------------------------------------------------------
// §3.3 n == 0 empty case
// ---------------------------------------------------------------------------
TEST_CASE("DrawLayerSlider: n == 0 returns -1 with no crash", "[DrawLayerSlider]")
{
    const int n = 0;

    // No active layer; must not divide by zero or crash for any y.
    REQUIRE(layer_slider_y_to_index(T, H, T, B, n) == -1);
    REQUIRE(layer_slider_y_to_index(H / 2, H, T, B, n) == -1);
    REQUIRE(layer_slider_y_to_index(H - B, H, T, B, n) == -1);
    REQUIRE(layer_slider_y_to_index(-50, H, T, B, n) == -1);
    REQUIRE(layer_slider_y_to_index(999, H, T, B, n) == -1);
}

// ---------------------------------------------------------------------------
// §3.4 Round-trip stability: y_to_index(index_to_y(i)) == i for every i.
// ---------------------------------------------------------------------------
TEST_CASE("DrawLayerSlider: index<->y round-trip is exact", "[DrawLayerSlider]")
{
    for (int n : {2, 5, 10, 50}) {
        for (int i = 0; i < n; ++i) {
            DYNAMIC_SECTION("n=" << n << " i=" << i)
            {
                const int y    = layer_slider_index_to_y(i, H, T, B, n);
                const int back = layer_slider_y_to_index(y, H, T, B, n);
                REQUIRE(back == i);
            }
        }
    }
}

// ---------------------------------------------------------------------------
// §3.5 Monotonicity: sweeping y from the bottom of the track up to the top
// yields a non-decreasing index sequence that starts at 0, ends at n-1, and
// visits every layer at least once (no gaps, no reversals).
// ---------------------------------------------------------------------------
TEST_CASE("DrawLayerSlider: y sweep is monotonic non-decreasing and covers all layers", "[DrawLayerSlider]")
{
    for (int n : {2, 5, 10, 50}) {
        DYNAMIC_SECTION("n=" << n)
        {
            const int top_y    = T;         // smallest y -> layer n-1
            const int bottom_y = H - B;     // largest y  -> layer 0

            std::vector<int> seq;
            seq.reserve(bottom_y - top_y + 1);
            // Sweep from the bottom of the track (layer 0) up to the top (layer n-1).
            for (int y = bottom_y; y >= top_y; --y)
                seq.push_back(layer_slider_y_to_index(y, H, T, B, n));

            REQUIRE(!seq.empty());

            // Starts at layer 0, ends at layer n-1.
            REQUIRE(seq.front() == 0);
            REQUIRE(seq.back() == n - 1);

            // Non-decreasing throughout (no reversal jitter during a scrub).
            bool non_decreasing = true;
            for (size_t k = 1; k < seq.size(); ++k) {
                if (seq[k] < seq[k - 1]) {
                    non_decreasing = false;
                    break;
                }
            }
            REQUIRE(non_decreasing);

            // Every index in [0, n-1] is visited at least once (no skipped layers).
            std::set<int> visited(seq.begin(), seq.end());
            REQUIRE((int) visited.size() == n);
            REQUIRE(*visited.begin() == 0);
            REQUIRE(*visited.rbegin() == n - 1);
        }
    }
}
