#pragma once

#include "engine/core/math.h"
#include "engine/core/result.h"

#include <cstdint>
#include <span>
#include <vector>

namespace engine::vt {

// C06 / Mega-W9 Virtual Texture: CPU page table + physical cache + request list +
// GPU feedback stub (CPU-simulated feedback buffer). Feature name: "virtual_texture".
// Not Nanite; Sample remains a CPU stub (not wired as default full-material path).
// Mega-W10: Feature "vt_near_default" + EffectTuning::enable_vt_near_default = Sandbox
// "near default" switch (opt-in lit VT preference; still not full-material Nanite).

struct PageCoord {
  std::uint32_t x = 0;
  std::uint32_t y = 0;
  std::uint32_t mip = 0;

  bool operator==(const PageCoord& o) const {
    return x == o.x && y == o.y && mip == o.mip;
  }
};

// GPU feedback entry (CPU-simulated): page id + optional mip hint / importance.
struct VtFeedbackRequest {
  PageCoord page{};
  float importance = 1.f;
};

struct PageTableEntry {
  std::uint32_t physical_slot = 0;
  bool resident = false;
};

struct PhysicalPage {
  PageCoord coord{};
  bool occupied = false;
  // Stub texel (RGBA) for Sample when resident.
  ColorRgba color{0.5f, 0.5f, 0.5f, 1.f};
};

class VirtualTexture {
 public:
  // virtual_pages_xy: pages along U/V at mip0. physical_slots: cache capacity.
  void Configure(std::uint32_t virtual_pages_xy, std::uint32_t physical_slots,
                 std::uint32_t mip_count = 1);

  [[nodiscard]] std::uint32_t virtual_pages_xy() const { return virtual_xy_; }
  [[nodiscard]] std::uint32_t physical_slots() const {
    return static_cast<std::uint32_t>(cache_.size());
  }
  [[nodiscard]] std::uint32_t mip_count() const { return mip_count_; }
  [[nodiscard]] std::uint32_t resident_count() const;
  [[nodiscard]] const std::vector<PageCoord>& pending_requests() const { return requests_; }

  // Map UV [0,1] + mip → page coord (clamped).
  [[nodiscard]] PageCoord UvToPage(float u, float v, std::uint32_t mip) const;

  [[nodiscard]] bool IsResident(PageCoord page) const;
  [[nodiscard]] const PageTableEntry* Lookup(PageCoord page) const;

  // Queue a residency request if not resident. Dedupes pending list.
  void RequestPage(PageCoord page);

  // Mega-W9: ingest CPU-simulated GPU feedback buffer into the request queue.
  void ProcessGpuFeedback(std::span<const VtFeedbackRequest> feedback);

  // Satisfy oldest pending requests into free / LRU physical slots.
  // Returns number of pages made resident.
  std::uint32_t ProcessRequests(std::uint32_t max_uploads);

  // Page upload helper: ProcessRequests then return how many pages are now resident.
  // Marks residency via the same physical-cache path as ProcessRequests.
  std::uint32_t UploadPendingPages(std::uint32_t max_uploads);

  // Evict one resident page (LRU-ish: first occupied). Returns true if evicted.
  bool EvictOne();

  // Sample stub: resident → page color; else → transparent black + auto RequestPage.
  [[nodiscard]] ColorRgba Sample(float u, float v, std::uint32_t mip = 0);

  // W13: one product tick — ingest feedback, upload up to budget pages.
  std::uint32_t TickNearField(std::span<const VtFeedbackRequest> feedback,
                              std::uint32_t max_uploads);

  void ClearRequests() { requests_.clear(); }

 private:
  [[nodiscard]] std::size_t TableIndex(PageCoord page) const;
  [[nodiscard]] std::uint32_t PagesAtMip(std::uint32_t mip) const;

  std::uint32_t virtual_xy_ = 0;
  std::uint32_t mip_count_ = 1;
  std::vector<PageTableEntry> table_;
  std::vector<PhysicalPage> cache_;
  std::vector<PageCoord> requests_;
  std::uint32_t clock_ = 0;
};

}  // namespace engine::vt
