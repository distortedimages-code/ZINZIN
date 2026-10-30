/* ZINZIN: the maths the engine uses. On the FM-1 (and in fm1-base's host simulator) it is fm1-base's fastmath.h,
 * single precision with no libm; ZZ_LIBM maps the same names to the C library, for the project's own test bench. */
#pragma once
#include <stdint.h>

#ifdef ZZ_LIBM
#include <math.h>
#define zz_tanhf tanhf
#define zz_expf expf
#define zz_powf powf
#define zz_tanf tanf
#define zz_sinf sinf
#define zz_cosf cosf
#define zz_fabsf fabsf
#else
#include "fastmath.h"
#define zz_tanhf fm_tanhf
#define zz_expf fm_expf
#define zz_powf fm_powf
#define zz_tanf fm_tanf
#define zz_sinf fm_sinf
#define zz_cosf fm_cosf
#define zz_fabsf fm_fabsf
#endif

#define ZZ_PI 3.14159265358979f

/* floor for the small numbers the engine wraps (phases, indices) */
static inline float zz_floorf(float x)
{
    int32_t i = (int32_t)x;
    return (float)(i - (x < (float)i));
}
static inline float zz_clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }
static inline float zz_sm(float x, float a, float b)       /* smoothstep from a to b */
{
    float t = zz_clamp01((x - a) / (b - a));
    return t * t * (3.0f - 2.0f * t);
}
static inline float zz_bump(float x, float mu, float w)
{
    float d = (x - mu) / w;
    return zz_expf(-d * d);
}
