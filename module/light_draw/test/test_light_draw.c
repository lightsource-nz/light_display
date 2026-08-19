// geometry checks for light_draw's arc, rounded-rect and filled-circle paths.
//
// pure geometry, so none of this needs hardware -- and the failure modes it looks for (gaps
// in an arc, joins that miss by a pixel, fill that escapes its outline) are exactly the ones
// that would otherwise only show up as a visibly broken frame on a panel, at which point the
// only tool left is squinting at it.
//
// RUN AS: ctest, or this binary directly. With no argument it runs everything; with a case
// name it runs just that one, which is how CTest registers them individually so a failure
// names itself instead of being one line in a wall of output.
//
// THE TOLERANCES HERE ARE MEASURED, NOT GUESSED. Several are set just above what the
// implementation actually achieves, and the numbers are quoted at each site. That matters
// because the first version of this file was written with round-number tolerances and
// silently accepted three separate broken implementations -- see test/mutants.ps1, which is
// how they were found and how any change to these tolerances should be re-checked.
#include <light_draw.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

// defined here rather than using M_PI, which lives behind _USE_MATH_DEFINES on some
// toolchains and _GNU_SOURCE on others -- this file needs no more portability surface than
// it already has
#define TEST_PI 3.14159265358979323846

// light_core's framework.c refers to `this_app`, which only an application defines -- so a
// test binary linking the real library has to be one. it is never started: main() below calls
// the drawing code directly and light_framework_run() is never reached, so these two are
// there to satisfy the linker and to keep the test honest about what it links against. the
// alternative was a stubbed light.h, which would test the geometry against a header that is
// not the one the firmware compiles with
static void _test_app_event(const struct light_module *module, uint8_t event, void *arg) {}
static uint8_t _test_app_main(struct light_application *app) { return LF_STATUS_RUN; }
Light_Application_Define(test_light_draw, _test_app_event, _test_app_main, &light_draw, &light_core);

static int failures;

#define CHECK(cond, ...) do { \
        if(!(cond)) { \
                printf("FAIL %s:%d: ", __FILE__, __LINE__); \
                printf(__VA_ARGS__); printf("\n"); \
                failures++; \
        } \
} while(0)

// 16bpp so foreground (white) and background (black) are unambiguous per pixel
static light_draw_context_t *make_ctx(int w, int h)
{
        light_draw_context_t *ctx = light_draw_context_create((const uint8_t *)"test", w, h, 16);
        light_draw_draw_clear(ctx);
        return ctx;
}
static int lit(const light_draw_context_t *ctx, int x, int y)
{
        if(x < 0 || y < 0 || x >= ctx->dim_x || y >= ctx->dim_y)
                return 0;
        size_t off = ((size_t)y * ctx->phys_dim_x + x) * 2;
        return ctx->buffer[off] || ctx->buffer[off + 1];
}
// signed distance to a rounded rectangle's boundary, positive outside. the exact oracle the
// outline is measured against -- checking a handful of individual pixels turned out to accept
// a corner arc displaced by a whole pixel, which this does not
static double sdf_round_rect(double px, double py, double cx, double cy,
                             double hx, double hy, double r)
{
        double qx = fabs(px - cx) - (hx - r);
        double qy = fabs(py - cy) - (hy - r);
        double mx = qx > 0 ? qx : 0, my = qy > 0 ? qy : 0;
        double inside = (qx > qy ? qx : qy);
        if(inside > 0) inside = 0;
        return sqrt(mx*mx + my*my) + inside - r;
}
static int count_lit(const light_draw_context_t *ctx)
{
        int n = 0;
        for(int y = 0; y < ctx->dim_y; y++)
                for(int x = 0; x < ctx->dim_x; x++)
                        n += lit(ctx, x, y) ? 1 : 0;
        return n;
}
// the drawn pixels must form ONE 8-connected component. an earlier version of this only
// looked for pixels with no lit neighbour at all, which a mutation test showed was far too
// weak: a gap in the middle of a long arc leaves the pixels on either side of it still
// touching their own neighbours, so nothing is ever isolated. counting components is what
// actually detects a break
static void check_connected(const light_draw_context_t *ctx, const char *what)
{
        int w = ctx->dim_x, h = ctx->dim_y;
        int total = count_lit(ctx);
        if(total <= 1)
                return;

        uint8_t *seen = calloc((size_t)w * h, 1);
        int *stack = malloc(sizeof(int) * (size_t)w * h);
        int components = 0, reached = 0;

        for(int sy = 0; sy < h && components < 2; sy++) {
                for(int sx = 0; sx < w && components < 2; sx++) {
                        if(!lit(ctx, sx, sy) || seen[sy * w + sx])
                                continue;
                        components++;
                        int top = 0;
                        stack[top++] = sy * w + sx;
                        seen[sy * w + sx] = 1;
                        while(top > 0) {
                                int idx = stack[--top];
                                int x = idx % w, y = idx / w;
                                reached++;
                                for(int dy = -1; dy <= 1; dy++) {
                                        for(int dx = -1; dx <= 1; dx++) {
                                                int nx = x + dx, ny = y + dy;
                                                if(nx < 0 || ny < 0 || nx >= w || ny >= h)
                                                        continue;
                                                if(!lit(ctx, nx, ny) || seen[ny * w + nx])
                                                        continue;
                                                seen[ny * w + nx] = 1;
                                                stack[top++] = ny * w + nx;
                                        }
                                }
                        }
                }
        }
        CHECK(components == 1,
              "%s: %d pixels form more than one component (first has %d) -- the arc has a gap",
              what, total, reached);
        free(seen);
        free(stack);
}

// a closed outline has no loose ends: every pixel on it has at least two 8-neighbours.
// connectivity alone cannot see a missing corner -- delete one quarter arc from a rounded
// rectangle and the remaining edges are still reachable from each other the long way round,
// so the component count stays at one. the two pixels either side of the hole are the only
// evidence, and this is what looks at them
static void check_closed(const light_draw_context_t *ctx, const char *what)
{
        for(int y = 0; y < ctx->dim_y; y++) {
                for(int x = 0; x < ctx->dim_x; x++) {
                        if(!lit(ctx, x, y))
                                continue;
                        int neighbours = 0;
                        for(int dy = -1; dy <= 1; dy++)
                                for(int dx = -1; dx <= 1; dx++)
                                        if((dx || dy) && lit(ctx, x + dx, y + dy))
                                                neighbours++;
                        if(neighbours < 2) {
                                CHECK(0, "%s: loose end at (%d,%d) -- the outline is not closed",
                                      what, x, y);
                                return;
                        }
                }
        }
}

// --- 1. arcs have no gaps, across radii and spans ---
static void test_arc_no_gaps(void)
{
        int radii[] = { 2, 3, 5, 8, 13, 20, 33, 54, 87, 120 };
        int spans[] = { 5, 17, 45, 90, 137, 180, 271, 359, 360 };
        int starts[] = { 0, 7, 45, 90, 173, 260, 315, 359 };

        for(size_t ri = 0; ri < sizeof(radii)/sizeof(*radii); ri++) {
                int r = radii[ri];
                int dim = 2 * r + 8;
                for(size_t si = 0; si < sizeof(spans)/sizeof(*spans); si++) {
                        for(size_t ai = 0; ai < sizeof(starts)/sizeof(*starts); ai++) {
                                light_draw_context_t *ctx = make_ctx(dim, dim);
                                light_draw_point2d c = { dim / 2, dim / 2 };
                                int start = starts[ai];
                                int end = (start + spans[si]) % 360;
                                light_draw_draw_arc(ctx, c, r, start, end);
                                char what[96];
                                snprintf(what, sizeof(what), "arc r=%d %d..%d", r, start, end);
                                check_connected(ctx, what);
                                // and it must actually draw something proportional to its length
                                int n = count_lit(ctx);
                                int expect = (int)(r * spans[si] * TEST_PI / 180.0);
                                CHECK(n >= expect / 2 && n <= expect * 2 + 8,
                                      "%s: %d pixels, expected around %d", what, n, expect);
                                free(ctx->buffer);
                                free(ctx);
                        }
                }
        }
}

// --- 2. every arc pixel lies on the circle, and within the requested angular span ---
static void test_arc_accuracy(void)
{
        int radii[] = { 4, 11, 29, 64, 120 };
        for(size_t ri = 0; ri < sizeof(radii)/sizeof(*radii); ri++) {
                int r = radii[ri];
                int dim = 2 * r + 8;
                for(int start = 0; start < 360; start += 23) {
                        int span = 70;
                        int end = (start + span) % 360;
                        light_draw_context_t *ctx = make_ctx(dim, dim);
                        light_draw_point2d c = { dim / 2, dim / 2 };
                        light_draw_draw_arc(ctx, c, r, start, end);
                        for(int y = 0; y < dim; y++) {
                                for(int x = 0; x < dim; x++) {
                                        if(!lit(ctx, x, y))
                                                continue;
                                        double dx = x - c.x, dy = y - c.y;
                                        double d = sqrt(dx*dx + dy*dy);
                                        CHECK(fabs(d - r) < 1.0,
                                              "arc r=%d %d..%d: pixel (%d,%d) is %.2f from centre",
                                              r, start, end, x, y, d);
                                        // y grows downward, so atan2(dy,dx) IS the clockwise
                                        // screen angle with no sign flip
                                        double a = atan2(dy, dx) * 180.0 / TEST_PI;
                                        if(a < 0) a += 360;
                                        double rel = a - start;
                                        if(rel < 0) rel += 360;
                                        // one pixel of slack at each end, scaled to the angle
                                        // one pixel subtends at this radius
                                        double slack = 60.0 / r + 1.0;
                                        CHECK(rel <= span + slack || rel >= 360 - slack,
                                              "arc r=%d %d..%d: pixel (%d,%d) at %.1f deg, %.1f into span",
                                              r, start, end, x, y, a, rel);
                                }
                        }
                        free(ctx->buffer);
                        free(ctx);
                }
        }
}

// --- 3. endpoints land where cos/sin say, pinning the angle convention ---
static void test_arc_endpoints(void)
{
        light_draw_context_t *ctx = make_ctx(101, 101);
        light_draw_point2d c = { 50, 50 };
        int r = 40;

        // 0 is right, 90 is DOWN, 180 is left, 270 is up
        struct { int deg, x, y; } cases[] = {
                {   0, 90, 50 },
                {  90, 50, 90 },
                { 180, 10, 50 },
                { 270, 50, 10 },
        };
        for(size_t i = 0; i < sizeof(cases)/sizeof(*cases); i++) {
                light_draw_draw_clear(ctx);
                // a 1-degree sliver, so only the endpoint neighbourhood is drawn
                light_draw_draw_arc(ctx, c, r, cases[i].deg, cases[i].deg + 1);
                CHECK(lit(ctx, cases[i].x, cases[i].y),
                      "arc at %d deg should reach (%d,%d)",
                      cases[i].deg, cases[i].x, cases[i].y);
        }
        free(ctx->buffer);
        free(ctx);

        // the FAR end matters as much as the near one, and only shows up at a radius where
        // the step count is large: an implementation that accumulates a truncated per-step
        // angle instead of recomputing from the endpoints finishes short by a fraction of a
        // degree, which is a pixel or more of shortfall once r passes about 100 -- and a
        // corner arc that stops short of its straight edge is exactly the visible defect
        // light_draw_draw_rect_rounded() must not have
        int big = 150;
        light_draw_context_t *wide = make_ctx(2 * big + 5, 2 * big + 5);
        light_draw_point2d wc = { big + 2, big + 2 };
        struct { int start, end, x, y; } ends[] = {
                {  10,  90, wc.x,        wc.y + big },   // ends pointing straight down
                { 200, 270, wc.x,        wc.y - big },   // ends pointing straight up
                { 100, 180, wc.x - big,  wc.y      },    // ends pointing left
                { 300,   0, wc.x + big,  wc.y      },    // wraps through 360, ends right
        };
        for(size_t i = 0; i < sizeof(ends)/sizeof(*ends); i++) {
                light_draw_draw_clear(wide);
                light_draw_draw_arc(wide, wc, big, ends[i].start, ends[i].end);
                CHECK(lit(wide, ends[i].x, ends[i].y),
                      "arc r=%d %d..%d should finish exactly on (%d,%d)",
                      big, ends[i].start, ends[i].end, ends[i].x, ends[i].y);
        }
        free(wide->buffer);
        free(wide);
}

// --- 4. rounded rect: corner arcs meet their straight edges, outline is one closed loop ---
static void test_rect_rounded_joins(void)
{
        struct { int w, h, r; } cases[] = {
                { 240, 280, 20 }, { 240, 280, 1 }, { 64, 128, 8 }, { 100, 100, 50 },
                { 100, 100, 90 },       // over-large: clamps to a circle
                { 40, 120, 60 },        // over-large on the short axis: clamps to a stadium
                { 31, 47, 7 }, { 200, 12, 5 },
        };
        for(size_t i = 0; i < sizeof(cases)/sizeof(*cases); i++) {
                int w = cases[i].w, h = cases[i].h, r = cases[i].r;
                light_draw_context_t *ctx = make_ctx(w, h);
                light_draw_point2d p0 = { 0, 0 }, p1 = { w - 1, h - 1 };
                light_draw_draw_rect_rounded(ctx, p0, p1, r, false);
                char what[96];
                snprintf(what, sizeof(what), "rounded rect %dx%d r=%d", w, h, r);
                check_connected(ctx, what);
                check_closed(ctx, what);

                int rr = r;
                if(rr > (w - 1) / 2) rr = (w - 1) / 2;
                if(rr > (h - 1) / 2) rr = (h - 1) / 2;
                // the straight edges are present at their midpoints...
                CHECK(lit(ctx, w / 2, 0), "%s: no top edge", what);
                CHECK(lit(ctx, w / 2, h - 1), "%s: no bottom edge", what);
                CHECK(lit(ctx, 0, h / 2), "%s: no left edge", what);
                CHECK(lit(ctx, w - 1, h / 2), "%s: no right edge", what);
                // ...and the corners themselves are clear, which is the whole point
                if(rr >= 3) {
                        CHECK(!lit(ctx, 0, 0), "%s: (0,0) should be cut away", what);
                        CHECK(!lit(ctx, w - 1, 0), "%s: top-right should be cut away", what);
                        CHECK(!lit(ctx, 0, h - 1), "%s: bottom-left should be cut away", what);
                        CHECK(!lit(ctx, w - 1, h - 1), "%s: bottom-right should be cut away", what);
                }
                // every lit pixel sits on the true boundary. the implementation measures at
                // worst 0.79 away (in the 100x100 r=50 case, where the shape is a circle and
                // every pixel is arc), so 1.0 is a real bound and not a rubber stamp -- a
                // corner arc displaced by a single pixel reaches 1.56 and is rejected
                double cx = (w - 1) / 2.0, cy = (h - 1) / 2.0;
                for(int y = 0; y < h; y++) {
                        for(int x = 0; x < w; x++) {
                                if(!lit(ctx, x, y))
                                        continue;
                                double d = fabs(sdf_round_rect(x, y, cx, cy, cx, cy, rr));
                                if(d > 1.0) {
                                        CHECK(0, "%s: pixel (%d,%d) is %.2f off the boundary",
                                              what, x, y, d);
                                        y = h; break;   // one report per case
                                }
                        }
                }
                free(ctx->buffer);
                free(ctx);
        }
}

// --- 5. filled circle against a brute-force oracle ---
static void test_circle_fill(void)
{
        for(int r = 1; r <= 60; r++) {
                int dim = 2 * r + 5;
                light_draw_context_t *ctx = make_ctx(dim, dim);
                light_draw_point2d c = { dim / 2, dim / 2 };
                light_draw_draw_circle(ctx, c, r, true);

                // the exact disc is the oracle, but the two disagree on boundary cells the
                // midpoint algorithm rounds the other way, so counting mismatches needs a
                // tolerance -- and a tolerance loose enough to allow that turned out to be
                // loose enough to accept every span being a pixel too wide. these two
                // one-sided distance bounds are sharp instead: measured worst case is 0.38
                // outside, and a span one pixel too wide reaches 0.83
                for(int y = 0; y < dim; y++) {
                        for(int x = 0; x < dim; x++) {
                                int dx = x - c.x, dy = y - c.y;
                                double d = sqrt((double)dx*dx + (double)dy*dy);
                                if(lit(ctx, x, y) && d > r + 0.5) {
                                        CHECK(0, "filled circle r=%d: pixel (%d,%d) is %.2f out",
                                              r, x, y, d - r);
                                        y = dim; break;
                                }
                                // the inward bound is the looser of the two on purpose: the
                                // midpoint algorithm approximates from the inside, so its
                                // spans fall up to about 0.55px short of the true circle at
                                // r=60 and the shortfall grows slowly with radius. that is
                                // the algorithm, not a defect -- the outward bound above is
                                // where the sharp check lives
                                if(!lit(ctx, x, y) && d < r - 1.0) {
                                        CHECK(0, "filled circle r=%d: hole at (%d,%d), %.2f in",
                                              r, x, y, r - d);
                                        y = dim; break;
                                }
                        }
                }
                free(ctx->buffer);
                free(ctx);
        }
}

// --- 6. a filled disc covers its own outline exactly ---
static void test_circle_fill_matches_outline(void)
{
        for(int r = 1; r <= 60; r++) {
                int dim = 2 * r + 5;
                light_draw_context_t *outline = make_ctx(dim, dim);
                light_draw_context_t *disc = make_ctx(dim, dim);
                light_draw_point2d c = { dim / 2, dim / 2 };
                light_draw_draw_circle(outline, c, r, false);
                light_draw_draw_circle(disc, c, r, true);
                for(int y = 0; y < dim; y++)
                        for(int x = 0; x < dim; x++)
                                if(lit(outline, x, y))
                                        CHECK(lit(disc, x, y),
                                              "r=%d: outline pixel (%d,%d) missing from fill",
                                              r, x, y);
                free(outline->buffer); free(outline);
                free(disc->buffer); free(disc);
        }
}

// --- 7. a filled rounded rect stays inside its outline's bounds and covers the middle ---
static void test_rect_rounded_fill(void)
{
        struct { int w, h, r; } cases[] = {
                { 240, 280, 20 }, { 64, 128, 8 }, { 100, 100, 50 }, { 33, 33, 4 },
        };
        for(size_t i = 0; i < sizeof(cases)/sizeof(*cases); i++) {
                int w = cases[i].w, h = cases[i].h, r = cases[i].r;
                light_draw_context_t *ctx = make_ctx(w, h);
                light_draw_draw_rect_rounded(ctx, (light_draw_point2d) { 0, 0 },
                                       (light_draw_point2d) { w - 1, h - 1 }, r, true);
                char what[96];
                snprintf(what, sizeof(what), "filled rounded rect %dx%d r=%d", w, h, r);
                // the middle is solid
                for(int y = r; y <= h - 1 - r; y++)
                        for(int x = 0; x < w; x++)
                                if(!lit(ctx, x, y)) {
                                        CHECK(0, "%s: hole at (%d,%d)", what, x, y);
                                        y = h; x = w;
                                }
                // the corner cells are cut away
                if(r >= 3) {
                        CHECK(!lit(ctx, 0, 0), "%s: (0,0) should be cut away", what);
                        CHECK(!lit(ctx, w - 1, h - 1), "%s: far corner should be cut away", what);
                }
                // the top and bottom rows are present but inset
                CHECK(lit(ctx, w / 2, 0), "%s: top row missing", what);
                CHECK(lit(ctx, w / 2, h - 1), "%s: bottom row missing", what);
                free(ctx->buffer);
                free(ctx);
        }
}

// --- 8. arcs near an edge clip rather than wrapping into garbage ---
static void test_arc_clipping(void)
{
        light_draw_context_t *ctx = make_ctx(60, 60);
        // centred on the corner, so three quarters of it is off-canvas
        light_draw_draw_arc(ctx, (light_draw_point2d) { 0, 0 }, 30, 0, 360);
        int n = count_lit(ctx);
        CHECK(n > 20 && n < 80, "corner arc drew %d pixels", n);
        for(int y = 0; y < 60; y++)
                for(int x = 0; x < 60; x++)
                        if(lit(ctx, x, y)) {
                                double d = sqrt((double)x*x + (double)y*y);
                                CHECK(fabs(d - 30) < 1.0,
                                      "clipped arc: stray pixel at (%d,%d), %.2f from centre", x, y, d);
                        }
        free(ctx->buffer);
        free(ctx);
}

// --- 9. the case that started this: a title inside a rounded frame at the touch board's
// real geometry. the bug was the first characters of "light_ui" disappearing behind the
// glass curve, so the check is that the title's bounding box is clear of the frame and
// inside the shape. the layout arithmetic here mirrors light_ui's _ui_window_inset_y() and
// _paint_window(); it is duplicated rather than linked because reaching those means pulling
// in light_canvas and the whole framework, and the property being tested is geometric
static void test_demo_title_clears_curve(void)
{
        const int panel_w = 240, panel_h = 280;
        // MEASURED with screentest_calib169, not guessed -- the original 20 here was less
        // than half the real value, which is why the frame's corners fell off the glass
        const int panel_radius = 42;    // ST_DISPLAY_CORNER_RADIUS
        const int safe_inset = 2;       // LIGHT_UI_DEMO_SAFE_INSET
        const int radius = panel_radius - safe_inset;
        const int border = 1, padding = 2;
        const int char_w = 13, char_h = 19;     // TypeLightSans_ttf_16px on this board
        const int gap = 6, rows = 3;
        const char *title = "light_ui";

        light_draw_context_t *ctx = make_ctx(panel_w, panel_h);
        int x0 = safe_inset, y0 = safe_inset;
        int x1 = panel_w - 1 - safe_inset, y1 = panel_h - 1 - safe_inset;
        light_draw_draw_rect_rounded(ctx, (light_draw_point2d) { x0, y0 }, (light_draw_point2d) { x1, y1 },
                               radius, false);

        // mirrors light_ui: the title stays at the TOP and clears the arc sideways, indented
        // by however far the curve reaches in at its own topmost row
        int ty = y0 + border;
        int dy_title = radius - border;
        int indent = radius - (int)sqrt((double)(radius * radius - dy_title * dy_title));
        int tx = x0 + indent + border + 1;
        int tw = (int)strlen(title) * char_w;

        // no frame pixel may fall inside the title's box -- that is the defect verbatim
        for(int y = ty; y < ty + char_h; y++)
                for(int x = tx; x < tx + tw; x++)
                        if(lit(ctx, x, y)) {
                                CHECK(0, "demo title box overlaps the frame at (%d,%d)", x, y);
                                y = ty + char_h; break;
                        }
        // and the box must be inside the rounded shape, not merely missing the drawn line
        double cx = (x0 + x1) / 2.0, cy = (y0 + y1) / 2.0;
        double hx = (x1 - x0) / 2.0, hy = (y1 - y0) / 2.0;
        struct { int x, y; } corners[] = {
                { tx, ty }, { tx + tw - 1, ty }, { tx, ty + char_h - 1 },
                { tx + tw - 1, ty + char_h - 1 }
        };
        for(size_t i = 0; i < 4; i++) {
                double d = sdf_round_rect(corners[i].x, corners[i].y, cx, cy, hx, hy, radius);
                CHECK(d < 0, "demo title corner (%d,%d) is outside the frame (sdf %.2f)",
                      corners[i].x, corners[i].y, d);
        }
        // the recovered width is the point of the exercise: content spans nearly the full
        // panel now, where a uniform 20px inset left it 40px narrower
        int content_w = (x1 - padding - border) - (x0 + padding + border) + 1;
        CHECK(content_w >= panel_w - 12,
              "content is only %d px wide of %d -- the inset band was not recovered",
              content_w, panel_w);

        // the button rows must clear the corners too, and unlike the title they span the
        // full content width -- so their top-left and bottom-left corners are the ones at
        // risk. this is what a radius of 40 costs vertically, and the check is that it is a
        // cost rather than a clipped layout
        int cx0 = x0 + padding + border, cx1 = x1 - padding - border;
        int ix = padding + border;
        // the corner drop: how far down before the arc has come in as far as the rows sit
        int drop = radius - (int)sqrt((double)(ix * (2 * radius - ix)));
        int inset_y = drop > ix ? drop : ix;
        int sep_y = ty + char_h;
        // the header is a LOWER BOUND on where content starts, not an addition to the drop
        int header_bottom = y0 + border + char_h + 2;
        int cy0 = (y0 + inset_y) > header_bottom ? (y0 + inset_y) : header_bottom;
        // the bottom runs flush and the last row takes the container's curve, so it is inset
        // only by the padding rather than by the drop
        int flush_r = radius - ix;
        int cy1 = y1 - (flush_r > 0 ? ix : inset_y);
        CHECK(cy0 > sep_y, "first row (y=%d) overlaps the title separator (y=%d)", cy0, sep_y);
        int row_h = (cy1 - cy0 + 1 - gap * (rows - 1)) / rows;
        CHECK(row_h >= char_h + 4, "rows are only %d px for a %d px font", row_h, char_h);
        // the flush treatment needs the last row to be at least as tall as the curve it is
        // taking on, or its straight left edge would start inside the arc
        CHECK(row_h >= flush_r, "last row (%d px) is shorter than its corner radius (%d)",
              row_h, flush_r);
        // top corners of the first row still have to clear the arc the ordinary way
        struct { int x, y; } probes[] = { { cx0, cy0 }, { cx1, cy0 } };
        for(size_t i = 0; i < 2; i++) {
                double d = sdf_round_rect(probes[i].x, probes[i].y, cx, cy, hx, hy, radius);
                CHECK(d < 0, "content corner (%d,%d) is outside the frame (sdf %.2f)",
                      probes[i].x, probes[i].y, d);
        }
        // and the last row, drawn flush with its own curve, must lie inside the frame --
        // checked against the real renderer rather than by arithmetic
        int last_top = cy0 + (rows - 1) * (row_h + gap);
        light_draw_context_t *frame = make_ctx(panel_w, panel_h);
        light_draw_draw_rect_rounded(frame, (light_draw_point2d) { x0, y0 },
                               (light_draw_point2d) { x1, y1 }, radius, true);
        light_draw_context_t *last = make_ctx(panel_w, panel_h);
        light_draw_draw_rect_rounded_corners(last, (light_draw_point2d) { cx0, last_top },
                        (light_draw_point2d) { cx1, cy1 }, flush_r, LIGHT_DRAW_CORNER_BOTTOM, true);
        for(int y = 0; y < panel_h; y++)
                for(int x = 0; x < panel_w; x++)
                        if(lit(last, x, y) && !lit(frame, x, y)) {
                                CHECK(0, "last row escapes the frame at (%d,%d)", x, y);
                                y = panel_h; break;
                        }
        free(frame->buffer); free(frame);
        free(last->buffer); free(last);

        printf("  demo geometry: frame (%d,%d)-(%d,%d) r=%d, title (%d,%d), sep y=%d\n",
               x0, y0, x1, y1, radius, tx, ty, sep_y);
        printf("  content: x %d..%d (%d px, %d chars), y %d..%d, %d rows of %d px\n",
               cx0, cx1, content_w, content_w / char_w, cy0, cy1, rows, row_h);
        printf("  last row: y %d..%d flush, bottom corners r=%d\n", last_top, cy1, flush_r);
        free(ctx->buffer);
        free(ctx);
}

// --- 10. per-corner rounding: named corners curve, the rest stay sharp ---
static void test_rect_rounded_corner_mask(void)
{
        struct { uint8_t mask; const char *name; int tl, tr, br, bl; } cases[] = {
                { LIGHT_DRAW_CORNER_ALL,          "all",    1, 1, 1, 1 },
                { LIGHT_DRAW_CORNER_BOTTOM,       "bottom", 0, 0, 1, 1 },
                { LIGHT_DRAW_CORNER_TOP,          "top",    1, 1, 0, 0 },
                { LIGHT_DRAW_CORNER_LEFT,         "left",   1, 0, 0, 1 },
                { LIGHT_DRAW_CORNER_TOP_LEFT,     "tl",     1, 0, 0, 0 },
                { LIGHT_DRAW_CORNER_BOTTOM_RIGHT, "br",     0, 0, 1, 0 },
                { LIGHT_DRAW_CORNER_NONE,         "none",   0, 0, 0, 0 },
        };
        const int w = 160, h = 120, r = 24;

        for(size_t i = 0; i < sizeof(cases)/sizeof(*cases); i++) {
                for(int fill = 0; fill <= 1; fill++) {
                        light_draw_context_t *ctx = make_ctx(w, h);
                        light_draw_draw_rect_rounded_corners(ctx, (light_draw_point2d) { 0, 0 },
                                        (light_draw_point2d) { w - 1, h - 1 }, r, cases[i].mask, fill);
                        char what[96];
                        snprintf(what, sizeof(what), "mask %s %s",
                                 cases[i].name, fill ? "filled" : "outline");

                        // a square corner keeps its corner pixel; a rounded one loses it
                        struct { int x, y, rounded; const char *label; } probe[] = {
                                { 0,     0,     cases[i].tl, "top-left" },
                                { w - 1, 0,     cases[i].tr, "top-right" },
                                { w - 1, h - 1, cases[i].br, "bottom-right" },
                                { 0,     h - 1, cases[i].bl, "bottom-left" },
                        };
                        for(size_t k = 0; k < 4; k++) {
                                int on = lit(ctx, probe[k].x, probe[k].y);
                                if(probe[k].rounded)
                                        CHECK(!on, "%s: %s should be cut away", what, probe[k].label);
                                else
                                        CHECK(on, "%s: %s should be square and lit", what, probe[k].label);
                        }
                        if(!fill) {
                                check_connected(ctx, what);
                                check_closed(ctx, what);
                        }
                        free(ctx->buffer);
                        free(ctx);
                }
        }

        // the flush-row case this was added for: a row whose bottom corners follow a
        // container's curve must stay inside that container. the container is a rounded rect
        // of radius R; the row is inset by ix on both sides with bottom corners of radius
        // R-ix, which shares the container's arc centres exactly
        const int R = 40, ix = 3, cw = 240, ch = 280;
        light_draw_context_t *cont = make_ctx(cw, ch);
        light_draw_draw_rect_rounded(cont, (light_draw_point2d) { 0, 0 },
                               (light_draw_point2d) { cw - 1, ch - 1 }, R, true);
        light_draw_context_t *row = make_ctx(cw, ch);
        int row_top = ch - 1 - ix - 90;         // a row comfortably taller than R-ix
        light_draw_draw_rect_rounded_corners(row, (light_draw_point2d) { ix, row_top },
                        (light_draw_point2d) { cw - 1 - ix, ch - 1 - ix },
                        R - ix, LIGHT_DRAW_CORNER_BOTTOM, true);
        for(int y = 0; y < ch; y++)
                for(int x = 0; x < cw; x++)
                        if(lit(row, x, y) && !lit(cont, x, y)) {
                                CHECK(0, "flush row escapes its container at (%d,%d)", x, y);
                                y = ch; break;
                        }
        free(cont->buffer); free(cont);
        free(row->buffer); free(row);
}

// the cases CTest registers one at a time. the names are duplicated in test/CMakeLists.txt
// ---------------------------------------------------------------------------------------------
//   THE COORDINATE TRANSFORM. Everything above draws; this pair maps between the logical space
// an application thinks in and the physical buffer a panel scans out. Coverage showed
// light_draw_untransform_point() and light_draw_transform_rect() at 0% -- untested, despite untransform
// being what turns a touch controller's report into the widget the user actually pressed.
//
//   the round-trip property is the one that matters. A tap is untransformed to logical space,
// hit-tested against rects that were laid out in logical space, and the widget is then drawn
// through the forward transform. If the two disagree at any rotation, the interface responds to
// presses somewhere other than where it drew itself -- which is a genuinely confusing bug to
// chase from the symptom, and one this project has already spent a session on.
// ---------------------------------------------------------------------------------------------

static const uint8_t _rotations[] = { LIGHT_DRAW_ROTATE_0, LIGHT_DRAW_ROTATE_90, LIGHT_DRAW_ROTATE_180, LIGHT_DRAW_ROTATE_270 };
static const char *_rotation_name(uint8_t r)
{
        switch(r) {
        case LIGHT_DRAW_ROTATE_0:   return "0";
        case LIGHT_DRAW_ROTATE_90:  return "90";
        case LIGHT_DRAW_ROTATE_180: return "180";
        default:              return "270";
        }
}

static void test_transform_round_trip(void)
{
        //   deliberately non-square: a square canvas hides every axis swap, because getting
        // x and y the wrong way round still lands inside the buffer and still round-trips
        light_draw_context_t *ctx = make_ctx(48, 32);

        for(size_t r = 0; r < sizeof(_rotations); r++) {
                light_draw_context_set_rotation(ctx, _rotations[r]);

                for(uint16_t y = 0; y < ctx->dim_y; y += 3) {
                        for(uint16_t x = 0; x < ctx->dim_x; x += 3) {
                                light_draw_point2d logical = { x, y };
                                light_draw_point2d phys_min, phys_max;
                                // transform_rect of a degenerate rect is the forward mapping of
                                // the single point, whichever way the axes were flipped
                                light_draw_transform_rect(ctx, logical, logical, &phys_min, &phys_max);

                                light_draw_point2d back = light_draw_untransform_point(ctx, phys_min);
                                CHECK(back.x == x && back.y == y,
                                        "rotation %s: (%u,%u) -> (%u,%u) -> (%u,%u)",
                                        _rotation_name(_rotations[r]), x, y,
                                        phys_min.x, phys_min.y, back.x, back.y);
                        }
                }
        }
}
static void test_transform_maps_corners(void)
{
        light_draw_context_t *ctx = make_ctx(48, 32);

        for(size_t r = 0; r < sizeof(_rotations); r++) {
                light_draw_context_set_rotation(ctx, _rotations[r]);

                //   the logical origin has to land on SOME physical corner, and the far logical
                // corner on the opposite one. Which corner is the rotation's business; that the
                // two stay diagonally opposite is the invariant, and it is what fails if a
                // translation term is dropped and the image slides off the panel
                light_draw_point2d origin = { 0, 0 };
                light_draw_point2d far = { (uint16_t)(ctx->dim_x - 1), (uint16_t)(ctx->dim_y - 1) };
                light_draw_point2d omin, omax, fmin, fmax;
                light_draw_transform_rect(ctx, origin, origin, &omin, &omax);
                light_draw_transform_rect(ctx, far, far, &fmin, &fmax);

                uint16_t px_max = (uint16_t)(ctx->phys_dim_x - 1);
                uint16_t py_max = (uint16_t)(ctx->phys_dim_y - 1);

                CHECK((omin.x == 0 || omin.x == px_max) && (omin.y == 0 || omin.y == py_max),
                        "rotation %s: logical origin should map to a physical corner, got (%u,%u)",
                        _rotation_name(_rotations[r]), omin.x, omin.y);
                CHECK(fmin.x == (omin.x == 0 ? px_max : 0) && fmin.y == (omin.y == 0 ? py_max : 0),
                        "rotation %s: far corner should be diagonally opposite the origin, got (%u,%u) against (%u,%u)",
                        _rotation_name(_rotations[r]), fmin.x, fmin.y, omin.x, omin.y);
        }
}
static void test_transform_rect_orders_its_output(void)
{
        light_draw_context_t *ctx = make_ctx(48, 32);

        for(size_t r = 0; r < sizeof(_rotations); r++) {
                light_draw_context_set_rotation(ctx, _rotations[r]);

                light_draw_point2d p0 = { 4, 6 };
                light_draw_point2d p1 = { 20, 25 };
                light_draw_point2d min, max;
                light_draw_transform_rect(ctx, p0, p1, &min, &max);

                //   min really is the minimum on both axes. Half the rotations negate an axis,
                // so the forward mapping of p0 can come out ABOVE that of p1 -- and every
                // caller feeds these straight into a fill loop that draws nothing at all when
                // the bounds arrive the wrong way round
                CHECK(min.x <= max.x, "rotation %s: min.x %u > max.x %u",
                        _rotation_name(_rotations[r]), min.x, max.x);
                CHECK(min.y <= max.y, "rotation %s: min.y %u > max.y %u",
                        _rotation_name(_rotations[r]), min.y, max.y);

                //   and the area is preserved: a rotation moves a rect, it does not resize it.
                // Catches a transform that maps both corners onto the same point, which would
                // satisfy the ordering check above while collapsing everything drawn
                uint32_t w = (uint32_t)(max.x - min.x) + 1, h = (uint32_t)(max.y - min.y) + 1;
                uint32_t expect_w = 20 - 4 + 1, expect_h = 25 - 6 + 1;
                bool swapped = (w == expect_h && h == expect_w);
                bool same = (w == expect_w && h == expect_h);
                CHECK(swapped || same, "rotation %s: %ux%u is neither %ux%u nor its transpose",
                        _rotation_name(_rotations[r]), w, h, expect_w, expect_h);
        }
}
static void test_untransform_clamps_outside_points(void)
{
        light_draw_context_t *ctx = make_ctx(48, 32);

        //   a touch panel's coordinate range need not match the display's, so a report can land
        // outside the canvas. Clamping rather than wrapping is the contract: light_draw_point2d's
        // fields are unsigned, so a negative intermediate becomes an enormous coordinate and
        // hit-tests against whatever widget happens to live there.
        //
        //   swept over all four rotations because the two clamps are not symmetric in when they
        // fire: at 0 degrees an over-range physical point can only come out too LARGE, and it
        // takes a rotation carrying a translation term (180, say) for the same point to come
        // out negative. Testing one orientation exercises one clamp and leaves the other free
        // to be deleted unnoticed
        for(size_t r = 0; r < sizeof(_rotations); r++) {
                light_draw_context_set_rotation(ctx, _rotations[r]);

                light_draw_point2d far = light_draw_untransform_point(ctx,
                        (light_draw_point2d) { (uint16_t)(ctx->phys_dim_x + 100),
                                         (uint16_t)(ctx->phys_dim_y + 100) });
                CHECK(far.x < ctx->dim_x && far.y < ctx->dim_y,
                        "rotation %s: a point past the far edge should clamp inside %ux%u, got (%u,%u)",
                        _rotation_name(_rotations[r]), ctx->dim_x, ctx->dim_y, far.x, far.y);

                light_draw_point2d origin = light_draw_untransform_point(ctx, (light_draw_point2d) { 0, 0 });
                CHECK(origin.x < ctx->dim_x && origin.y < ctx->dim_y,
                        "rotation %s: the physical origin should map inside the canvas, got (%u,%u)",
                        _rotation_name(_rotations[r]), origin.x, origin.y);
        }
}
//   the exact largest scale at which a `w`x`h` image, turned by `angle`, still fits inside a
// `w`x`h` buffer. light_draw_scale_inscribed() computes this in Q15 off a per-degree sine table;
// this computes it in floating point, which is the oracle rather than a reimplementation --
// the point of the comparison is that two independent routes agree
static double inscribed_optimum(double w, double h, double angle)
{
        double c = fabs(cos(angle * TEST_PI / 180.0));
        double s = fabs(sin(angle * TEST_PI / 180.0));
        double fit_w = w / (w * c + h * s);
        double fit_h = h / (w * s + h * c);
        double fit = fit_w < fit_h ? fit_w : fit_h;
        return fit > 1.0 ? 1.0 : fit;
}
static void test_scale_inscribed_shrinks_off_axis(void)
{
        //   both aspect ratios, because the two axis fits are not interchangeable: on a wide
        // canvas the height constraint binds and on a tall one the width does. Checking a
        // single shape leaves whichever term is not binding free to be wrong -- and since the
        // function returns the smaller of the two, a wrong non-binding term is invisible
        const int dims[][2] = { { 48, 32 }, { 32, 48 } };

        for(size_t d = 0; d < sizeof(dims) / sizeof(*dims); d++) {
                light_draw_context_t *ctx = make_ctx(dims[d][0], dims[d][1]);

                //   on-axis needs no shrinking, and anything less than full scale here would
                // soften every frame that is not mid-turn
                CHECK(light_draw_scale_inscribed(ctx, 0) == LIGHT_DRAW_SCALE_ONE,
                        "%dx%d: 0 degrees should need no shrink, got %d",
                        dims[d][0], dims[d][1], light_draw_scale_inscribed(ctx, 0));

                for(int angle = -180; angle <= 180; angle += 15) {
                        int32_t got = light_draw_scale_inscribed(ctx, (int16_t)angle);
                        double want = inscribed_optimum(dims[d][0], dims[d][1], angle) * LIGHT_DRAW_SCALE_ONE;

                        //   too large and the corners this exists to preserve get clipped; too
                        // small and the image is needlessly shrunk. 1% either side covers the
                        // per-degree table and the Q15 rounding, and nothing wider
                        CHECK(got <= want * 1.01,
                                "%dx%d at %d degrees: %d exceeds the inscribed fit %.0f -- corners will clip",
                                dims[d][0], dims[d][1], angle, got, want);
                        CHECK(got >= want * 0.99,
                                "%dx%d at %d degrees: %d is below the inscribed fit %.0f -- needlessly shrunk",
                                dims[d][0], dims[d][1], angle, got, want);
                }

                //   turning left and right need the same room, which the table-driven
                // implementation only gets right if it takes the magnitude of both terms
                CHECK(light_draw_scale_inscribed(ctx, -45) == light_draw_scale_inscribed(ctx, 45),
                        "%dx%d: -45 and 45 should agree, got %d and %d", dims[d][0], dims[d][1],
                        light_draw_scale_inscribed(ctx, -45), light_draw_scale_inscribed(ctx, 45));
        }
}

static const uint8_t _flips[] = { LIGHT_DRAW_FLIP_NONE, LIGHT_DRAW_FLIP_HORIZONTAL, LIGHT_DRAW_FLIP_VERTICAL, LIGHT_DRAW_FLIP_BOTH };
static const char *_flip_name(uint8_t f)
{
        switch(f) {
        case LIGHT_DRAW_FLIP_NONE:       return "none";
        case LIGHT_DRAW_FLIP_HORIZONTAL: return "horizontal";
        case LIGHT_DRAW_FLIP_VERTICAL:   return "vertical";
        default:                   return "both";
        }
}

static void test_transform_round_trip_with_flips(void)
{
        light_draw_context_t *ctx = make_ctx(48, 32);

        //   flip composes with rotation rather than replacing it, and the composition is where
        // this can go wrong: the flip matrix is built from dim_x/dim_y, which rotation has
        // already swapped. Build the flip against the wrong pair and the round-trip still
        // works at 0 and 180 degrees, failing only on the two rotations that transpose
        for(size_t f = 0; f < sizeof(_flips); f++) {
                for(size_t r = 0; r < sizeof(_rotations); r++) {
                        light_draw_context_set_rotation(ctx, _rotations[r]);
                        light_draw_context_set_flip(ctx, _flips[f]);

                        for(uint16_t y = 0; y < ctx->dim_y; y += 3) {
                                for(uint16_t x = 0; x < ctx->dim_x; x += 3) {
                                        light_draw_point2d logical = { x, y };
                                        light_draw_point2d pmin, pmax;
                                        light_draw_transform_rect(ctx, logical, logical, &pmin, &pmax);
                                        light_draw_point2d back = light_draw_untransform_point(ctx, pmin);

                                        CHECK(back.x == x && back.y == y,
                                                "flip %s rotation %s: (%u,%u) -> (%u,%u) -> (%u,%u)",
                                                _flip_name(_flips[f]), _rotation_name(_rotations[r]),
                                                x, y, pmin.x, pmin.y, back.x, back.y);
                                }
                        }
                }
        }
}
static void test_flip_mirrors_the_right_axis(void)
{
        light_draw_context_t *ctx = make_ctx(48, 32);
        light_draw_context_set_rotation(ctx, LIGHT_DRAW_ROTATE_0);

        //   the round-trip above is self-consistent by construction -- it would pass just as
        // happily if horizontal and vertical were swapped, or if both were no-ops. This pins
        // which axis each one actually mirrors, at the one rotation where the answer is
        // unambiguous
        struct { uint8_t flip; uint16_t want_x, want_y; } expect[] = {
                { LIGHT_DRAW_FLIP_NONE,       0, 0 },
                { LIGHT_DRAW_FLIP_HORIZONTAL, 47, 0 },
                { LIGHT_DRAW_FLIP_VERTICAL,   0, 31 },
                { LIGHT_DRAW_FLIP_BOTH,       47, 31 },
        };

        for(size_t i = 0; i < sizeof(expect) / sizeof(*expect); i++) {
                light_draw_context_set_flip(ctx, expect[i].flip);
                light_draw_point2d origin = { 0, 0 }, min, max;
                light_draw_transform_rect(ctx, origin, origin, &min, &max);
                CHECK(min.x == expect[i].want_x && min.y == expect[i].want_y,
                        "flip %s: logical origin -> (%u,%u), expected (%u,%u)",
                        _flip_name(expect[i].flip), min.x, min.y,
                        expect[i].want_x, expect[i].want_y);
        }
}
static void test_double_buffer_swaps(void)
{
        light_draw_context_t *ctx = make_ctx(16, 16);

        //   swapping without a back buffer is a no-op that says so, rather than swapping in
        // NULL. A caller that enables double buffering conditionally (on how much RAM is
        // left, say) then draws through the same code either way depends on this
        CHECK(!light_draw_context_swap_buffers(ctx), "swapping with no back buffer should return false");
        CHECK(ctx->buffer != NULL, "a failed swap should leave the front buffer intact");

        light_draw_context_enable_double_buffer(ctx);
        CHECK(ctx->buffer_back != NULL, "enabling double buffering should allocate a back buffer");

        //   enabling twice must not allocate again: the second allocation would leak the
        // first, and on a device where the buffer is a sizeable fraction of RAM that is one
        // stray call away from an allocation failure much later
        uint8_t *back = ctx->buffer_back;
        light_draw_context_enable_double_buffer(ctx);
        CHECK(ctx->buffer_back == back, "enabling double buffering twice reallocated the buffer");

        uint8_t *front = ctx->buffer;
        CHECK(light_draw_context_swap_buffers(ctx), "swapping with a back buffer should return true");
        CHECK(ctx->buffer == back && ctx->buffer_back == front,
                "the swap should exchange the two buffers, not overwrite one");

        //   and the buffers are genuinely separate memory. Drawing into the new front must
        // leave the old one alone -- that separation is the entire point, and a swap that
        // handed back the same pointer would satisfy every check above
        light_draw_draw_clear(ctx);
        light_draw_draw_point(ctx, (light_draw_point2d) { 3, 4 });
        CHECK(lit(ctx, 3, 4), "the point should be lit in the new front buffer");
        size_t off = ((size_t)4 * ctx->phys_dim_x + 3) * 2;
        CHECK(front[off] == 0 && front[off + 1] == 0,
                "drawing after the swap wrote into the back buffer as well");
}

// rather than discovered, which is deliberate: a discovery step that silently found nothing
// would report a clean run having tested precisely zero things, and that is the one failure
// mode a test suite must not have. `--list` exists so the two can be checked against each
// other by eye, or by a shell loop, without running anything
static const struct {
        const char *name;
        void (*fn)(void);
} test_cases[] = {
        { "arc_no_gaps",              test_arc_no_gaps },
        { "arc_accuracy",             test_arc_accuracy },
        { "arc_endpoints",            test_arc_endpoints },
        { "arc_clipping",             test_arc_clipping },
        { "rect_rounded_joins",       test_rect_rounded_joins },
        { "rect_rounded_fill",        test_rect_rounded_fill },
        { "rect_rounded_corner_mask", test_rect_rounded_corner_mask },
        { "circle_fill",              test_circle_fill },
        { "circle_fill_matches_outline", test_circle_fill_matches_outline },
        { "demo_title_clears_curve",  test_demo_title_clears_curve },
        { "transform_round_trip",     test_transform_round_trip },
        { "transform_maps_corners",   test_transform_maps_corners },
        { "transform_rect_orders_its_output", test_transform_rect_orders_its_output },
        { "untransform_clamps_outside_points", test_untransform_clamps_outside_points },
        { "scale_inscribed_shrinks_off_axis", test_scale_inscribed_shrinks_off_axis },
        { "transform_round_trip_with_flips", test_transform_round_trip_with_flips },
        { "flip_mirrors_the_right_axis", test_flip_mirrors_the_right_axis },
        { "double_buffer_swaps",      test_double_buffer_swaps },
};
#define TEST_CASE_COUNT (sizeof(test_cases) / sizeof(*test_cases))

int main(int argc, char **argv)
{
        if(argc > 1 && strcmp(argv[1], "--list") == 0) {
                for(size_t i = 0; i < TEST_CASE_COUNT; i++)
                        printf("%s\n", test_cases[i].name);
                return 0;
        }

        if(argc > 1) {
                for(size_t i = 0; i < TEST_CASE_COUNT; i++) {
                        if(strcmp(argv[1], test_cases[i].name) != 0)
                                continue;
                        test_cases[i].fn();
                        printf("%s: %s, %d failure(s)\n", test_cases[i].name,
                               failures ? "FAILED" : "PASSED", failures);
                        return failures ? 1 : 0;
                }
                // an unknown name must be an error, not a silent pass -- a typo in
                // CMakeLists.txt would otherwise register a test that always succeeds
                printf("FAIL: no such test case '%s'\n", argv[1]);
                return 2;
        }

        for(size_t i = 0; i < TEST_CASE_COUNT; i++)
                test_cases[i].fn();
        printf("%s: %d failure(s)\n", failures ? "FAILED" : "PASSED", failures);
        return failures ? 1 : 0;
}
