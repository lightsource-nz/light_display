/*
 *  light_draw_api.c
 *  dispatch layer for light_draw API functions
 * 
 *  authored by Alex Fulton
 *  created august 2022
 * 
 */

#include <light_draw.h>
#include "light_draw_internal.h"

#include <stdio.h>
#include <string.h>

light_draw_context_t *light_draw_context_create(const uint8_t *name, uint16_t width, uint16_t height, uint8_t px_bits)
{
    trace_log_f("\"%s\": [%dx%d] @ %db", name, width, height, px_bits);
    return _context_create(name, width, height, px_bits);
}
void light_draw_context_set_font(light_draw_context_t *ctx, const light_draw_font_t *font)
{
    trace_log();
    _context_set_font(ctx, font);
}
void light_draw_context_set_rotation(light_draw_context_t *ctx, uint8_t rotation)
{
    trace_log_f("rotation=%d", rotation);
    _context_set_rotation(ctx, rotation);
}
void light_draw_context_set_flip(light_draw_context_t *ctx, uint8_t flip)
{
    trace_log_f("flip=%d", flip);
    _context_set_flip(ctx, flip);
}
void light_draw_context_set_clip(light_draw_context_t *ctx,
                uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1)
{
    trace_log_f("clip=(%d,%d)-(%d,%d)", x0, y0, x1, y1);
    _context_set_clip(ctx, x0, y0, x1, y1);
}
void light_draw_context_clear_clip(light_draw_context_t *ctx)
{
    trace_log();
    _context_clear_clip(ctx);
}
void light_draw_context_enable_double_buffer(light_draw_context_t *ctx)
{
    trace_log();
    _context_enable_double_buffer(ctx);
}
bool light_draw_context_swap_buffers(light_draw_context_t *ctx)
{
    trace_log();
    return _context_swap_buffers(ctx);
}
void light_draw_draw_circle(const light_draw_context_t *ctx, light_draw_point2d centre, uint16_t radius, bool fill)
{
    trace_log_f("(%d,%d), radius=%d, fill=%d", centre.x, centre.y, radius, fill);
    _draw_circle(ctx, centre, radius, fill);
}
void light_draw_draw_arc(const light_draw_context_t *ctx, light_draw_point2d centre, uint16_t radius,
                   int16_t start_deg, int16_t end_deg)
{
    trace_log_f("(%d,%d), radius=%d, %d..%d deg", centre.x, centre.y, radius, start_deg, end_deg);
    _draw_arc(ctx, centre, radius, start_deg, end_deg);
}
void light_draw_draw_rect_rounded(const light_draw_context_t *ctx, light_draw_point2d p0, light_draw_point2d p1,
                            uint16_t radius, bool fill)
{
    trace_log_f("(%d,%d)->(%d,%d), radius=%d", p0.x, p0.y, p1.x, p1.y, radius);
    _draw_rect_rounded(ctx, p0, p1, radius, LIGHT_DRAW_CORNER_ALL, fill);
}
void light_draw_draw_rect_rounded_corners(const light_draw_context_t *ctx, light_draw_point2d p0, light_draw_point2d p1,
                            uint16_t radius, uint8_t corners, bool fill)
{
    trace_log_f("(%d,%d)->(%d,%d), radius=%d, corners=0x%x", p0.x, p0.y, p1.x, p1.y, radius, corners);
    _draw_rect_rounded(ctx, p0, p1, radius, corners, fill);
}
void light_draw_draw_point(const light_draw_context_t *ctx, light_draw_point2d p)
{
    trace_log_f("(%d,%d)", p.x, p.y);
    _draw_point(ctx, p);
}
void light_draw_draw_line(const light_draw_context_t *ctx, light_draw_point2d p0, light_draw_point2d p1, bool solid)
{
    trace_log_f("(%d,%d)->(%d,%d)", p0.x, p0.y, p1.x, p1.y);
    _draw_line(ctx, p0, p1, solid);
}
void light_draw_draw_clear(const light_draw_context_t *ctx)
{
    trace_log();
    _draw_clear(ctx);
}
void light_draw_draw_text(const light_draw_context_t *ctx,
                    light_draw_point2d p, const uint8_t *text)
{
    trace_log_f("(%d,%d): \"%s\"", p.x, p.y, text);
    _draw_text(ctx, p, text);
}
void light_draw_draw_rect(const light_draw_context_t *ctx,
                    light_draw_point2d p0, light_draw_point2d p1, bool fill)
{
    trace_log_f("(%d,%d)->(%d,%d)", p0.x, p0.y, p1.x, p1.y);
    _draw_rect(ctx, p0, p1, fill);
}
uint8_t *light_draw_buffer_to_string(const light_draw_context_t *ctx)
{
    trace_log();
    return _buffer_to_string(ctx);
}
void light_draw_debug_buffer_print_stdout(const light_draw_context_t *ctx)
{
    trace_log();
    _buffer_print_stdout(ctx);
}
