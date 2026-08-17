#pragma once

#include "engine/core/math.h"
#include "engine/core/result.h"

#include <cstdint>
#include <vector>

namespace engine::vt {

// C06 minimal Virtual Texture: CPU-side page table + physical cache + request list.
// No GPU feedback / tiled resources this wave — residency + Sample stub only.

struct PageCoord {
  std::uint32_t x = 0;
  std::uint32_t y = 0;
  std::uint32_t mip = 0;

  bool operator==(const PageCoord& o) const {
    return x == o.x && y == o.y && mip == o.mip;
  }
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

  // Satisfy oldest pending requests into free / LRU physical slots.
  // Returns number of pages made resident.
  std::uint32_t ProcessRequests(std::uint32_t max_uploads);

  // Evict one resident page (LRU-ish: first occupied). Returns true if evicted.
  bool EvictOne();

  // Sample stub: resident → page color; else → transparent black + auto RequestPage.
  [[nodiscard]] ColorRgba Sample(float u, float v, std::uint32_t mip = 0);

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
