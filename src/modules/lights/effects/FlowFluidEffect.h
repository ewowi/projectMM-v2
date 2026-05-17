#pragma once
//
// FlowFluidEffect — stable-fluids (Navier-Stokes) simulation.
//
// Velocity field (u,v) advecting RGB dye each frame:
//   velocity: diffuse → project → self-advect → project → optional vorticity
//   dye: diffuse + advect through final velocity → per-frame dissipation
// Built-in orbiting emitter injects coloured dye + velocity — self-contained.
// Memory: 12 float arrays of w*h (PSRAM). 32×32 ≈ 48 KB. 2D only.
// Ported from v1 FlowFluidEffect (Jos Stam stable-fluids 1999).
//
// Controls:
//   layout:     id of a layout module (text, default "layout-0")  [from base]
//   viscosity:  0–255 → 0..0.002 (default 13)
//   diffusion:  0–255 → 0..0.002 (default 13)
//   vel_dissip: 0–255 → 0..1     (default 191)
//   dye_dissip: 0–255 → 0..1     (default 64)
//   vorticity:  0–255 → 0..20    (default 89)
//   speed:      1–240 emitter BPM (default 30)
//

#include <cmath>
#include <cstring>

#include "../../../pal/Pal.h"
#include "../../../pal/PalHeap.h"
#include "../RGB.h"
#include "PixelEffectBase.h"

namespace pmm {

class FlowFluidEffect : public PixelEffectBase {
 public:
  const char* category() const override { return "effect"; }

  void teardown() override {
    free_fields_();
    PixelEffectBase::teardown();
  }

 protected:
  void build_effect_controls() override {
    addControl(viscosity_,  "viscosity",  "slider", (uint8_t)0, (uint8_t)255);
    addControl(diffusion_,  "diffusion",  "slider", (uint8_t)0, (uint8_t)255);
    addControl(vel_dissip_, "vel_dissip", "slider", (uint8_t)0, (uint8_t)255);
    addControl(dye_dissip_, "dye_dissip", "slider", (uint8_t)0, (uint8_t)255);
    addControl(vorticity_,  "vorticity",  "slider", (uint8_t)0, (uint8_t)255);
    addControl(speed_,      "speed",      "slider", (uint8_t)1, (uint8_t)240);
  }

  // Base calls this after (re)allocating pixels_ at the new geometry.
  void on_geometry_(uint16_t w, uint16_t h, uint16_t /*d*/) override {
    free_fields_();
    w_ = w; h_ = h; n_ = (uint32_t)w * h;
    u_      = palloc_(n_); v_      = palloc_(n_);
    u_prev_ = palloc_(n_); v_prev_ = palloc_(n_);
    press_  = palloc_(n_); div_    = palloc_(n_);
    gr_ = palloc_(n_); gg_ = palloc_(n_); gb_ = palloc_(n_);
    tr_ = palloc_(n_); tg_ = palloc_(n_); tb_ = palloc_(n_);
    last_ms_ = 0; phase_ = 0.0f;
  }

  void render_(RGB* px, uint16_t /*w*/, uint16_t /*h*/, uint16_t /*d*/) override {
    if (!u_) return;
    const float viscosity = (float)viscosity_ * (0.002f / 255.0f);
    const float diffusion = (float)diffusion_ * (0.002f / 255.0f);
    const float velDissip = (float)vel_dissip_ * (1.0f / 255.0f);
    const float dyeDissip = (float)dye_dissip_ * (1.0f / 255.0f);
    const float vorticity = (float)vorticity_  * (20.0f / 255.0f);

    const uint32_t nowMs = pal::millis();
    float dt = (last_ms_ == 0) ? 0.016f : (float)(nowMs - last_ms_) * 0.001f;
    if (dt > 0.1f) dt = 0.1f;
    if (dt < 0.001f) dt = 0.001f;
    last_ms_ = nowMs;

    phase_ += (float)speed_ * dt * 0.1f;
    inject_(dt);

    const float dvg = 0.3f * dt;
    for (uint32_t i = 0; i < n_; ++i) v_[i] += dvg;

    std::memcpy(u_prev_, u_, n_ * sizeof(float));
    std::memcpy(v_prev_, v_, n_ * sizeof(float));
    diffuse_(1, u_, u_prev_, viscosity, dt);
    diffuse_(2, v_, v_prev_, viscosity, dt);
    project_();

    std::memcpy(u_prev_, u_, n_ * sizeof(float));
    std::memcpy(v_prev_, v_, n_ * sizeof(float));
    advect_(1, u_, u_prev_, u_prev_, v_prev_, dt);
    advect_(2, v_, v_prev_, u_prev_, v_prev_, dt);
    project_();

    if (vorticity > 0.0f) { vorticity_confinement_(dt, vorticity); project_(); }

    if (diffusion > 0.0f) {
      std::memcpy(tr_, gr_, n_ * sizeof(float));
      std::memcpy(tg_, gg_, n_ * sizeof(float));
      std::memcpy(tb_, gb_, n_ * sizeof(float));
      diffuse_(0, gr_, tr_, diffusion, dt);
      diffuse_(0, gg_, tg_, diffusion, dt);
      diffuse_(0, gb_, tb_, diffusion, dt);
    }
    std::memcpy(tr_, gr_, n_ * sizeof(float));
    std::memcpy(tg_, gg_, n_ * sizeof(float));
    std::memcpy(tb_, gb_, n_ * sizeof(float));
    advect_(0, gr_, tr_, u_, v_, dt);
    advect_(0, gg_, tg_, u_, v_, dt);
    advect_(0, gb_, tb_, u_, v_, dt);

    const float fv = std::pow(velDissip, dt);
    const float fd = std::pow(dyeDissip, dt);
    for (uint32_t i = 0; i < n_; ++i) {
      u_[i] *= fv; v_[i] *= fv;
      gr_[i] *= fd; gg_[i] *= fd; gb_[i] *= fd;
    }

    for (uint32_t i = 0; i < n_; ++i)
      px[i] = { to8_(gr_[i]), to8_(gg_[i]), to8_(gb_[i]) };
  }

 private:
  static constexpr int kIter = 5;

  uint8_t viscosity_  = 13;
  uint8_t diffusion_  = 13;
  uint8_t vel_dissip_ = 191;
  uint8_t dye_dissip_ = 64;
  uint8_t vorticity_  = 89;
  uint8_t speed_      = 30;

  uint16_t w_ = 0, h_ = 0;
  uint32_t n_ = 0;

  float* u_ = nullptr; float* v_ = nullptr;
  float* u_prev_ = nullptr; float* v_prev_ = nullptr;
  float* press_ = nullptr; float* div_ = nullptr;
  float* gr_ = nullptr; float* gg_ = nullptr; float* gb_ = nullptr;
  float* tr_ = nullptr; float* tg_ = nullptr; float* tb_ = nullptr;

  uint32_t last_ms_ = 0;
  float    phase_   = 0.0f;

  uint32_t at_(int x, int y) const { return (uint32_t)y * w_ + (uint32_t)x; }

  float sampleClamped_(const float* a, int y, int x) const {
    if (x < 0) x = 0; else if (x >= (int)w_) x = (int)w_ - 1;
    if (y < 0) y = 0; else if (y >= (int)h_) y = (int)h_ - 1;
    return a[at_(x, y)];
  }

  void setBnd_(int b, float* x) const {
    if (b == 1) {
      for (int y = 0; y < (int)h_; ++y) { x[at_(0,y)] = 0.0f; x[at_(w_-1,y)] = 0.0f; }
    } else if (b == 2) {
      for (int xc = 0; xc < (int)w_; ++xc) { x[at_(xc,0)] = 0.0f; x[at_(xc,h_-1)] = 0.0f; }
    }
  }

  void linSolve_(int b, float* x, const float* x0, float a, float c) const {
    const float invC = 1.0f / c;
    for (int k = 0; k < kIter; ++k) {
      for (int y = 0; y < (int)h_; ++y)
        for (int xc = 0; xc < (int)w_; ++xc) {
          const float xm = xc > 0 ? x[at_(xc-1,y)] : x[at_(xc,y)];
          const float xp = xc < (int)w_-1 ? x[at_(xc+1,y)] : x[at_(xc,y)];
          const float ym = y > 0 ? x[at_(xc,y-1)] : x[at_(xc,y)];
          const float yp = y < (int)h_-1 ? x[at_(xc,y+1)] : x[at_(xc,y)];
          x[at_(xc,y)] = (x0[at_(xc,y)] + a * (xm + xp + ym + yp)) * invC;
        }
      setBnd_(b, x);
    }
  }

  void diffuse_(int b, float* x, float* x0, float diff, float dt) const {
    if (diff <= 0.0f) { std::memcpy(x, x0, n_ * sizeof(float)); setBnd_(b, x); return; }
    const float sz = (float)(w_ < h_ ? w_ : h_);
    const float a  = dt * diff * sz * sz;
    linSolve_(b, x, x0, a, 1.0f + 4.0f * a);
  }

  void advect_(int b, float* d, const float* d0, const float* vu, const float* vv, float dt) const {
    const float sz   = (float)(w_ < h_ ? w_ : h_);
    const float dt0  = dt * sz;
    const float maxX = (float)w_ - 1.5f;
    const float maxY = (float)h_ - 1.5f;
    for (int y = 0; y < (int)h_; ++y)
      for (int xc = 0; xc < (int)w_; ++xc) {
        float sx = (float)xc - dt0 * vu[at_(xc,y)];
        float sy = (float)y  - dt0 * vv[at_(xc,y)];
        if (sx < 0.5f) sx = 0.5f; else if (sx > maxX) sx = maxX;
        if (sy < 0.5f) sy = 0.5f; else if (sy > maxY) sy = maxY;
        const int ix0 = (int)sx, iy0 = (int)sy;
        const int ix1 = ix0+1 < (int)w_ ? ix0+1 : (int)w_-1;
        const int iy1 = iy0+1 < (int)h_ ? iy0+1 : (int)h_-1;
        const float fx = sx - ix0, fy = sy - iy0;
        const float top = d0[at_(ix0,iy0)] * (1.0f-fx) + d0[at_(ix1,iy0)] * fx;
        const float bot = d0[at_(ix0,iy1)] * (1.0f-fx) + d0[at_(ix1,iy1)] * fx;
        d[at_(xc,y)] = top * (1.0f-fy) + bot * fy;
      }
    setBnd_(b, d);
  }

  void project_() {
    const float sz    = (float)(w_ < h_ ? w_ : h_);
    const float invSz = 1.0f / sz;
    for (int y = 0; y < (int)h_; ++y)
      for (int xc = 0; xc < (int)w_; ++xc) {
        const float uxp = xc < (int)w_-1 ? u_[at_(xc+1,y)] : u_[at_(xc,y)];
        const float uxm = xc > 0          ? u_[at_(xc-1,y)] : u_[at_(xc,y)];
        const float vyp = y < (int)h_-1   ? v_[at_(xc,y+1)] : v_[at_(xc,y)];
        const float vym = y > 0            ? v_[at_(xc,y-1)] : v_[at_(xc,y)];
        div_[at_(xc,y)]   = -0.5f * invSz * (uxp - uxm + vyp - vym);
        press_[at_(xc,y)] = 0.0f;
      }
    setBnd_(0, div_); setBnd_(0, press_);
    linSolve_(0, press_, div_, 1.0f, 4.0f);
    for (int y = 0; y < (int)h_; ++y)
      for (int xc = 0; xc < (int)w_; ++xc) {
        const float pxp = xc < (int)w_-1 ? press_[at_(xc+1,y)] : press_[at_(xc,y)];
        const float pxm = xc > 0          ? press_[at_(xc-1,y)] : press_[at_(xc,y)];
        const float pyp = y < (int)h_-1   ? press_[at_(xc,y+1)] : press_[at_(xc,y)];
        const float pym = y > 0            ? press_[at_(xc,y-1)] : press_[at_(xc,y)];
        u_[at_(xc,y)] -= 0.5f * (pxp - pxm) * sz;
        v_[at_(xc,y)] -= 0.5f * (pyp - pym) * sz;
      }
    setBnd_(1, u_); setBnd_(2, v_);
  }

  void vorticity_confinement_(float dt, float strength) {
    for (int y = 0; y < (int)h_; ++y)
      for (int xc = 0; xc < (int)w_; ++xc) {
        const float vxp = xc < (int)w_-1 ? v_[at_(xc+1,y)] : v_[at_(xc,y)];
        const float vxm = xc > 0          ? v_[at_(xc-1,y)] : v_[at_(xc,y)];
        const float uyp = y < (int)h_-1   ? u_[at_(xc,y+1)] : u_[at_(xc,y)];
        const float uym = y > 0            ? u_[at_(xc,y-1)] : u_[at_(xc,y)];
        div_[at_(xc,y)] = 0.5f * (vxp - vxm - (uyp - uym));
      }
    for (int y = 0; y < (int)h_; ++y)
      for (int xc = 0; xc < (int)w_; ++xc) {
        float gx = 0.5f * (std::fabs(sampleClamped_(div_, y,   xc+1)) - std::fabs(sampleClamped_(div_, y,   xc-1)));
        float gy = 0.5f * (std::fabs(sampleClamped_(div_, y+1, xc))   - std::fabs(sampleClamped_(div_, y-1, xc)));
        const float len = std::sqrt(gx*gx + gy*gy) + 1e-5f;
        gx /= len; gy /= len;
        const float curl = div_[at_(xc,y)];
        u_[at_(xc,y)] += dt * strength *  gy * curl;
        v_[at_(xc,y)] += dt * strength * -gx * curl;
      }
    setBnd_(1, u_); setBnd_(2, v_);
  }

  void inject_(float dt) {
    const float cx = (float)w_ * 0.5f;
    const float cy = (float)h_ * 0.5f;
    const float r  = (float)(w_ < h_ ? w_ : h_) * 0.3f;
    const float ex = cx + r * std::cos(phase_);
    const float ey = cy + r * std::sin(phase_ * 0.7f);
    const float str = dt * 30.0f;
    const float du  = std::sin(phase_) * str;
    const float dv  = -std::cos(phase_) * str;
    const float hue = std::fmod(phase_ * 0.3f, 6.28318f);
    const float dr  = str * (0.5f + 0.5f * std::sin(hue));
    const float dg  = str * (0.5f + 0.5f * std::sin(hue + 2.09440f));
    const float db  = str * (0.5f + 0.5f * std::sin(hue + 4.18879f));
    const int rad = (w_ > 8) ? 2 : 1;
    for (int dy = -rad; dy <= rad; ++dy)
      for (int dx = -rad; dx <= rad; ++dx) {
        const int ix = (int)(ex + (float)dx);
        const int iy = (int)(ey + (float)dy);
        if (ix < 0 || ix >= (int)w_ || iy < 0 || iy >= (int)h_) continue;
        const float wt = 1.0f / (1.0f + (float)(dx*dx + dy*dy));
        u_[at_(ix,iy)] += du * wt; v_[at_(ix,iy)] += dv * wt;
        gr_[at_(ix,iy)] += dr * wt; gg_[at_(ix,iy)] += dg * wt; gb_[at_(ix,iy)] += db * wt;
      }
  }

  static uint8_t to8_(float f) {
    if (f <= 0.0f) return 0;
    if (f >= 1.0f) return 255;
    return (uint8_t)(f * 255.0f + 0.5f);
  }

  static float* palloc_(uint32_t n) {
    auto* p = (float*)pal::psram_alloc(n * sizeof(float));
    if (p) std::memset(p, 0, n * sizeof(float));
    return p;
  }

  void free_fields_() {
    float** ptrs[] = {&u_, &v_, &u_prev_, &v_prev_, &press_, &div_,
                      &gr_, &gg_, &gb_, &tr_, &tg_, &tb_};
    for (auto** pp : ptrs) { pal::psram_free(*pp); *pp = nullptr; }
    w_ = h_ = 0; n_ = 0;
  }
};

}  // namespace pmm
