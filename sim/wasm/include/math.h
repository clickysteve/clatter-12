/* Freestanding <math.h> for the WebAssembly build of the panel: builtins, and CLATTER's libm
 * (third_party/clatter-pocket/lxr/bridge/libm.c) for the rest. GPL-3.0-only */
#ifndef C12W_MATH_H
#define C12W_MATH_H
#define M_PI 3.14159265358979323846
#define M_PI_2 1.57079632679489661923
#define M_LN2 0.69314718055994530942
#define M_E 2.7182818284590452354
#define INFINITY (__builtin_inff())
#define NAN (__builtin_nanf(""))
#ifdef __cplusplus
extern "C" {
#endif
float expf(float x);
float sinf(float x);
float cosf(float x);
float powf(float x, float y);
float tanhf(float x);
double exp(double x);
double sin(double x);
double cos(double x);
double pow(double x, double y);
double log(double x);
#ifdef __cplusplus
}
#endif
static inline float sqrtf(float x) { return __builtin_sqrtf(x); }
static inline float fabsf(float x) { return __builtin_fabsf(x); }
static inline float floorf(float x) { return __builtin_floorf(x); }
static inline float ceilf(float x) { return __builtin_ceilf(x); }
static inline float truncf(float x) { return __builtin_truncf(x); }
static inline double sqrt(double x) { return __builtin_sqrt(x); }
static inline double fabs(double x) { return __builtin_fabs(x); }
static inline double floor(double x) { return __builtin_floor(x); }
#endif
