#ifndef _LIGHT_DRAW_INTERNAL_H
#define _LIGHT_DRAW_INTERNAL_H

#ifndef _LIGHT_DRAW_H
#error "This file must be included after light_draw.h"
#endif

#include <stdint.h>
#include <stdbool.h>

#ifdef LIGHT_DRAW_DEBUG_API_TRACE
//#   define trace_log(...) printf("TRACE: " __func__ __VA_ARGS__ "\n");
#   define trace_log() trace_log_f("","")
#   define trace_log_f(msg, ...) light_trace(msg, __VA_ARGS__)
#else
#   define trace_log(...)
#   define trace_log_f(msg, ...)
#endif

light_draw_context_t *_context_create(const uint8_t *name, uint16_t width, uint16_t height, uint8_t px_bits);
void _context_set_font(light_draw_context_t *ctx, const light_draw_font_t *font);
void _context_set_rotation(light_draw_context_t *ctx, uint8_t rotation);
void _context_set_flip(light_draw_context_t *ctx, uint8_t flip);
void _context_set_clip(light_draw_context_t *ctx, uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1);
void _context_clear_clip(light_draw_context_t *ctx);
void _context_enable_double_buffer(light_draw_context_t *ctx);
bool _context_swap_buffers(light_draw_context_t *ctx);

void _draw_circle(const light_draw_context_t *ctx, light_draw_point2d centre, uint16_t radius, bool fill);
void _draw_arc(const light_draw_context_t *ctx, light_draw_point2d centre, uint16_t radius,
                    int16_t start_deg, int16_t end_deg);
void _draw_rect_rounded(const light_draw_context_t *ctx, light_draw_point2d p0, light_draw_point2d p1,
                    uint16_t radius, uint8_t corners, bool fill);
void _draw_point(const light_draw_context_t *img, light_draw_point2d p);
void _draw_line(const light_draw_context_t *ctx, light_draw_point2d p0, light_draw_point2d p1, bool solid);
void _draw_clear(const light_draw_context_t *ctx);
void _draw_text(const light_draw_context_t *ctx,
                    light_draw_point2d p, const uint8_t *text);
void _draw_rect(const light_draw_context_t *ctx,
                    light_draw_point2d p0, light_draw_point2d p1, bool fill);
void _draw_rect_norm(const light_draw_context_t *ctx,
                    light_draw_point2d p0, light_draw_point2d p1, bool fill);

uint8_t *_buffer_to_string(const light_draw_context_t *ctx);
void _buffer_print_stdout(const light_draw_context_t *ctx);

#endif