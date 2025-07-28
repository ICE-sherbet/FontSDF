#include <algorithm>
#include <cassert>
#include <cstdint>
#include <span>
#include <vector>

namespace ttf {

constexpr uint16_t ReadU16(const uint8_t* p) noexcept {
  return (uint16_t(p[0]) << 8) | p[1];
}
constexpr int16_t ReadS16(const uint8_t* p) noexcept {
  return int16_t(ReadU16(p));
}
constexpr uint32_t ReadU32(const uint8_t* p) noexcept {
  return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) |
         (uint32_t(p[2]) << 8) | p[3];
}

struct GlyphContour {
  struct Segment {
    float x0, y0, cx, cy, x1, y1;
  };
  std::vector<Segment> segments;
  std::vector<size_t> contours;
  int16_t advance_width = 0;
};

class FontLoader {
 public:
  explicit FontLoader(std::span<const uint8_t> blob)
      : data_(blob.data()), size_(blob.size()) {
    assert(size_ > 12);
    ParseDirectory();
    ParseEssentialTables();
    BuildCmapIndex();
    BuildHMetrics();
  }

  uint16_t GlyphId(char32_t code_point) const {
    if (code_point > 0x10FFFF) return 0;
    if (!cmap12_.empty()) {
      uint32_t lo = 0, hi = uint32_t(cmap12_.size());
      while (lo < hi) {
        uint32_t mid = (lo + hi) / 2;
        const auto& g = cmap12_[mid];
        if (code_point < g.start_char_code)
          hi = mid;
        else if (code_point > g.end_char_code)
          lo = mid + 1;
        else
          return uint16_t(g.start_glyph_id + (code_point - g.start_char_code));
      }
    }
    if (!cmap4_.empty() && code_point <= 0xFFFF) {
      uint16_t ch = uint16_t(code_point);
      const auto it = std::lower_bound(
          cmap4_.begin(), cmap4_.end(), ch,
          [](const Cmap4Seg& seg, uint16_t c) { return seg.end_code < c; });
      if (it != cmap4_.end() && it->start_code <= ch && ch <= it->end_code) {
        if (it->id_range_offset == 0)
          return uint16_t((ch + it->id_delta) & 0xFFFF);
        uint32_t idx = uint32_t(it - cmap4_.begin());
        uint32_t ofs = (it->id_range_offset / 2) + (ch - it->start_code) + idx -
                       uint32_t(cmap4_.size());
        if (ofs < glyph_id_array4_.size()) {
          uint16_t gid = glyph_id_array4_[ofs];
          if (gid != 0) gid = uint16_t((gid + it->id_delta) & 0xFFFF);
          return gid;
        }
      }
    }
    return 0;
  }

  float UnitsPerEm() const noexcept {
    return static_cast<float>(units_per_em_);
  }
  uint16_t GlyphCount() const noexcept { return num_glyphs_; }

  GlyphContour Extract(uint16_t glyph_id, float flatness = 1.0f) const {
    GlyphReader gr(*this, glyph_id, flatness);
    return gr.Run();
  }

 private:
  const uint8_t* data_ = nullptr;
  size_t size_ = 0;

  struct DirEntry {
    uint32_t tag, checksum, offset, length;
  };
  std::vector<DirEntry> directory_;

  static constexpr uint32_t Tag4(char a, char b, char c, char d) {
    return (uint32_t(a) << 24) | (uint32_t(b) << 16) | (uint32_t(c) << 8) |
           uint32_t(d);
  }

  const uint8_t* TablePtr(uint32_t tag) const {
    auto it = std::find_if(directory_.begin(), directory_.end(),
                           [tag](const DirEntry& e) { return e.tag == tag; });
    return (it == directory_.end()) ? nullptr : (data_ + it->offset);
  }

  void ParseDirectory() {
    const uint8_t* p = data_;
    uint16_t num_tables = ReadU16(p + 4);
    directory_.reserve(num_tables);
    p += 12;
    for (uint16_t i = 0; i < num_tables; ++i, p += 16) {
      DirEntry e{ReadU32(p), ReadU32(p + 4), ReadU32(p + 8), ReadU32(p + 12)};
      if (e.offset + e.length <= size_) directory_.push_back(e);
    }
  }

  uint16_t units_per_em_ = 0;
  uint16_t num_glyphs_ = 0;
  uint16_t index_to_loc_format_ = 0;
  uint16_t num_long_hor_metrics_ = 0;

  const uint8_t* loca_ = nullptr;
  const uint8_t* glyf_ = nullptr;
  const uint8_t* hmtx_ = nullptr;

  void ParseEssentialTables() {
    if (auto head = TablePtr(Tag4('h', 'e', 'a', 'd')); head) {
      units_per_em_ = ReadU16(head + 18);
      index_to_loc_format_ = ReadU16(head + 50);
    }
    if (auto maxp = TablePtr(Tag4('m', 'a', 'x', 'p')); maxp)
      num_glyphs_ = ReadU16(maxp + 4);

    loca_ = TablePtr(Tag4('l', 'o', 'c', 'a'));
    glyf_ = TablePtr(Tag4('g', 'l', 'y', 'f'));

    if (auto hhea = TablePtr(Tag4('h', 'h', 'e', 'a')); hhea)
      num_long_hor_metrics_ = ReadU16(hhea + 34);
    hmtx_ = TablePtr(Tag4('h', 'm', 't', 'x'));
  }

  struct Cmap4Seg {
    uint16_t end_code, start_code, id_delta, id_range_offset;
  };
  struct Cmap12Group {
    uint32_t start_char_code, end_char_code, start_glyph_id;
  };

  std::vector<Cmap4Seg> cmap4_;
  std::vector<uint16_t> glyph_id_array4_;
  std::vector<Cmap12Group> cmap12_;

  void BuildCmapIndex() {
    const uint8_t* cmap = TablePtr(Tag4('c', 'm', 'a', 'p'));
    if (!cmap) return;
    uint16_t num_sub = ReadU16(cmap + 2);
    const uint8_t* sub = cmap + 4;
    const uint8_t* best4 = nullptr;
    const uint8_t* best12 = nullptr;
    for (uint16_t i = 0; i < num_sub; ++i, sub += 8) {
      uint16_t plat = ReadU16(sub);
      uint16_t enc = ReadU16(sub + 2);
      (void)enc;
      uint32_t offset = ReadU32(sub + 4);
      const uint8_t* ptr = cmap + offset;
      uint16_t fmt = ReadU16(ptr);
      if (fmt == 12) {
        if (!best12 || (plat == 3 && ReadU16(best12 - 4) != 3)) best12 = ptr;
      } else if (fmt == 4) {
        if (!best4 || (plat == 3 && ReadU16(best4 - 4) != 3)) best4 = ptr;
      }
    }
    if (best4) ParseCmap4(best4);
    if (best12) ParseCmap12(best12);
  }

  void ParseCmap4(const uint8_t* p) {
    uint16_t seg_count = ReadU16(p + 6) / 2;
    cmap4_.resize(seg_count);
    const uint8_t* end_codes = p + 14;
    const uint8_t* start_codes = end_codes + seg_count * 2 + 2;
    const uint8_t* id_deltas = start_codes + seg_count * 2;
    const uint8_t* id_r_offsets = id_deltas + seg_count * 2;
    uint16_t glyph_id_array_len = (ReadU16(p + 2) - (16 + seg_count * 8)) / 2;
    glyph_id_array4_.resize(glyph_id_array_len);
    const uint8_t* gid_array = id_r_offsets + seg_count * 2;

    for (uint16_t i = 0; i < seg_count; ++i) {
      cmap4_[i] = {ReadU16(end_codes + i * 2), ReadU16(start_codes + i * 2),
                   ReadU16(id_deltas + i * 2), ReadU16(id_r_offsets + i * 2)};
    }
    for (uint16_t i = 0; i < glyph_id_array_len; ++i)
      glyph_id_array4_[i] = ReadU16(gid_array + i * 2);
  }

  void ParseCmap12(const uint8_t* p) {
    uint32_t num_groups = ReadU32(p + 12);
    cmap12_.resize(num_groups);
    const uint8_t* g = p + 16;
    for (uint32_t i = 0; i < num_groups; ++i, g += 12)
      cmap12_[i] = {ReadU32(g), ReadU32(g + 4), ReadU32(g + 8)};
  }

  std::vector<uint16_t> advance_widths_;

  void BuildHMetrics() {
    if (!hmtx_ || !num_long_hor_metrics_) return;
    advance_widths_.resize(num_glyphs_);
    for (uint16_t i = 0; i < num_glyphs_; ++i) {
      uint16_t aw = (i < num_long_hor_metrics_)
                        ? ReadU16(hmtx_ + i * 4)
                        : advance_widths_[num_long_hor_metrics_ - 1];
      advance_widths_[i] = aw;
    }
  }

  uint16_t AdvanceWidth(uint16_t gid) const noexcept {
    return (gid < advance_widths_.size()) ? advance_widths_[gid] : 0;
  }

  class GlyphReader {
   public:
    GlyphReader(const FontLoader& f, uint16_t gid, float flat)
        : font_(f), glyph_id_(gid), flatness_(flat) {}
    GlyphContour Run() {
      GlyphContour out;
      out.advance_width = int16_t(font_.AdvanceWidth(glyph_id_));
      Visit(glyph_id_, 0, 0, out);
      return out;
    }

   private:
    const FontLoader& font_;
    uint16_t glyph_id_;
    float flatness_;

    void Visit(uint16_t gid, int32_t dx, int32_t dy, GlyphContour& out) {
      const uint8_t* gptr;
      uint32_t glen;
      if (!font_.GlyphOffset(gid, gptr, glen)) return;
      int16_t n_contours = ReadS16(gptr);
      if (n_contours >= 0)
        ParseSimple(gptr, glen, dx, dy, out);
      else
        ParseComposite(gptr, glen, dx, dy, out);
    }

    void ParseSimple(const uint8_t* g, uint32_t, int32_t dx, int32_t dy,
                     GlyphContour& out) {
      int16_t n_contours = ReadS16(g);
      const uint8_t* ptr = g + 10;
      std::vector<uint16_t> end_pts(n_contours);
      for (int i = 0; i < n_contours; ++i) end_pts[i] = ReadU16(ptr + i * 2);
      ptr += n_contours * 2;
      uint16_t instr_len = ReadU16(ptr);
      ptr += 2 + instr_len;
      uint16_t n_pts = end_pts.back() + 1;

      std::vector<uint8_t> flags;
      flags.reserve(n_pts);
      for (uint16_t i = 0; i < n_pts;) {
        uint8_t f = *ptr++;
        flags.push_back(f);
        ++i;
        if (f & 0x08) {
          uint8_t rep = *ptr++;
          for (int r = 0; r < rep; ++r) {
            flags.push_back(f);
            ++i;
          }
        }
      }

      std::vector<int16_t> xs(n_pts), ys(n_pts);
      for (uint16_t i = 0; i < n_pts; ++i) {
        if (flags[i] & 0x02) {
          uint8_t dx8 = *ptr++;
          xs[i] = (flags[i] & 0x10) ? dx8 : -int16_t(dx8);
        } else if (!(flags[i] & 0x10)) {
          xs[i] = ReadS16(ptr);
          ptr += 2;
        } else
          xs[i] = 0;
      }
      for (uint16_t i = 1; i < n_pts; ++i) xs[i] += xs[i - 1];
      for (uint16_t i = 0; i < n_pts; ++i) {
        if (flags[i] & 0x04) {
          uint8_t dy8 = *ptr++;
          ys[i] = (flags[i] & 0x20) ? dy8 : -int16_t(dy8);
        } else if (!(flags[i] & 0x20)) {
          ys[i] = ReadS16(ptr);
          ptr += 2;
        } else
          ys[i] = 0;
      }
      for (uint16_t i = 1; i < n_pts; ++i) ys[i] += ys[i - 1];

      uint16_t start = 0;
      for (int c = 0; c < n_contours; ++c) {
        out.contours.push_back(out.segments.size());
        uint16_t end = end_pts[c];
        EmitContour(xs, ys, flags, start, end, dx, dy, out);
        start = end + 1;
      }
    }

    void EmitContour(const std::vector<int16_t>& xs,
                     const std::vector<int16_t>& ys,
                     const std::vector<uint8_t>& flags, uint16_t first_idx,
                     uint16_t last_idx, int32_t dx, int32_t dy,
                     GlyphContour& out) {
      const uint16_t n = last_idx - first_idx + 1;
      if (!n) return;

      auto IsOn = [&](uint16_t i) -> bool { return flags[i] & 1u; };
      auto Pt = [&](uint16_t i) -> std::pair<float, float> {
        return {float(xs[i]) + dx, float(ys[i]) + dy};
      };
      auto Next = [&](uint16_t i) -> uint16_t {
        return (i == last_idx) ? first_idx : uint16_t(i + 1);
      };

      uint16_t start = first_idx;
      while (!IsOn(start) && start != Next(start)) start = Next(start);

      uint16_t i_prev = start;
      auto [prev_x, prev_y] = Pt(i_prev);
      bool prev_on = IsOn(i_prev);

      for (uint16_t i_cur = Next(start);; i_cur = Next(i_cur)) {
        bool cur_on;
        float cur_x, cur_y;
        std::tie(cur_x, cur_y) = Pt(i_cur);
        cur_on = IsOn(i_cur);

        if (prev_on && cur_on) {
          AddLine(prev_x, prev_y, cur_x, cur_y, out);
        } else if (prev_on && !cur_on) {
        } else if (!prev_on && !cur_on) {
          float mx = (prev_x + cur_x) * 0.5f;
          float my = (prev_y + cur_y) * 0.5f;
          AddQuad(prev_x, prev_y, prev_x, prev_y, mx, my, out);
          prev_x = mx;
          prev_y = my;
          prev_on = true;
          continue;
        } else {
          AddQuad(prev_x, prev_y, prev_x, prev_y, cur_x, cur_y, out);
        }

        if (i_cur == start) break;
        prev_x = cur_x;
        prev_y = cur_y;
        prev_on = cur_on;
      }
    }

    void AddLine(float x0, float y0, float x1, float y1, GlyphContour& out) {
      float cx = (x0 + x1) * 0.5f;
      float cy = (y0 + y1) * 0.5f;
      out.segments.push_back({x0, y0, cx, cy, x1, y1});
    }
    void AddQuad(float x0, float y0, float cx, float cy, float x1, float y1,
                 GlyphContour& out) {
      out.segments.push_back({x0, y0, cx, cy, x1, y1});
    }

    void ParseComposite(const uint8_t* g, uint32_t, int32_t dx, int32_t dy,
                        GlyphContour& out) {
      const uint8_t* ptr = g + 10;
      enum Flags {
        ARGS_ARE_WORDS = 1,
        ARGS_ARE_XY = 2,
        MORE_COMPONENTS = 0x20,
        WE_HAVE_SCALE = 8,
        WE_HAVE_XY_SCALE = 0x40,
        WE_HAVE_2X2 = 0x80
      };
      bool more;
      do {
        uint16_t flags = ReadU16(ptr);
        ptr += 2;
        uint16_t cid = ReadU16(ptr);
        ptr += 2;
        int32_t arg1, arg2;
        if (flags & ARGS_ARE_WORDS) {
          arg1 = ReadS16(ptr);
          arg2 = ReadS16(ptr + 2);
          ptr += 4;
        } else {
          arg1 = int8_t(*ptr);
          arg2 = int8_t(*(ptr + 1));
          ptr += 2;
        }

        int32_t cdx = dx, cdy = dy;
        if (flags & ARGS_ARE_XY) {
          cdx += arg1;
          cdy += arg2;
        }
        Visit(cid, cdx, cdy, out);

        if (flags & WE_HAVE_SCALE)
          ptr += 2;
        else if (flags & WE_HAVE_XY_SCALE)
          ptr += 4;
        else if (flags & WE_HAVE_2X2)
          ptr += 8;
        more = flags & MORE_COMPONENTS;
      } while (more);
    }
  };

  bool GlyphOffset(uint16_t gid, const uint8_t*& ptr, uint32_t& len) const {
    if (!glyf_ || !loca_ || gid >= num_glyphs_) return false;
    uint32_t offset, next;
    if (index_to_loc_format_ == 0) {
      offset = ReadU16(loca_ + gid * 2) * 2u;
      next = ReadU16(loca_ + (gid + 1) * 2) * 2u;
    } else {
      offset = ReadU32(loca_ + gid * 4);
      next = ReadU32(loca_ + (gid + 1) * 4);
    }
    if (offset == next) return false;
    ptr = glyf_ + offset;
    len = next - offset;
    return true;
  }
};

}  // namespace ttf
