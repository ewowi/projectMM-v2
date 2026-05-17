#pragma once
//
// PixelEffectBase — shared spine for pixel-producing effects.
//
// Every effect repeated the same ~70 lines: a `layout` text input, parent-or-id
// layout resolution, a psram pixel buffer + owned DataBuffer<RGB> declared in
// DataRegistry, the ADR 0005 invalidate-on-teardown, the loop1s resize poll,
// and pixelBuffer(). That boilerplate lives here once; a concrete effect adds
// only its controls and its per-frame render.
//
// A subclass implements:
//   const char* category() — "effect"
//   void build_effect_controls()         — its own addControl() calls
//   void render_(RGB* px, uint16_t w, uint16_t h, uint16_t d)  — fill px
// and may override:
//   bool is3d() const                    — false (2D, d forced to 1) by default
//   void on_geometry_(w,h,d)             — (re)size extra per-pixel tables
//   void on_control_(const char* key)    — react to a non-"layout" control
//
// Layout resolution (fixes the pre-port bug): a layout is identified by its
// virtual category()=="layout" — i.e. it is a LayoutModule — NOT by the
// registered factory type string. GridLayoutModule, RingLayoutModule and
// WheelLayoutModule all derive from LayoutModule and register under distinct
// factory types. Matching on the registered type ("layout") only ever worked
// by coincidence (Grid happened to be registered as "layout"); that is the v1
// strcmp-on-type-name pattern CLAUDE.md warns against.
//

#include <cstring>

#include "../../../core/DataBuffer.h"
#include "../../../core/DataRegistry.h"
#include "../../../core/MoonModule.h"
#include "../../../core/ModuleManager.h"
#include "../../../pal/Pal.h"
#include "../../../pal/PalHeap.h"
#include "../../system/Logger.h"
#include "../Pixelable.h"
#include "../RGB.h"
#include "../layouts/LayoutModule.h"

namespace pmm {

class PixelEffectBase : public MoonModule, public PixelSource {
 public:
  void setup() override { resolve_layout_(); }

  void onBuildControls() override {
    clearControls();  // safe no-op on first call; enables control rebuild
    addControl(layout_id_, sizeof(layout_id_), "layout", "text");
    build_effect_controls();
  }

  // Rebuild the schema (e.g. a "type" control that shows a different set).
  // A subclass calls this from on_control_() when its control set changes.
  void rebuild_controls_() { onBuildControls(); }

  void onAllocateMemory() override { resolve_layout_(); allocate_(); }

  void onUpdate(const char* key) override {
    if (std::strcmp(key, "layout") == 0) { resolve_layout_(); allocate_(); }
    else on_control_(key);
  }

  void loop1s() override {
    if (!layout_) resolve_layout_();
    if (layout_) {
      const uint16_t d = is3d() ? layout_->depth() : 1;
      if (layout_->width() != w_ || layout_->height() != h_ || d != d_) allocate_();
    }
  }

  void loop20ms() override {
    if (!layout_) resolve_layout_();
    if (!pixels_) return;

    render_(pixels_, w_, h_, d_);

    if (buf_ && buf_->valid()) {
      RGB* dst = buf_->acquire_write();
      std::memcpy(dst, pixels_, (uint32_t)w_ * h_ * d_ * sizeof(RGB));
      buf_->publish();
      revision_++;
    }
  }

  void teardown() override {
    if (buf_) {
      DataRegistry::instance().undeclare(buf_);
      buf_->invalidate();  // ADR 0005: signal dead before free so cached readers skip
      delete buf_;
      buf_ = nullptr;
    }
    free_pixels_();
    layout_ = nullptr;
  }

  PixelBufferRef pixelBuffer() const override {
    return { pixels_, w_, h_, d_, revision_ };
  }

 protected:
  // -- Subclass contract -----------------------------------------------------
  virtual void build_effect_controls() = 0;
  virtual void render_(RGB* px, uint16_t w, uint16_t h, uint16_t d) = 0;
  virtual bool is3d() const { return false; }
  virtual void on_geometry_(uint16_t /*w*/, uint16_t /*h*/, uint16_t /*d*/) {}
  virtual void on_control_(const char* /*key*/) {}

  uint16_t w_ = 16, h_ = 16, d_ = 1;
  RGB*     pixels_ = nullptr;

 private:
  char             layout_id_[24] = "layout-0";
  LayoutModule*    layout_   = nullptr;
  DataBuffer<RGB>* buf_      = nullptr;
  uint32_t         revision_ = 0;

  void resolve_layout_() {
    layout_ = nullptr;
    if (!manager_) return;
    // A layout is a LayoutModule (category()=="layout", virtual). Parent
    // chain wins, then the `layout` input id. static_cast is safe: only
    // LayoutModule answers "layout" to the virtual category().
    for (MoonModule* p = parent(); p; p = p->parent()) {
      if (std::strcmp(p->category(), "layout") == 0) {
        layout_ = static_cast<LayoutModule*>(p);
        return;
      }
    }
    MoonModule* m = manager_->find(layout_id_);
    if (m && std::strcmp(m->category(), "layout") == 0)
      layout_ = static_cast<LayoutModule*>(m);
  }

  void free_pixels_() {
    if (pixels_) { pal::psram_free(pixels_); pixels_ = nullptr; }
    moduleAllocBytes_ = 0;
  }

  void allocate_() {
    const uint16_t pw = w_, ph = h_, pd = d_;
    if (layout_) {
      w_ = layout_->width();
      h_ = layout_->height();
      d_ = is3d() ? layout_->depth() : 1;
    } else {
      w_ = 16; h_ = 16; d_ = 1;
    }

    // Geometry unchanged and buffers live: nothing to reallocate. Skips a
    // psram churn and (via on_geometry_) any per-pixel table rebuild on a
    // no-op layout re-set or an identical-geometry state reload. Cold path,
    // but Rule #1 is also "minimal in CPU cycles". Matches the original
    // RipplesEffect allocation-skip guard the port folded in here.
    if (w_ == pw && h_ == ph && d_ == pd && pixels_ && buf_) return;

    const uint32_t n = (uint32_t)w_ * h_ * d_;
    free_pixels_();
    pixels_ = (RGB*)pal::psram_alloc(n * sizeof(RGB));
    if (!pixels_) return;
    std::memset(pixels_, 0, n * sizeof(RGB));
    moduleAllocBytes_ = n * sizeof(RGB);
    on_geometry_(w_, h_, d_);

    if (!buf_) buf_ = new DataBuffer<RGB>();
    if (buf_->allocate(n))
      DataRegistry::instance().declare(id(), buf_, n, sizeof(RGB), w_, h_, d_);
    ++revision_;
  }
};

}  // namespace pmm
