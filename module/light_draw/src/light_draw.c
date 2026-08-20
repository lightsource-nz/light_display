/*
 *  light_draw.c
 *  routines for managing and drawing to image buffers
 * 
 *  authored by Alex Fulton
 *  created august 2022
 * 
 */

/*
 *  --> TODO: implement optimized methods for filling horizontal spans of
 *  pixels in memory simultaneously
 *  --> TODO: implement support for grayscale and colour pixel formats
 *
 *  rotation and mirroring are implemented as composable affine transforms
 *  (_recompute_transform()/_apply_transform() / light_draw_context_set_rotation()/
 *  light_draw_context_set_flip())
 */

#include <light_draw.h>
#include "light_draw_internal.h"

#include <stdio.h>
#include <string.h>

// TODO implement for BPP values > 1
// composes two affine transforms represented as their equivalent 3x3 homogeneous
// matrices (bottom row [0 0 1], not stored): result = second . first, i.e. a point is
// mapped by 'first' and then by 'second'
static light_draw_transform_t _compose_transform(light_draw_transform_t first, light_draw_transform_t second)
{
    return (light_draw_transform_t) {
        .a  = second.a * first.a + second.b * first.c,
        .b  = second.a * first.b + second.b * first.d,
        .tx = second.a * first.tx + second.b * first.ty + second.tx,
        .c  = second.c * first.a + second.d * first.c,
        .d  = second.c * first.b + second.d * first.d,
        .ty = second.c * first.tx + second.d * first.ty + second.ty
    };
}
// recomputes ctx->transform from ctx->rotation/ctx->flip/phys_dim_x/phys_dim_y. called
// whenever any of those change (_context_set_rotation()/_context_set_flip()) rather
// than re-deriving the matrix on every pixel
static void _recompute_transform(light_draw_context_t *ctx)
{
    // logical dim_x/dim_y depend only on rotation -- flip mirrors within whatever
    // logical space rotation already established, it doesn't change the dimensions
    if(ctx->rotation == LIGHT_DRAW_ROTATE_90 || ctx->rotation == LIGHT_DRAW_ROTATE_270) {
        ctx->dim_x = ctx->phys_dim_y;
        ctx->dim_y = ctx->phys_dim_x;
    } else {
        ctx->dim_x = ctx->phys_dim_x;
        ctx->dim_y = ctx->phys_dim_y;
    }

    light_draw_transform_t flip;
    switch(ctx->flip) {
    case LIGHT_DRAW_FLIP_HORIZONTAL:
        flip = (light_draw_transform_t) { .a = -1, .b = 0, .tx = ctx->dim_x - 1, .c = 0, .d = 1,  .ty = 0 };
        break;
    case LIGHT_DRAW_FLIP_VERTICAL:
        flip = (light_draw_transform_t) { .a = 1,  .b = 0, .tx = 0, .c = 0, .d = -1, .ty = ctx->dim_y - 1 };
        break;
    case LIGHT_DRAW_FLIP_BOTH:
        flip = (light_draw_transform_t) { .a = -1, .b = 0, .tx = ctx->dim_x - 1, .c = 0, .d = -1, .ty = ctx->dim_y - 1 };
        break;
    case LIGHT_DRAW_FLIP_NONE:
    default:
        flip = (light_draw_transform_t) { .a = 1, .b = 0, .tx = 0, .c = 0, .d = 1, .ty = 0 };
        break;
    }

    light_draw_transform_t rotate;
    switch(ctx->rotation) {
    case LIGHT_DRAW_ROTATE_90:
        // logical top edge -> physical right edge
        rotate = (light_draw_transform_t) {
            .a = 0, .b = -1, .tx = ctx->phys_dim_x - 1,
            .c = 1, .d = 0,  .ty = 0
        };
        break;
    case LIGHT_DRAW_ROTATE_180:
        rotate = (light_draw_transform_t) {
            .a = -1, .b = 0,  .tx = ctx->phys_dim_x - 1,
            .c = 0,  .d = -1, .ty = ctx->phys_dim_y - 1
        };
        break;
    case LIGHT_DRAW_ROTATE_270:
        // logical top edge -> physical left edge
        rotate = (light_draw_transform_t) {
            .a = 0,  .b = 1, .tx = 0,
            .c = -1, .d = 0, .ty = ctx->phys_dim_y - 1
        };
        break;
    case LIGHT_DRAW_ROTATE_0:
    default:
        rotate = (light_draw_transform_t) {
            .a = 1, .b = 0, .tx = 0,
            .c = 0, .d = 1, .ty = 0
        };
        break;
    }

    // flip is applied first (in logical space), then rotate maps the (possibly
    // flipped) logical point onto the physical buffer
    ctx->transform = _compose_transform(flip, rotate);

    //   any clip was expressed in the logical space this function has just redefined, so it
    // is reset to the whole (new) canvas rather than carried across meaning something else.
    // this is also what initialises it: _context_create() runs through here
    ctx->clip_x0 = 0;
    ctx->clip_y0 = 0;
    ctx->clip_x1 = ctx->dim_x - 1;
    ctx->clip_y1 = ctx->dim_y - 1;
}
// maps a LOGICAL (dim_x, dim_y)-bounded coordinate onto the PHYSICAL
// (phys_dim_x, phys_dim_y)-bounded buffer via ctx->transform (see light_draw.h for the
// matrix layout). LIGHT_DRAW_ROTATE_0's identity matrix makes this a no-op multiply
static light_draw_point2d _apply_transform(const light_draw_context_t *ctx, light_draw_point2d p)
{
    const light_draw_transform_t *m = &ctx->transform;
    int32_t x = m->a * (int32_t)p.x + m->b * (int32_t)p.y + m->tx;
    int32_t y = m->c * (int32_t)p.x + m->d * (int32_t)p.y + m->ty;
    return (light_draw_point2d) { (uint16_t)x, (uint16_t)y };
}

// inverse of _apply_transform(). solving
//     px = a*x + b*y + tx
//     py = c*x + d*y + ty
// for (x, y) gives the adjugate over the determinant; because det is always +/-1 here
// (see _recompute_transform() -- rotations and flips only, and their composition), the
// division degenerates into a multiply by det itself, so this stays exact integer maths
light_draw_point2d light_draw_untransform_point(const light_draw_context_t *ctx, light_draw_point2d phys)
{
    const light_draw_transform_t *m = &ctx->transform;
    int32_t det = m->a * m->d - m->b * m->c;
    if(det == 0)
        // not reachable for any transform this builds, but a zero determinant would mean
        // an unrecoverable mapping -- return the input rather than divide by zero
        return phys;

    int32_t px = (int32_t)phys.x - m->tx;
    int32_t py = (int32_t)phys.y - m->ty;
    int32_t x = (m->d * px - m->b * py) * det;
    int32_t y = (m->a * py - m->c * px) * det;

    // a touch panel's coordinate range doesn't have to match the display's exactly, so a
    // point can land just outside the logical canvas -- clamp rather than let it wrap when
    // it becomes light_draw_point2d's unsigned fields
    if(x < 0) x = 0;
    if(y < 0) y = 0;
    if(x > (int32_t)ctx->dim_x - 1) x = ctx->dim_x - 1;
    if(y > (int32_t)ctx->dim_y - 1) y = ctx->dim_y - 1;

    return (light_draw_point2d) { (uint16_t)x, (uint16_t)y };
}

// sine of 0..90 degrees in Q15, one entry per degree. a table rather than a series because
// the only consumer is an animation stepping through whole degrees, and 182 bytes of rodata
// is cheaper than any runtime approximation -- and keeps this free of floating point, which
// matters on the FPU-less RP2040 as much as the RP2350
static const int16_t _sin_q15[91] = {
        0,   572,  1144,  1715,  2286,  2856,  3425,  3993,  4560,  5126,
     5690,  6252,  6813,  7371,  7927,  8481,  9032,  9580, 10126, 10668,
    11207, 11743, 12275, 12803, 13328, 13848, 14364, 14876, 15383, 15886,
    16384, 16877, 17364, 17847, 18324, 18795, 19261, 19720, 20174, 20622,
    21063, 21498, 21926, 22348, 22763, 23170, 23571, 23965, 24351, 24730,
    25102, 25466, 25822, 26170, 26510, 26842, 27166, 27482, 27789, 28088,
    28378, 28660, 28932, 29196, 29452, 29698, 29935, 30163, 30382, 30592,
    30792, 30983, 31164, 31336, 31499, 31651, 31795, 31928, 32052, 32166,
    32270, 32365, 32449, 32524, 32588, 32643, 32688, 32723, 32748, 32763,
    32767
};
// wraps any angle into 0..359 and reflects through the quadrants, so only a quarter turn
// has to be tabulated
static int32_t _sin_deg_q15(int16_t angle_deg)
{
    int32_t a = angle_deg % 360;
    if(a < 0) a += 360;
    if(a <= 90)  return _sin_q15[a];
    if(a <= 180) return _sin_q15[180 - a];
    if(a <= 270) return -_sin_q15[a - 180];
    return -_sin_q15[360 - a];
}
static int32_t _cos_deg_q15(int16_t angle_deg)
{
    return _sin_deg_q15((int16_t)(angle_deg + 90));
}
// the same sine at sub-degree resolution, taking Q8 degrees (degrees << 8) and interpolating
// linearly between the table's whole-degree entries. arcs need this where light_draw_blit_rotated()
// did not: an arc of radius r steps by about 1/r radians, which is under a degree as soon as
// r passes 60, and quantising those steps back onto whole degrees would pile several samples
// onto one pixel and leave gaps between the rest. linear interpolation error on sine at one
// degree spacing peaks around 4e-5 -- three orders of magnitude below the pixel it feeds
static int32_t _sin_deg_q8_q15(int32_t deg_q8)
{
    // arithmetic shift floors, including for negative angles, which is exactly what the
    // fractional part has to be measured up from
    int32_t whole = deg_q8 >> 8;
    int32_t frac  = deg_q8 & 0xFF;
    int32_t s0 = _sin_deg_q15((int16_t)(whole % 360));
    int32_t s1 = _sin_deg_q15((int16_t)((whole + 1) % 360));
    return s0 + (((s1 - s0) * frac) >> 8);
}
static int32_t _cos_deg_q8_q15(int32_t deg_q8)
{
    return _sin_deg_q8_q15(deg_q8 + (90 << 8));
}

int32_t light_draw_scale_inscribed(const light_draw_context_t *ctx, int16_t angle_deg)
{
    int32_t s = _sin_deg_q15(angle_deg);
    int32_t c = _cos_deg_q15(angle_deg);
    if(s < 0) s = -s;
    if(c < 0) c = -c;

    int32_t w = ctx->phys_dim_x;
    int32_t h = ctx->phys_dim_y;
    // the rotated image's bounding box, still in Q15 pixels. each axis has to fit the
    // buffer's matching dimension, so the binding constraint is whichever needs shrinking
    // further
    int32_t need_w = w * c + h * s;
    int32_t need_h = w * s + h * c;
    if(need_w <= 0 || need_h <= 0)
        return LIGHT_DRAW_SCALE_ONE;

    // (dimension << 30) / need, in 64-bit: shifting need_w down to whole pixels first would
    // be simpler but rounds the divisor, and rounding a DIVISOR down inflates the result --
    // an inscribed scale that comes out even slightly too large clips the corners it exists
    // to preserve
    int32_t fit_w = (int32_t)(((int64_t)w << 30) / need_w);
    int32_t fit_h = (int32_t)(((int64_t)h << 30) / need_h);
    int32_t fit = fit_w < fit_h ? fit_w : fit_h;
    return fit > LIGHT_DRAW_SCALE_ONE ? LIGHT_DRAW_SCALE_ONE : fit;
}

void light_draw_blit_rotated(const light_draw_context_t *ctx, const uint8_t *src,
                       int16_t angle_deg, int32_t scale_q15)
{
    // 16bpp only -- a 1bpp source would need bit addressing on both ends, and no mono panel
    // in this tree rotates (they have no orientation sensor)
    if(ctx->px_bits != 16 || !src || scale_q15 <= 0)
        return;

    int32_t w = ctx->phys_dim_x;
    int32_t h = ctx->phys_dim_y;
    int32_t cx = w / 2;
    int32_t cy = h / 2;

    // the INVERSE rotation, folded with the inverse of the scale: sampling backwards means
    // rotating by -angle and dividing by the scale, and doing both here keeps the inner loop
    // to two multiply-accumulates per axis
    int32_t inv = (int32_t)(((int64_t)LIGHT_DRAW_SCALE_ONE * LIGHT_DRAW_SCALE_ONE) / scale_q15);
    int32_t cos_i = (int32_t)(((int64_t)_cos_deg_q15(angle_deg) * inv) >> 15);
    int32_t sin_i = (int32_t)(((int64_t)_sin_deg_q15(angle_deg) * inv) >> 15);

    for(int32_t dy = 0; dy < h; dy++) {
        int32_t ry = dy - cy;
        // the row's source origin, stepped along x by (cos_i, -sin_i) rather than recomputed
        // per pixel -- the same incremental trick a DDA uses
        int32_t sx = (-cx * cos_i + ry * sin_i) + (cx << 15);
        int32_t sy = ( cx * sin_i + ry * cos_i) + (cy << 15);
        uint8_t *dst_row = &ctx->buffer[(uint32_t)dy * w * 2];

        for(int32_t dx = 0; dx < w; dx++, sx += cos_i, sy -= sin_i) {
            // rounded, not truncated. this is nearest-neighbour sampling, so rounding is
            // what "nearest" means -- and it is load-bearing rather than cosmetic: Q15
            // cannot represent 1.0 (the table's sin(90) is 32767), so at 0 degrees the
            // step is one ulp short of a whole pixel and the deficit accumulates until it
            // crosses a pixel boundary exactly at the centre, shifting the entire right
            // half of the image over by one. rounding absorbs that
            int32_t px = (sx + 16384) >> 15;
            int32_t py = (sy + 16384) >> 15;
            // outside the source: leave the destination pixel untouched, so a caller that
            // cleared first gets background rather than smeared edge pixels
            if(px < 0 || py < 0 || px >= w || py >= h)
                continue;
            const uint8_t *s = &src[((uint32_t)py * w + px) * 2];
            dst_row[dx * 2]     = s[0];
            dst_row[dx * 2 + 1] = s[1];
        }
    }
}

void light_draw_blit_offset(const light_draw_context_t *ctx, const uint8_t *src, int32_t off_x, int32_t off_y)
{
    // 16bpp only, matching light_draw_blit_rotated() -- a 1bpp path would need bit addressing on
    // both ends, and the panels that would want one do not animate
    if(ctx->px_bits != 16 || !src)
        return;

    int32_t w = ctx->phys_dim_x;
    int32_t h = ctx->phys_dim_y;

    // shifted entirely off the buffer: every row would clip away below, so say so here
    if(off_x <= -w || off_x >= w || off_y <= -h || off_y >= h)
        return;

    // whole rows at a time. this is a translation, so each destination row draws from one
    // contiguous run of one source row -- there is nothing to sample and nothing to round,
    // which is what makes it a memcpy per row rather than a loop per pixel
    for(int32_t dy = 0; dy < h; dy++) {
        int32_t sy = dy - off_y;
        if(sy < 0 || sy >= h)
            continue;

        // the run of destination columns whose source column is inside the buffer
        int32_t dx0 = off_x > 0 ? off_x : 0;
        int32_t dx1 = off_x < 0 ? w + off_x : w;
        if(dx1 <= dx0)
            continue;

        const uint8_t *s = &src[((uint32_t)sy * (uint32_t)w + (uint32_t)(dx0 - off_x)) * 2];
        uint8_t *d = &ctx->buffer[((uint32_t)dy * (uint32_t)w + (uint32_t)dx0) * 2];
        memcpy(d, s, (size_t)(dx1 - dx0) * 2);
    }
}

void light_draw_transform_rect(const light_draw_context_t *ctx, light_draw_point2d p0, light_draw_point2d p1,
                         light_draw_point2d *out_min, light_draw_point2d *out_max)
{
    light_draw_point2d a = _apply_transform(ctx, p0);
    light_draw_point2d b = _apply_transform(ctx, p1);

    out_min->x = a.x < b.x ? a.x : b.x;
    out_min->y = a.y < b.y ? a.y : b.y;
    out_max->x = a.x > b.x ? a.x : b.x;
    out_max->y = a.y > b.y ? a.y : b.y;
}

void _set_pixel(const light_draw_context_t *ctx, light_draw_point2d p, uint32_t color)
{
#ifdef LIGHT_DRAW_DEBUG_PIXEL_TRACE
    trace_log_f("(%d,%d) = %d", p.x, p.y, color);
#endif
    light_draw_point2d phys = _apply_transform(ctx, p);
    // resolve pixel address in buffer, after 2d transforms
    if(ctx->px_bits == 1) {     // monochrome image: 8px per byte
        uint8_t width_bytes = (ctx->phys_dim_x / 8) + ((ctx->phys_dim_x % 8)? 1 : 0);
        uint8_t *buf_byte = &ctx->buffer[phys.y * width_bytes + phys.x / 8];
        if(color) {
            *buf_byte = *buf_byte | (1 << phys.x % 8);
        } else {
            *buf_byte = *buf_byte & ~(1 << phys.x % 8);
        }
    } else if(ctx->px_bits == 16) {     // RGB565, big-endian (matches ST7789's native RAMWR order)
        uint32_t offset = ((uint32_t)phys.y * ctx->phys_dim_x + phys.x) * 2;
        ctx->buffer[offset]     = (uint8_t)(color >> 8);
        ctx->buffer[offset + 1] = (uint8_t)(color & 0xFF);
    }
/*
#ifdef LIGHT_DRAW_DEBUG_DISPLAY_PIXEL
    light_draw_debug_buffer_print_stdout(ctx);
#endif
*/
}

uint32_t _get_pixel(const light_draw_context_t *ctx, light_draw_point2d p)
{
    light_draw_point2d phys = _apply_transform(ctx, p);
    if(ctx->px_bits == 1) {     // monochrome image: 8px per byte
        uint8_t width_bytes = (ctx->phys_dim_x / 8) + ((ctx->phys_dim_x % 8)? 1 : 0);
        uint8_t px_block = ctx->buffer[phys.y * width_bytes + phys.x / 8];
        uint32_t out = px_block & (1 << phys.x % 8);
        return out;
    } else if(ctx->px_bits == 16) {
        uint32_t offset = ((uint32_t)phys.y * ctx->phys_dim_x + phys.x) * 2;
        return ((uint32_t)ctx->buffer[offset] << 8) | ctx->buffer[offset + 1];
    }
    return 0;
}

// light_draw_point2d's fields are uint16_t, so an expression like "centre.x - p.y" that goes
// negative (e.g. drawing a circle/point near the top-left of the canvas) silently
// wraps to a huge unsigned value once stored back into a light_draw_point2d -- _set_pixel()
// would then index far outside the buffer. this clips any such point instead of
// wrapping, computing each candidate coordinate as a signed value first
static void _set_pixel_clipped(const light_draw_context_t *ctx, int32_t x, int32_t y, uint32_t color)
{
    // against the context's clip rather than the raw canvas: the clip defaults to the whole
    // canvas and can never exceed it (set_clip clamps), so this is the same buffer-safety
    // check it always was, plus whatever the caller narrowed it to
    if(x < (int32_t)ctx->clip_x0 || y < (int32_t)ctx->clip_y0
                    || x > (int32_t)ctx->clip_x1 || y > (int32_t)ctx->clip_y1)
        return;
    _set_pixel(ctx, (light_draw_point2d) { (uint16_t)x, (uint16_t)y }, color);
}
// TODO add optimized version to fill all pixels along horizontal spans using memset
void _set_octant_pixels(const light_draw_context_t *ctx, light_draw_point2d centre, light_draw_point2d p, uint32_t color)
{
    int32_t cx = centre.x, cy = centre.y, px = p.x, py = p.y;
    // iterate through octants in clockwise order
    _set_pixel_clipped(ctx, cx + px, cy + py, color);
    _set_pixel_clipped(ctx, cx + py, cy + px, color);
    _set_pixel_clipped(ctx, cx + py, cy - px, color);
    _set_pixel_clipped(ctx, cx + px, cy - py, color);
    _set_pixel_clipped(ctx, cx - px, cy - py, color);
    _set_pixel_clipped(ctx, cx - py, cy - px, color);
    _set_pixel_clipped(ctx, cx - py, cy + px, color);
    _set_pixel_clipped(ctx, cx - px, cy + py, color);
}
// a horizontal run, clipped like _set_pixel_clipped() and for the same reason: a span
// covering a circle near an edge extends past it, and the endpoints are computed signed
static void _set_span_clipped(const light_draw_context_t *ctx, int32_t x0, int32_t x1, int32_t y,
                              uint32_t color)
{
    // the context's clip, not the raw canvas -- see _set_pixel_clipped()
    if(y < (int32_t)ctx->clip_y0 || y > (int32_t)ctx->clip_y1)
        return;
    if(x0 > x1) { int32_t t = x0; x0 = x1; x1 = t; }
    if(x1 < (int32_t)ctx->clip_x0 || x0 > (int32_t)ctx->clip_x1)
        return;
    if(x0 < (int32_t)ctx->clip_x0) x0 = ctx->clip_x0;
    if(x1 > (int32_t)ctx->clip_x1) x1 = ctx->clip_x1;
    for(int32_t x = x0; x <= x1; x++)
        _set_pixel(ctx, (light_draw_point2d) { (uint16_t)x, (uint16_t)y }, color);
}
// the filled counterpart of _set_octant_pixels(): the same midpoint iteration, but each pair
// of mirrored points becomes the horizontal run between them. filling from the outline's own
// spans is what keeps the two exactly consistent -- a disc drawn this way has precisely the
// extent its outline would have had
static void _set_spans_circle(const light_draw_context_t *ctx, light_draw_point2d centre, uint16_t radius,
                              uint32_t color)
{
    int32_t cx = centre.x, cy = centre.y;
    int32_t d = 3 - 2 * (int32_t)radius;
    int32_t px = 0, py = radius;

    while(py >= px) {
        _set_span_clipped(ctx, cx - px, cx + px, cy + py, color);
        _set_span_clipped(ctx, cx - px, cx + px, cy - py, color);
        _set_span_clipped(ctx, cx - py, cx + py, cy + px, color);
        _set_span_clipped(ctx, cx - py, cx + py, cy - px, color);
        px++;
        if(d > 0) {
            py--;
            d += 4 * (px - py) + 10;
        } else {
            d += 4 * px + 6;
        }
    }
}
void _set_pixels_circle(const light_draw_context_t *ctx, light_draw_point2d centre, uint16_t radius, uint32_t color)
{
    int32_t d = 3 - 2 * (int32_t)radius;
    light_draw_point2d p = {0, radius};

    _set_octant_pixels(ctx, centre, p, color);
    while(p.y >= p.x) {
        p.x++;
        if(d > 0)  {
            p.y--;
            d += 4 * (p.x - p.y) + 10;
        } else {
            d += 4 * p.x + 6;
        }
        _set_octant_pixels(ctx, centre, p, color);
    }
}

light_draw_context_t *_context_create(const uint8_t *name, uint16_t width, uint16_t height, uint8_t px_bits)
{
    // matches ctx->buffer_length's own type (size_t) -- a plain uint16_t here silently
    // wrapped for any buffer over 64KB (e.g. a 240x280 16bpp context is 134400 bytes),
    // allocating and memset-ing far too little and corrupting memory on first use
    size_t buffer_length;
    if(px_bits == 1) {
        uint16_t width_bytes = width % 8 == 0? width / 8: width / 8 + 1;
        buffer_length = width_bytes * height;
    } else {
        uint16_t px_bytes = px_bits % 8 == 0? px_bits / 8: px_bits / 8 + 1;
        buffer_length = width * height * px_bytes;
    }
    light_draw_context_t *ctx = malloc(sizeof(light_draw_context_t));
    ctx->name = name;
    ctx->dim_x = width;
    ctx->dim_y = height;
    ctx->phys_dim_x = width;
    ctx->phys_dim_y = height;
    ctx->rotation = LIGHT_DRAW_ROTATE_0;
    ctx->flip = LIGHT_DRAW_FLIP_NONE;
    _recompute_transform(ctx);
    ctx->px_bits = px_bits;
    ctx->buffer_length = buffer_length;
    ctx->buffer = malloc(buffer_length);
    memset(ctx->buffer, 0, buffer_length);
    ctx->buffer_back = NULL;
    ctx->font = NULL;
    ctx->color_bg = LIGHT_DRAW_BLACK;
    ctx->color_fg = LIGHT_DRAW_WHITE;
    return ctx;
}

void _context_set_font(light_draw_context_t *ctx, const light_draw_font_t *font)
{
    ctx->font = font;
}

void _context_enable_double_buffer(light_draw_context_t *ctx)
{
    if(ctx->buffer_back)
        return;
    ctx->buffer_back = malloc(ctx->buffer_length);
    memset(ctx->buffer_back, 0, ctx->buffer_length);
}

bool _context_swap_buffers(light_draw_context_t *ctx)
{
    if(!ctx->buffer_back)
        return false;
    uint8_t *front = ctx->buffer;
    ctx->buffer = ctx->buffer_back;
    ctx->buffer_back = front;
    return true;
}

void _context_set_rotation(light_draw_context_t *ctx, uint8_t rotation)
{
    ctx->rotation = rotation;
    // dim_x/dim_y are (re)derived inside _recompute_transform(), since flip also
    // depends on them and both need to stay in sync with whichever changed last
    _recompute_transform(ctx);
}

void _context_set_flip(light_draw_context_t *ctx, uint8_t flip)
{
    ctx->flip = flip;
    _recompute_transform(ctx);
}

void _context_set_clip(light_draw_context_t *ctx, uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1)
{
    //   clamped to the canvas, which is what lets _set_pixel_clipped() test ONLY the clip:
    // a clip can never authorise a write the canvas would have refused. a degenerate
    // rectangle (x0 > x1) is allowed through the same arithmetic and simply clips everything
    if(x1 > ctx->dim_x - 1) x1 = ctx->dim_x - 1;
    if(y1 > ctx->dim_y - 1) y1 = ctx->dim_y - 1;
    ctx->clip_x0 = x0;
    ctx->clip_y0 = y0;
    ctx->clip_x1 = x1;
    ctx->clip_y1 = y1;
}
void _context_clear_clip(light_draw_context_t *ctx)
{
    ctx->clip_x0 = 0;
    ctx->clip_y0 = 0;
    ctx->clip_x1 = ctx->dim_x - 1;
    ctx->clip_y1 = ctx->dim_y - 1;
}

// TODO implement using light_draw_draw_point so point radius setting is observed
void _draw_circle(const light_draw_context_t *ctx, light_draw_point2d p, uint16_t radius, bool fill)
{
    // fill was accepted and then ignored here for a long time, so every caller asking for a
    // disc quietly got a ring -- including screen-test's animated "circle"
    if(fill)
        _set_spans_circle(ctx, p, radius, ctx->color_fg);
    else
        _set_pixels_circle(ctx, p, radius, ctx->color_fg);
}

void _draw_point(const light_draw_context_t *ctx, light_draw_point2d p)
{
    // a point is a solid dot; it drew as a tiny ring only because _draw_circle() had no fill
    // path to share
    _set_spans_circle(ctx, p, ctx->point_radius, ctx->color_fg);
}

void _draw_arc(const light_draw_context_t *ctx, light_draw_point2d centre, uint16_t radius,
               int16_t start_deg, int16_t end_deg)
{
    if(radius == 0) {
        _set_pixel_clipped(ctx, centre.x, centre.y, ctx->color_fg);
        return;
    }

    int32_t start = start_deg % 360;
    if(start < 0) start += 360;
    int32_t end = end_deg % 360;
    if(end < 0) end += 360;
    // an end before the start sweeps the long way round rather than drawing nothing, and
    // coincident angles mean the whole circle -- which is what arc(0, 360) reduces to once
    // both ends wrap
    int32_t span = end - start;
    if(span <= 0) span += 360;

    int32_t start_q8 = start << 8;
    int32_t span_q8 = span << 8;
    // arc length is r*theta, so a one-pixel step is 1/r radians -- but sampling at exactly
    // that spacing is not enough. rounding moves a coordinate by up to half a pixel, so two
    // samples a whole pixel apart can round to pixels TWO apart and leave a hole. at half
    // that spacing the rounded samples can never differ by more than one on either axis,
    // which is the no-gap guarantee this function owes light_draw_draw_rect_rounded().
    // 1144/2^24 is pi/(180*256): Q8 degrees to radians. 64-bit because r*span_q8 alone
    // reaches 6e9 at the extremes of both
    int32_t steps = (int32_t)(((int64_t)radius * span_q8 * 1144) >> 24) * 2 + 1;

    for(int32_t i = 0; i <= steps; i++) {
        // recomputed from the endpoints every step rather than accumulated, so the final
        // sample lands exactly on end_deg no matter how many steps it took to get there --
        // an accumulated angle would drift by a fraction of a step and open the joins that
        // rounded rectangles depend on being closed
        int32_t theta = start_q8 + (int32_t)(((int64_t)span_q8 * i) / steps);
        int32_t x = centre.x + (((int32_t)radius * _cos_deg_q8_q15(theta) + 16384) >> 15);
        int32_t y = centre.y + (((int32_t)radius * _sin_deg_q8_q15(theta) + 16384) >> 15);
        _set_pixel_clipped(ctx, x, y, ctx->color_fg);
    }
}

// integer square root, for the filled rounded rect's corner rows. Newton's method on
// integers, converging in a handful of iterations for anything a display can hold
static uint32_t _isqrt(uint32_t n)
{
    if(n == 0)
        return 0;
    uint32_t x = n, y = (x + 1) / 2;
    while(y < x) {
        x = y;
        y = (x + n / x) / 2;
    }
    return x;
}

void _draw_rect_rounded(const light_draw_context_t *ctx, light_draw_point2d p0, light_draw_point2d p1,
                        uint16_t radius, uint8_t corners, bool fill)
{
    int32_t x0 = p0.x < p1.x ? p0.x : p1.x;
    int32_t x1 = p0.x > p1.x ? p0.x : p1.x;
    int32_t y0 = p0.y < p1.y ? p0.y : p1.y;
    int32_t y1 = p0.y > p1.y ? p0.y : p1.y;

    // clamp to half the shorter side: beyond that the corner arcs would overlap each other
    // and the straight edges would have negative length. clamping degenerates the shape into
    // a stadium (or a circle, for a square), which is the sensible limit of what was asked
    // for rather than a drawing made of nonsense coordinates
    int32_t half_w = (x1 - x0) / 2;
    int32_t half_h = (y1 - y0) / 2;
    int32_t r = radius;
    if(r > half_w) r = half_w;
    if(r > half_h) r = half_h;
    if(r <= 0 || corners == LIGHT_DRAW_CORNER_NONE) {
        _draw_rect_norm(ctx, (light_draw_point2d) { (uint16_t)x0, (uint16_t)y0 },
                             (light_draw_point2d) { (uint16_t)x1, (uint16_t)y1 }, fill);
        return;
    }

    // how far each corner's arc is inset -- 0 for a corner left square, which makes the edge
    // and span arithmetic below uniform instead of needing a branch per corner
    int32_t tl = (corners & LIGHT_DRAW_CORNER_TOP_LEFT)     ? r : 0;
    int32_t tr = (corners & LIGHT_DRAW_CORNER_TOP_RIGHT)    ? r : 0;
    int32_t br = (corners & LIGHT_DRAW_CORNER_BOTTOM_RIGHT) ? r : 0;
    int32_t bl = (corners & LIGHT_DRAW_CORNER_BOTTOM_LEFT)  ? r : 0;

    if(fill) {
        // one pass over the rows, each narrowed by however far the arcs at ITS end reach in.
        // the clamp above guarantees y0+r <= y1-r, so the top and bottom bands never overlap
        // and a row can be in at most one of them
        for(int32_t y = y0; y <= y1; y++) {
            int32_t li = 0, ri = 0;
            if(y < y0 + r) {
                int32_t dy = y0 + r - y;
                int32_t d = r - (int32_t)_isqrt((uint32_t)(r * r - dy * dy));
                if(tl) li = d;
                if(tr) ri = d;
            } else if(y > y1 - r) {
                int32_t dy = y - (y1 - r);
                int32_t d = r - (int32_t)_isqrt((uint32_t)(r * r - dy * dy));
                if(bl) li = d;
                if(br) ri = d;
            }
            _set_span_clipped(ctx, x0 + li, x1 - ri, y, ctx->color_fg);
        }
        return;
    }

    // straight edges, each shortened only at the ends whose corner is actually rounded, so it
    // stops exactly where its arc starts. the arcs' own endpoints land on these same pixels
    // (cos/sin of 0, 90, 180 and 270 are exact in the table), so the joins close without
    // overlap arithmetic
    _draw_line(ctx, (light_draw_point2d) { (uint16_t)(x0 + tl), (uint16_t)y0 },
                    (light_draw_point2d) { (uint16_t)(x1 - tr), (uint16_t)y0 }, true);
    _draw_line(ctx, (light_draw_point2d) { (uint16_t)(x0 + bl), (uint16_t)y1 },
                    (light_draw_point2d) { (uint16_t)(x1 - br), (uint16_t)y1 }, true);
    _draw_line(ctx, (light_draw_point2d) { (uint16_t)x0, (uint16_t)(y0 + tl) },
                    (light_draw_point2d) { (uint16_t)x0, (uint16_t)(y1 - bl) }, true);
    _draw_line(ctx, (light_draw_point2d) { (uint16_t)x1, (uint16_t)(y0 + tr) },
                    (light_draw_point2d) { (uint16_t)x1, (uint16_t)(y1 - br) }, true);

    // 0 points right and angles run clockwise on screen, so 180->270 is the top-LEFT quarter
    if(tl) _draw_arc(ctx, (light_draw_point2d) { (uint16_t)(x0 + r), (uint16_t)(y0 + r) },
                     (uint16_t)r, 180, 270);
    if(tr) _draw_arc(ctx, (light_draw_point2d) { (uint16_t)(x1 - r), (uint16_t)(y0 + r) },
                     (uint16_t)r, 270, 360);
    if(br) _draw_arc(ctx, (light_draw_point2d) { (uint16_t)(x1 - r), (uint16_t)(y1 - r) },
                     (uint16_t)r, 0, 90);
    if(bl) _draw_arc(ctx, (light_draw_point2d) { (uint16_t)(x0 + r), (uint16_t)(y1 - r) },
                     (uint16_t)r, 90, 180);
}

void _draw_line(const light_draw_context_t *ctx, light_draw_point2d p0, light_draw_point2d p1, bool solid)
{
    light_draw_point2d p;
    int8_t sx, sy;
    int32_t dx, dy, error, err2;

    dx = abs(p1.x - p0.x);
    sx = p0.x < p1.x? 1 : -1;
    dy = -1 * abs(p1.y - p0.y);
    sy = p0.y < p1.y? 1 : -1;
    error = dx + dy;
    p = p0;

    while (1)
    {
        // clipped, so a line participates in the context clip like every other primitive --
        // rect borders are drawn as lines, and a scrolled widget's border has to stop at
        // its container's edge exactly where its fill does
        _set_pixel_clipped(ctx, p.x, p.y, ctx->color_fg);
        err2 = 2 * error;

        if(err2 >= dy) {
            if(p.x == p1.x) break;
            error += dy;
            p.x += sx;
        }
        if(err2 <= dx) {
            if(p.y == p1.y) break;
            error += dx;
            p.y += sy;
        }
    }
}

void _draw_clear(const light_draw_context_t *ctx)
{
    if(ctx->px_bits == 1) {
        uint8_t value = ctx->color_bg? 0xFF : 0x00;
        memset(ctx->buffer, value, ctx->buffer_length);
    } else if(ctx->px_bits == 16) {
        uint8_t hi = (uint8_t)(ctx->color_bg >> 8);
        uint8_t lo = (uint8_t)(ctx->color_bg & 0xFF);
        for(size_t i = 0; i < ctx->buffer_length; i += 2) {
            ctx->buffer[i]     = hi;
            ctx->buffer[i + 1] = lo;
        }
    }
}
void _draw_text(const light_draw_context_t *ctx,
                    light_draw_point2d origin, const uint8_t *text)
{
    if(!ctx->font) return;
    const light_draw_font_t *font = ctx->font;
    uint8_t pitch = (font->char_width + 7) / 8;
    light_draw_point2d pen = origin;
    for(const uint8_t *c = text; *c; c++) {
        if(*c < LIGHT_DRAW_FONT_GLYPH_TABLE_SIZE) {
            const uint8_t *glyph = font->glyphs[*c];
            if(glyph) {
                for(uint8_t y = 0; y < font->char_height; y++) {
                    for(uint8_t x = 0; x < font->char_width; x++) {
                        uint8_t byte = glyph[y * pitch + x / 8];
                        if((byte >> (7 - (x % 8))) & 1) {
                            //   clipped, which does two jobs: text participates in the
                            // context clip, and a glyph walked past the canvas edge is now
                            // dropped HERE rather than being an out-of-bounds buffer write
                            // callers had to pre-guard against
                            _set_pixel_clipped(ctx, pen.x + x, pen.y + y, ctx->color_fg);
                        }
                    }
                }
            }
        }
        pen.x += font->char_width;
    }
}
// tail chaining function to rearrange input geometry -- callers may pass either diagonal's
// two corners in any order, so normalize independently per axis into (top-left, bottom-right)
// before handing off to _draw_rect_norm(), which assumes that canonical form
void _draw_rect(const light_draw_context_t *ctx,
                    light_draw_point2d p0, light_draw_point2d p1, bool fill)
{
        light_draw_point2d top_left     = { p0.x < p1.x ? p0.x : p1.x, p0.y < p1.y ? p0.y : p1.y };
        light_draw_point2d bottom_right = { p0.x > p1.x ? p0.x : p1.x, p0.y > p1.y ? p0.y : p1.y };
        return _draw_rect_norm(ctx, top_left, bottom_right, fill);
}
void _draw_rect_norm(const light_draw_context_t *ctx,
                    light_draw_point2d p0, light_draw_point2d p1, bool fill)
{
        // ASSERT ( p0.y < p1.y && p0.x < p1.x )
        // assertion: the gradient from p0 -> p1 is positive
        if(fill) {
                for(uint16_t y = p0.y; y <= p1.y; y++) {
                        _draw_line(ctx, (light_draw_point2d) { p0.x, y }, (light_draw_point2d) { p1.x, y }, true);
                }
                return;
        }
        _draw_line(ctx, p0, (light_draw_point2d) { p0.x, p1.y }, true);
        _draw_line(ctx, p0, (light_draw_point2d) { p1.x, p0.y }, true);
        _draw_line(ctx, (light_draw_point2d) { p0.x, p1.y }, p1, true);
        _draw_line(ctx, (light_draw_point2d) { p1.x, p0.y }, p1, true);
}

uint8_t *_buffer_to_string(const light_draw_context_t *ctx)
{
    size_t out_len;
    if(ctx->px_bits == 1) {
        out_len = (ctx->dim_x + 1) * ctx->dim_y + 1;
    }
    uint8_t *out = calloc(sizeof(uint8_t), out_len);
    memset(out, ' ', out_len);

    uint8_t *p = out;
    for(uint16_t y = 0; y < ctx->dim_y; y++) {
        for(uint16_t x = 0; x < ctx->dim_x; x++) {
            *(p++) = _get_pixel(ctx, (light_draw_point2d) {x,y})? '0' : '_';
        }
        *(p++) = '\n';
    }
    *p = '\0';
    return out;
}
void _buffer_print_stdout(const light_draw_context_t *ctx)
{
    uint8_t width = ctx->dim_x;
    uint8_t *frame = _buffer_to_string(ctx);
    uint8_t *border = calloc(sizeof(uint8_t), width + 1);
    memset(border, '#', width);
    border[width] = '\0';
    printf("%s\n%s%s\n", border, frame, border);
}