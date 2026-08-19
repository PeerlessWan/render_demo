#include "engine/vt/virtual_texture.h"

#include <algorithm>

namespace engine::vt {

void VirtualTexture::Configure(std::uint32_t virtual_pages_xy, std::uint32_t physical_slots,
                               std::uint32_t mip_count) {
  virtual_xy_ = std::max(virtual_pages_xy, 1u);
  mip_count_ = std::max(mip_count, 1u);
  std::size_t total = 0;
  for (std::uint32_t m = 0; m < mip_count_; ++m) {
    const std::uint32_t n = PagesAtMip(m);
    total += static_cast<std::size_t>(n) * static_cast<std::size_t>(n);
  }
  table_.assign(total, {});
  cache_.assign(std::max(physical_slots, 1u), {});
  requests_.clear();
  clock_ = 0;
}

std::uint32_t VirtualTexture::PagesAtMip(std::uint32_t mip) const {
  const std::uint32_t shift = std::min(mip, 31u);
  const std::uint32_t n = virtual_xy_ >> shift;
  return std::max(n, 1u);
}

std::size_t VirtualTexture::TableIndex(PageCoord page) const {
  std::size_t base = 0;
  for (std::uint32_t m = 0; m < page.mip && m < mip_count_; ++m) {
    const std::uint32_t n = PagesAtMip(m);
    base += static_cast<std::size_t>(n) * static_cast<std::size_t>(n);
  }
  const std::uint32_t n = PagesAtMip(page.mip);
  const std::uint32_t x = std::min(page.x, n - 1);
  const std::uint32_t y = std::min(page.y, n - 1);
  return base + static_cast<std::size_t>(y) * n + x;
}

std::uint32_t VirtualTexture::resident_count() const {
  std::uint32_t n = 0;
  for (const auto& e : table_) {
    if (e.resident) {
      ++n;
    }
  }
  return n;
}

PageCoord VirtualTexture::UvToPage(float u, float v, std::uint32_t mip) const {
  mip = std::min(mip, mip_count_ - 1);
  const std::uint32_t n = PagesAtMip(mip);
  const float uu = std::clamp(u, 0.f, 0.999999f);
  const float vv = std::clamp(v, 0.f, 0.999999f);
  return PageCoord{static_cast<std::uint32_t>(uu * static_cast<float>(n)),
                   static_cast<std::uint32_t>(vv * static_cast<float>(n)), mip};
}

bool VirtualTexture::IsResident(PageCoord page) const {
  if (page.mip >= mip_count_ || table_.empty()) {
    return false;
  }
  return table_[TableIndex(page)].resident;
}

const PageTableEntry* VirtualTexture::Lookup(PageCoord page) const {
  if (page.mip >= mip_count_ || table_.empty()) {
    return nullptr;
  }
  return &table_[TableIndex(page)];
}

void VirtualTexture::RequestPage(PageCoord page) {
  if (page.mip >= mip_count_) {
    return;
  }
  page.x = std::min(page.x, PagesAtMip(page.mip) - 1);
  page.y = std::min(page.y, PagesAtMip(page.mip) - 1);
  if (IsResident(page)) {
    return;
  }
  for (const auto& r : requests_) {
    if (r == page) {
      return;
    }
  }
  requests_.push_back(page);
}

void VirtualTexture::ProcessGpuFeedback(std::span<const VtFeedbackRequest> feedback) {
  // Higher importance first so budgeted uploads prefer hot pages.
  std::vector<VtFeedbackRequest> ordered(feedback.begin(), feedback.end());
  std::sort(ordered.begin(), ordered.end(),
            [](const VtFeedbackRequest& a, const VtFeedbackRequest& b) {
              return a.importance > b.importance;
            });
  for (const auto& req : ordered) {
    RequestPage(req.page);
  }
}

bool VirtualTexture::EvictOne() {
  for (std::uint32_t slot = 0; slot < cache_.size(); ++slot) {
    auto& phys = cache_[slot];
    if (!phys.occupied) {
      continue;
    }
    auto& entry = table_[TableIndex(phys.coord)];
    entry.resident = false;
    entry.physical_slot = 0;
    phys = {};
    return true;
  }
  return false;
}

std::uint32_t VirtualTexture::ProcessRequests(std::uint32_t max_uploads) {
  std::uint32_t done = 0;
  while (done < max_uploads && !requests_.empty()) {
    const PageCoord page = requests_.front();
    requests_.erase(requests_.begin());
    if (IsResident(page)) {
      continue;
    }

    std::int32_t free_slot = -1;
    for (std::uint32_t i = 0; i < cache_.size(); ++i) {
      if (!cache_[i].occupied) {
        free_slot = static_cast<std::int32_t>(i);
        break;
      }
    }
    if (free_slot < 0) {
      if (!EvictOne()) {
        requests_.insert(requests_.begin(), page);
        break;
      }
      for (std::uint32_t i = 0; i < cache_.size(); ++i) {
        if (!cache_[i].occupied) {
          free_slot = static_cast<std::int32_t>(i);
          break;
        }
      }
    }
    if (free_slot < 0) {
      break;
    }

    const auto slot = static_cast<std::uint32_t>(free_slot);
    auto& phys = cache_[slot];
    phys.occupied = true;
    phys.coord = page;
    // Deterministic stub color from page coords (for Sample tests).
    phys.color = ColorRgba{(page.x + 1) / 16.f, (page.y + 1) / 16.f, (page.mip + 1) / 8.f, 1.f};

    auto& entry = table_[TableIndex(page)];
    entry.resident = true;
    entry.physical_slot = slot;
    ++done;
    ++clock_;
  }
  return done;
}

std::uint32_t VirtualTexture::UploadPendingPages(std::uint32_t max_uploads) {
  return ProcessRequests(max_uploads);
}

ColorRgba VirtualTexture::Sample(float u, float v, std::uint32_t mip) {
  const PageCoord page = UvToPage(u, v, mip);
  if (!IsResident(page)) {
    RequestPage(page);
    return ColorRgba{0.f, 0.f, 0.f, 0.f};
  }
  const auto* e = Lookup(page);
  if (!e || e->physical_slot >= cache_.size()) {
    return ColorRgba{0.f, 0.f, 0.f, 0.f};
  }
  return cache_[e->physical_slot].color;
}

std::uint32_t VirtualTexture::TickNearField(std::span<const VtFeedbackRequest> feedback,
                                           std::uint32_t max_uploads) {
  ProcessGpuFeedback(feedback);
  return UploadPendingPages(max_uploads);
}

}  // namespace engine::vt
