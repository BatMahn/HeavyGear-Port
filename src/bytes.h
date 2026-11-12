/* bytes.h - little-endian readers (the data is little-endian; the port makes no host-order assumption) */
#ifndef HG_BYTES_H
#define HG_BYTES_H
#include <stdint.h>
#include <string.h>
static inline uint16_t rd_u16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static inline int16_t  rd_i16(const uint8_t *p) { return (int16_t)rd_u16(p); }
static inline uint32_t rd_u32(const uint8_t *p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }
static inline int32_t  rd_i32(const uint8_t *p) { return (int32_t)rd_u32(p); }
static inline float    rd_f32(const uint8_t *p) { uint32_t u = rd_u32(p); float f; memcpy(&f, &u, 4); return f; }
static inline double   rd_f64(const uint8_t *p)
{
    uint64_t u = rd_u32(p) | ((uint64_t)rd_u32(p + 4) << 32);
    double d;
    memcpy(&d, &u, 8);
    return d;
}
#endif
