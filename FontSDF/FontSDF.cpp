#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <fstream>
#include <iostream>
#include <span>
#include <string>
#include <thread>
#include <vector>

#include "FontLoader.h"
#include "include/Serializer/SerializeDemo.h"

using ttf::GlyphContour;
#pragma pack(push, 1)
struct FontAssetHeader {
  char magic[8];           
  uint16_t major, minor;   
  uint16_t flags;          
  uint16_t pixelSizePX;    
  uint16_t borderPX;       
  uint16_t spreadPX;       
  int16_t fontHeightPX;    
  int16_t ascenderPX;      
  int16_t descenderPX;     
  uint16_t lineAdvancePX;  
  uint16_t texW, texH;     
  uint16_t reserved;       
  uint32_t glyphCount;
};

struct GlyphRecord {
  // UTF-32
  uint32_t codePoint;           
  uint16_t u, v, w, h;         
  int16_t bearingX, bearingY;  
  uint16_t advance;            
  uint8_t atlasId;             
  uint8_t flags;               
};
#pragma pack(pop)
static constexpr int kSupersample = 64;
static constexpr int kRadiusPX = 5;
static constexpr int kBorderPX = 4;
static constexpr int kGlyphPX = 16;
static constexpr int kAtlasW = 1024;

struct GlyphMeta {
  char32_t cp;
  uint16_t u;
  uint16_t v;
};

struct BitPlane {
  int w{}, h{}, pitch{};
  std::vector<uint8_t> data;
  BitPlane(int width, int height) : w(width), h(height) {
    pitch = (w + 7) >> 3;
    data.resize(pitch * h, 0);
  }
  void Set(int x, int y) { data[y * pitch + (x >> 3)] |= 0x80 >> (x & 7); }
  bool Get(int x, int y) const {
    if (x < 0 || y < 0 || x >= w || y >= h) return false;
    return data[y * pitch + (x >> 3)] & (0x80 >> (x & 7));
  }
};
void WriteFontAsset(const std::string& root,
                    const std::vector<GlyphMeta>& metas,
                    const std::vector<uint8_t>& atlas, uint16_t texW,
                    uint16_t texH, int16_t fontHeightPX, int16_t ascPX,
                    int16_t descPX, uint16_t lineAdvancePX) {

  char path[260];
  sprintf_s(path, "%s.sdfb", root.c_str());
  std::ofstream ofs(path, std::ios::binary);
  if (!ofs) throw std::runtime_error("open sdfb fail");

  FontAssetHeader hd{};
  memcpy(hd.magic, "SDFONT1", 7);
  hd.major = 1;
  hd.minor = 0;
  hd.flags = 0;
  hd.pixelSizePX = kGlyphPX;
  hd.borderPX = kBorderPX;
  hd.spreadPX = kRadiusPX;
  hd.fontHeightPX = fontHeightPX;
  hd.ascenderPX = ascPX;
  hd.descenderPX = descPX;
  hd.lineAdvancePX = lineAdvancePX;
  hd.texW = texW;
  hd.texH = texH;
  hd.glyphCount = static_cast<uint32_t>(metas.size());
  ofs.write((char*)&hd, sizeof(hd));

  GlyphRecord gr{};
  gr.w = gr.h = kGlyphPX;
  gr.advance = kGlyphPX;
  for (auto& m : metas) {
    gr.codePoint = static_cast<uint32_t>(m.cp);
    gr.u = m.u;
    gr.v = m.v;
    gr.bearingX = 0;
    gr.bearingY = 0;
    ofs.write((char*)&gr, sizeof(gr));
  }

  ofs.write((char*)atlas.data(), atlas.size());
  ofs.close();
}
static void FlattenQuadR(float x0, float y0, float cx, float cy, float x1,
                         float y1, float tol2,
                         std::vector<std::pair<float, float>>& out) {
  float mx = (x0 + 2 * cx + x1) * 0.25f;
  float my = (y0 + 2 * cy + y1) * 0.25f;
  float lx = (x0 + x1) * 0.5f;
  float ly = (y0 + y1) * 0.5f;
  float dx = mx - lx;
  float dy = my - ly;
  if (dx * dx + dy * dy <= tol2) {
    out.emplace_back(x1, y1);
    return;
  }
  float q0x = (x0 + cx) * 0.5f;
  float q0y = (y0 + cy) * 0.5f;
  float q1x = (cx + x1) * 0.5f;
  float q1y = (cy + y1) * 0.5f;
  float qmx = (q0x + q1x) * 0.5f;
  float qmy = (q0y + q1y) * 0.5f;
  FlattenQuadR(x0, y0, q0x, q0y, qmx, qmy, tol2, out);
  FlattenQuadR(qmx, qmy, q1x, q1y, x1, y1, tol2, out);
}

static void RasterOutline(const GlyphContour& g, BitPlane& bmp) {
  if (g.segments.empty()) return;
  float min_x = FLT_MAX, min_y = FLT_MAX, max_x = -FLT_MAX, max_y = -FLT_MAX;
  for (auto& s : g.segments) {
    min_x = std::min({min_x, s.x0, s.cx, s.x1});
    min_y = std::min({min_y, s.y0, s.cy, s.y1});
    max_x = std::max({max_x, s.x0, s.cx, s.x1});
    max_y = std::max({max_y, s.y0, s.cy, s.y1});
  }
  const float drawable = kGlyphPX * kSupersample;
  float scale = drawable / std::max(max_x - min_x, max_y - min_y);
  float off_x = kBorderPX * kSupersample - min_x * scale;
  float off_y = kBorderPX * kSupersample - min_y * scale;

  const float tol2 = 1.0f / (512.0f * 512.0f);
  std::vector<std::pair<float, float>> poly;
  std::vector<float> x_int;
  for (int sy = 0; sy < bmp.h; ++sy) {
    float py_unit = (bmp.h - 1 - sy + 0.5f - off_y) / scale;
    x_int.clear();
    for (size_t c = 0; c < g.contours.size(); ++c) {
      size_t b = g.contours[c];
      size_t e =
          (c + 1 == g.contours.size()) ? g.segments.size() : g.contours[c + 1];
      poly.clear();
      for (size_t i = b; i < e; ++i) {
        const auto& s = g.segments[i];
        poly.emplace_back(s.x0, s.y0);
        if (s.cx == (s.x0 + s.x1) * 0.5f && s.cy == (s.y0 + s.y1) * 0.5f)
          poly.emplace_back(s.x1, s.y1);
        else
          FlattenQuadR(s.x0, s.y0, s.cx, s.cy, s.x1, s.y1, tol2, poly);
      }
      if (poly.size() < 2) continue;
      for (size_t i = 0, N = poly.size(); i < N; ++i) {
        auto [x0, y0] = poly[i];
        auto [x1, y1] = poly[(i + 1) % N];
        if ((y0 > py_unit) != (y1 > py_unit)) {
          float t = (py_unit - y0) / (y1 - y0);
          x_int.push_back(x0 + t * (x1 - x0));
        }
      }
    }
    if (x_int.size() < 2) continue;
    std::sort(x_int.begin(), x_int.end());
    const float eps = 1e-5f;
    size_t w = 0;
    for (size_t i = 0; i < x_int.size(); ++i)
      if (w == 0 || std::fabs(x_int[i] - x_int[w - 1]) > eps)
        x_int[w++] = x_int[i];
    x_int.resize(w);
    for (size_t k = 0; k + 1 < x_int.size(); k += 2) {
      int sx0 = int(x_int[k] * scale + off_x);
      int sx1 = int(x_int[k + 1] * scale + off_x);
      sx0 = std::clamp(sx0, 0, bmp.w - 1);
      sx1 = std::clamp(sx1, 0, bmp.w - 1);
      for (int sx = sx0; sx <= sx1; ++sx) bmp.Set(sx, sy);
    }
  }
}

inline bool ReadCanvasBit(const BitPlane& bmp, int32_t xo, int32_t yo, int x,
                          int y) {
  x -= xo;
  y -= yo;
  if (x < 0 || y < 0 || x >= bmp.w || y >= bmp.h) return false;
  return bmp.Get(x, y);
}

static float DistanceSq(const BitPlane& bmp, int xo, int yo, int x, int y,
                        bool from_inside) {
  const int R = kRadiusPX * kSupersample;
  int cx = x * kSupersample + 7;
  int cy = y * kSupersample + 7;
  int best = R * R;
  for (int dy = -R; dy <= R; dy += 2) {
    int yy = cy + dy;
    int dyy = dy * dy;
    if (dyy >= best) break;
    for (int dx = -R; dx <= R; dx += 2) {
      int dxx = dx * dx;
      int dsq = dxx + dyy;
      if (dsq >= best) continue;
      bool pix = ReadCanvasBit(bmp, xo, yo, (cx + dx), (cy + dy));
      if (pix != from_inside) {
        best = dsq;
        if (best == 0) return 0;
      }
    }
  }
  return float(best) / (R * R);
}

struct Shared {
  std::atomic_uint next{0};
  std::vector<GlyphMeta>* metas;
  std::vector<uint8_t>* atlas;
  int atlas_pitch;
};

static void Worker(const ttf::FontLoader& font, std::vector<char32_t>& cps,
                   Shared& sh) {
  const float flatness = font.UnitsPerEm() / float(kGlyphPX * 16);

  const int hi_side = (kGlyphPX + 2 * kBorderPX) * kSupersample;
  const int lo_side = kGlyphPX + 2 * kBorderPX;
  const int R = kRadiusPX * kSupersample;
  const int R2 = R * R;

  for (;;) {
    size_t idx = sh.next.fetch_add(1, std::memory_order_relaxed);
    if (idx >= cps.size()) break;

    uint16_t gid = font.GlyphId(cps[idx]);
    if (!gid) continue;

    GlyphContour outline = font.Extract(gid, flatness);

    BitPlane hi(hi_side, hi_side);
    RasterOutline(outline, hi);

    std::vector<uint8_t> sdf(lo_side * lo_side);

    for (int y = 0; y < lo_side; ++y)
      for (int x = 0; x < lo_side; ++x) {
        const int step = kSupersample / 4;
        const int half = step >> 1;
        int in_cnt = 0;
        for (int sy = 0; sy < 4; ++sy)
          for (int sx = 0; sx < 4; ++sx) {
            int hx = x * kSupersample + sx * step + half;
            int hy = y * kSupersample + sy * step + half;
            in_cnt += hi.Get(hx, hy);
          }
        bool inside = in_cnt >= 8;

        int cx = x * kSupersample + kSupersample / 2;
        int cy = y * kSupersample + kSupersample / 2;
        int best = R2;
        for (int dy = -R; dy <= R; ++dy) {
          int yy = cy + dy;
          int dyy = dy * dy;
          if (dyy >= best) continue;
          for (int dx = -R; dx <= R; ++dx) {
            int dxx = dx * dx;
            int d2 = dxx + dyy;
            if (d2 >= best) continue;
            bool pix = hi.Get(cx + dx, yy);
            if (pix != inside) best = d2;
          }
        }
        float norm = std::sqrt(float(best)) / float(R);
        float signed_n = inside ? norm : -norm;
        uint8_t v =
            uint8_t(std::clamp(128.0f + signed_n * 127.0f, 0.0f, 255.0f));
        sdf[y * lo_side + x] = v;
      }

    const GlyphMeta& m = (*sh.metas)[idx];
    int dst_y = m.v - kBorderPX;
    int dst_x = m.u - kBorderPX;

    for (int y = 0; y < lo_side; ++y) {
      std::memcpy(&(*sh.atlas)[(dst_y + y) * sh.atlas_pitch + dst_x],
                  &sdf[y * lo_side], lo_side);
    }
  }
}

static void WriteBmp(const std::wstring& path, int w, int h,
                     const uint8_t* buf) {
  uint32_t row_bytes = w * 3;
  uint32_t padding = (4 - (row_bytes & 3)) & 3;
  uint32_t image_bytes = (row_bytes + padding) * h;

  BITMAPFILEHEADER bf = {};
  bf.bfType = 0x4D42;
  bf.bfOffBits = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);
  bf.bfSize = bf.bfOffBits + image_bytes;

  BITMAPINFOHEADER bi = {};
  bi.biSize = sizeof(BITMAPINFOHEADER);
  bi.biWidth = static_cast<int32_t>(w);
  bi.biHeight = -static_cast<int32_t>(h);
  bi.biPlanes = 1;
  bi.biBitCount = 24;
  bi.biCompression = BI_RGB;
  bi.biSizeImage = image_bytes;
  bi.biXPelsPerMeter = 0x0EC4;
  bi.biYPelsPerMeter = 0x0EC4;

  std::ofstream ofs(path, std::ios::binary);
  if (!ofs) throw std::runtime_error("Cannot open bmp for writing");

  ofs.write(reinterpret_cast<const char*>(&bf), sizeof(bf));
  ofs.write(reinterpret_cast<const char*>(&bi), sizeof(bi));

  std::vector<uint8_t> pad(padding, 0);
  std::vector<uint8_t> row(row_bytes);

  for (uint32_t y = 0; y < h; ++y) {
    const uint8_t* src = buf + y * w;
    uint8_t* dst = row.data();
    for (uint32_t x = 0; x < w; ++x, dst += 3)
      dst[0] = dst[1] = dst[2] = src[x];
    ofs.write(reinterpret_cast<const char*>(row.data()), row_bytes);
    if (padding) ofs.write(reinterpret_cast<const char*>(pad.data()), padding);
  }
  ofs.close();
}

int wmain(int argc, wchar_t** argv) {
  

  std::ifstream settings("FontSDFSettings.txt");
  if (!settings) {
		std::wcerr << L"Settings file not found.\n";
		return -1;
	}

  std::string font_path;
  std::string chars;
  settings >> font_path >> chars;

  std::ifstream in(font_path, std::ios::binary | std::ios::ate);
  if (!in) {
    std::wcerr << L"font open fail";
    return -1;
  }
  std::vector<uint8_t> buf(size_t(in.tellg()));
  in.seekg(0);
  in.read(reinterpret_cast<char*>(buf.data()), buf.size());
  ttf::FontLoader font(std::span<const uint8_t>(buf.data(), buf.size()));

  auto decode = [&](const std::string& s) {
    std::vector<char32_t> out;
    size_t i = 0;
    while (i < s.size()) {
      unsigned c = static_cast<unsigned char>(s[i]);
      if (c < 0x80)
      {
        out.push_back(c);
        ++i;
      } else if ((c & 0xE0) == 0xC0)
      {
        out.push_back(((c & 0x1F) << 6) | (s[i + 1] & 0x3F));
        i += 2;
      } else if ((c & 0xF0) == 0xE0)
      {
        out.push_back(((c & 0x0F) << 12) | ((s[i + 1] & 0x3F) << 6) |
                      (s[i + 2] & 0x3F));
        i += 3;
      } else
      {
        out.push_back(((c & 0x07) << 18) | ((s[i + 1] & 0x3F) << 12) |
                      ((s[i + 2] & 0x3F) << 6) | (s[i + 3] & 0x3F));
        i += 4;
      }
    }
    return out;
  };

  auto cps = decode(chars);

  std::vector<GlyphMeta> metas(cps.size());
  int cur_x = kBorderPX, cur_y = kBorderPX, row_h = 0, atlas_h = kBorderPX;
  for (size_t i = 0; i < cps.size(); ++i) {
    if (cur_x + kGlyphPX + 2 * kBorderPX > kAtlasW) {
      cur_x = kBorderPX;
      cur_y += row_h + kBorderPX;
      row_h = 0;
    }
    metas[i] = {cps[i], uint16_t(cur_x + kBorderPX),
                uint16_t(cur_y + kBorderPX)};
    cur_x += kGlyphPX + 2 * kBorderPX;
    row_h = kGlyphPX + 2 * kBorderPX;
    atlas_h = std::max(atlas_h, cur_y + row_h + kBorderPX);
  }

  std::vector<uint8_t> atlas(kAtlasW * atlas_h, 0);
  Shared sh{0, &metas, &atlas, kAtlasW};
  
  // 時間測定
  auto start = std::chrono::high_resolution_clock::now();


  std::vector<std::thread> pool;
  for (int t = 0; t < std::thread::hardware_concurrency(); ++t)
    pool.emplace_back(Worker, std::cref(font), std::ref(cps), std::ref(sh));
  for (auto& t : pool) t.join();

  WriteBmp(L"atlas_super.bmp", kAtlasW, atlas_h, atlas.data());
  std::wcout << L"Saved atlas_super.bmp (" << kAtlasW << L"x" << atlas_h
             << L")\n";
  
  auto end = std::chrono::high_resolution_clock::now();
  std::chrono::duration<double> elapsed = end - start;
  std::wcout << L"Elapsed time: " << elapsed.count() << L" seconds\n";

  const int16_t asc = int16_t(kGlyphPX);
  const int16_t desc = -int16_t(kBorderPX);
  const int16_t fH = asc - desc;           
  const uint16_t advY = uint16_t(kGlyphPX + kBorderPX);

  WriteFontAsset("atlas_super",
                 metas, atlas, uint16_t(kAtlasW), uint16_t(atlas_h), fH, asc,
                 desc, advY);

  std::wcout << L"Saved atlas_super.sdfb (" << metas.size() << L" glyphs)\n";
  return 0;
}
