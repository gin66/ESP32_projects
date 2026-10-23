/**
 * @file dump_data.cpp
 * @brief Load the device's SD/SPIFFS dump for the host harness.
 *
 * The binary layouts mirror src/haus_history.cpp and src/haus_sensor_roles.cpp.
 * Nothing here is compiled into the firmware.
 */

#include "dump_data.h"

#include <dirent.h>
#include <sys/stat.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>

namespace {

constexpr uint32_t kRingMagic = 0x484E4752u; /* "RNGH" */
constexpr uint16_t kRingVersion = 4;
constexpr uint16_t kRoleMagic = 0x52534E48u; /* "HNSR" */
constexpr uint16_t kRoleVersion = 3;
constexpr size_t kRingHeader = 8;
constexpr size_t kRoleHeader = 8;

typedef struct __attribute__((packed)) {
  uint32_t epoch;
  int16_t temp[3];
  int16_t living_c100;
  uint16_t hum_c100;
  uint16_t power_dw;
  uint32_t pressure_pa;
  uint8_t flags;
  uint8_t stale_mask;
  int16_t target_c100;
} SampleV24;

typedef struct __attribute__((packed)) {
  uint32_t magic;
  uint16_t version;
  uint8_t count;
  uint8_t reserved;
  struct __attribute__((packed)) {
    uint8_t rom[8];
    uint8_t function;
  } roles[4];
} RolesFile;

std::string g_dir;
std::map<uint32_t, gui_sample_t> g_samples; /* epoch -> sample */
std::map<uint32_t, RollupColumn> g_rollups; /* bucket minute -> column */
std::string g_log;
bool g_has_log = false;
uint8_t g_role_count = 0;
uint8_t g_role_rom[4][8];
uint8_t g_role_fn[4];
uint64_t g_sd_bytes = 0;
uint64_t g_spiffs_bytes = 0;
bool g_loaded = false;
bool g_energy_valid = false;
double g_energy_consumed_kwh = 0.0;
double g_energy_produced_kwh = 0.0;

uint32_t crc32_ieee(const void* data, size_t len) {
  const uint8_t* p = (const uint8_t*)data;
  uint32_t crc = 0xFFFFFFFFu;
  for (size_t i = 0; i < len; i++) {
    crc ^= p[i];
    for (int b = 0; b < 8; b++) {
      crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
  }
  return ~crc;
}

bool read_file(const std::string& path, std::vector<uint8_t>* out) {
  FILE* f = fopen(path.c_str(), "rb");
  if (!f) return false;
  if (fseek(f, 0, SEEK_END) != 0) {
    fclose(f);
    return false;
  }
  const long n = ftell(f);
  if (n < 0) {
    fclose(f);
    return false;
  }
  rewind(f);
  out->resize((size_t)n);
  const size_t got = n ? fread(out->data(), 1, (size_t)n, f) : 0;
  fclose(f);
  return got == (size_t)n;
}

bool ends_with(const std::string& s, const char* suffix) {
  const size_t n = strlen(suffix);
  return s.size() >= n && s.compare(s.size() - n, n, suffix) == 0;
}

bool starts_with(const std::string& s, const char* prefix) {
  const size_t n = strlen(prefix);
  return s.size() >= n && s.compare(0, n, prefix) == 0;
}

std::vector<std::string> list_files(const std::string& dir) {
  std::vector<std::string> names;
  DIR* d = opendir(dir.c_str());
  if (!d) return names;
  struct dirent* e;
  while ((e = readdir(d)) != nullptr) {
    if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0) continue;
    names.push_back(e->d_name);
  }
  closedir(d);
  std::sort(names.begin(), names.end());
  return names;
}

void add_bytes(const std::string& path, uint64_t* total) {
  struct stat st;
  if (stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode)) {
    *total += (uint64_t)st.st_size;
  }
}

void load_day_file(const std::string& path) {
  std::vector<uint8_t> file;
  if (!read_file(path, &file) || file.empty()) return;

  constexpr size_t records = 1440;
  constexpr size_t blocks = (records + 63) / 64;
  const size_t trailer = 4 * blocks;
  if (file.size() <= trailer) return;
  const size_t rec = (file.size() - trailer) / records;
  if (rec != sizeof(SampleV24) && rec != sizeof(gui_sample_t)) return;

  size_t off = 0;
  for (size_t base = 0; base < records; base += 64) {
    const size_t n = std::min<size_t>(64, records - base);
    const size_t bytes = n * rec;
    if (off + bytes + 4 > file.size()) break;
    uint32_t crc = 0;
    memcpy(&crc, file.data() + off + bytes, 4);
    if (crc != crc32_ieee(file.data() + off, bytes)) break;

    for (size_t i = 0; i < n; i++) {
      gui_sample_t s;
      memset(&s, 0, sizeof(s));
      if (rec == sizeof(gui_sample_t)) {
        memcpy(&s, file.data() + off + i * rec, sizeof(s));
      } else {
        SampleV24 v;
        memcpy(&v, file.data() + off + i * rec, sizeof(v));
        s.epoch = v.epoch;
        memcpy(s.temp, v.temp, sizeof(s.temp));
        s.living_c100 = v.living_c100;
        s.hum_c100 = v.hum_c100;
        s.power_dw = v.power_dw;
        s.pressure_pa = v.pressure_pa;
        s.flags = v.flags;
        s.stale_mask = v.stale_mask;
        s.target_c100 = v.target_c100;
        s.ac_rms_dc = 0;
        s.ac_angle_cdeg = 0;
      }
      if (s.epoch == 0) continue;
      g_samples[s.epoch - (s.epoch % 60)] = s;
    }
    off += bytes + 4;
  }
}

void load_rollup_file(const std::string& path, bool ring) {
  std::vector<uint8_t> file;
  if (!read_file(path, &file)) return;

  if (ring) {
    if (file.size() < kRingHeader + sizeof(RollupColumn)) return;
    uint32_t magic = 0;
    uint16_t version = 0;
    memcpy(&magic, file.data(), 4);
    memcpy(&version, file.data() + 4, 2);
    if (magic != kRingMagic || version != kRingVersion) return;
    const size_t count =
        (file.size() - kRingHeader) / sizeof(RollupColumn);
    for (size_t i = 0; i < count; i++) {
      RollupColumn col;
      memcpy(&col, file.data() + kRingHeader + i * sizeof(col), sizeof(col));
      if (col.t != 0) g_rollups[col.t] = col;
    }
    return;
  }

  constexpr size_t columns = 744;
  size_t off = 0;
  for (size_t base = 0; base < columns; base += 64) {
    const size_t n = std::min<size_t>(64, columns - base);
    const size_t bytes = n * sizeof(RollupColumn);
    if (off + bytes + 4 > file.size()) break;
    uint32_t crc = 0;
    memcpy(&crc, file.data() + off + bytes, 4);
    if (crc != crc32_ieee(file.data() + off, bytes)) break;
    for (size_t i = 0; i < n; i++) {
      RollupColumn col;
      memcpy(&col, file.data() + off + i * sizeof(col), sizeof(col));
      if (col.t != 0) g_rollups[col.t] = col;
    }
    off += bytes + 4;
  }
}

void load_log(const std::string& dir) {
  std::string newest;
  for (const std::string& name : list_files(dir)) {
    if (!starts_with(name, "log_") || !ends_with(name, ".txt")) continue;
    if (newest.empty() || name > newest) newest = name;
  }
  if (newest.empty()) return;
  std::vector<uint8_t> file;
  if (!read_file(dir + "/" + newest, &file)) return;
  g_log.assign((const char*)file.data(), file.size());
  g_has_log = true;
}

void load_energy(const std::string& path) {
  FILE* f = fopen(path.c_str(), "r");
  if (!f) return;
  char line[160];
  while (fgets(line, sizeof(line), f)) {
    unsigned long epoch = 0;
    double consumed = 0.0;
    double produced = 0.0;
    if (sscanf(line, "%lu,%lf,%lf", &epoch, &consumed, &produced) == 3 &&
        epoch != 0) {
      g_energy_consumed_kwh = consumed;
      g_energy_produced_kwh = produced;
      g_energy_valid = true;
    }
  }
  fclose(f);
}

void load_roles(const std::string& path) {
  std::vector<uint8_t> file;
  if (!read_file(path, &file) || file.size() < kRoleHeader) return;
  RolesFile rf;
  memset(&rf, 0, sizeof(rf));
  memcpy(&rf, file.data(), std::min(file.size(), sizeof(rf)));
  if (rf.magic != kRoleMagic || rf.version != kRoleVersion) return;
  const uint8_t count = std::min<uint8_t>(rf.count, 4);
  g_role_count = count;
  for (uint8_t i = 0; i < count; i++) {
    memcpy(g_role_rom[i], rf.roles[i].rom, 8);
    g_role_fn[i] = rf.roles[i].function;
  }
}

std::string find_default_dump() {
  const char* env = getenv("HAUS_DUMP");
  if (env && env[0]) return env;

  std::vector<std::string> candidates;
  for (const char* parent : {"..", "."}) {
    const std::string base = parent;
    for (const std::string& name : list_files(base)) {
      if (!starts_with(name, "dump_")) continue;
      struct stat st;
      const std::string path = base + "/" + name;
      if (stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode))
        candidates.push_back(path);
    }
  }
  if (candidates.empty()) return std::string();
  std::sort(candidates.begin(), candidates.end());
  return candidates.back();
}

int16_t series_mean(const RollupColumn& col, int series) {
  if (series < 0 || series >= HAUS_HISTORY_SERIES || col.n[series] == 0)
    return GUI_EMPTY;
  return (int16_t)(col.sum[series] / col.n[series]);
}

bool rollup_for_epoch(uint32_t epoch, RollupColumn* out) {
  const uint32_t key = (epoch / 3600u) * 60u;
  const auto it = g_rollups.find(key);
  if (it == g_rollups.end()) return false;
  if (out) *out = it->second;
  return true;
}

void fill_from_rollup(uint32_t epoch, gui_sample_t* s) {
  RollupColumn col;
  memset(s, 0, sizeof(*s));
  s->epoch = epoch;
  s->temp[0] = s->temp[1] = s->temp[2] = HAUS_CAN_TEMP_EMPTY;
  s->living_c100 = HAUS_CAN_TEMP_EMPTY;
  s->target_c100 = GUI_EMPTY;
  s->flags = SAMPLE_FLAG_GAP;
  if (!rollup_for_epoch(epoch, &col)) return;

  s->temp[0] = series_mean(col, 0);
  s->temp[1] = series_mean(col, 1);
  s->temp[2] = series_mean(col, 2);
  const int16_t living = series_mean(col, 3);
  const int16_t hum = series_mean(col, 4);
  if (living != GUI_EMPTY) {
    s->living_c100 = living;
    s->flags |= SAMPLE_FLAG_CLIMATE_AHT20;
  }
  if (hum != GUI_EMPTY) {
    s->hum_c100 = (uint16_t)hum;
    s->flags |= SAMPLE_FLAG_CLIMATE_AHT20;
  }
  if (col.n[6] != 0) s->power_dw = (uint16_t)(col.sum[6] / col.n[6]);
  if (col.n[7] != 0) s->ac_rms_dc = (uint16_t)(col.sum[7] / col.n[7]);
  if (col.n[8] != 0) s->ac_angle_cdeg = (int16_t)(col.sum[8] / col.n[8]);
  s->target_c100 = col.target_c100;
  s->flags &= (uint8_t)~SAMPLE_FLAG_GAP;
}

}  // namespace

bool dump_load(const char* dir) {
  g_samples.clear();
  g_rollups.clear();
  g_log.clear();
  g_has_log = false;
  g_role_count = 0;
  g_sd_bytes = g_spiffs_bytes = 0;
  g_energy_valid = false;
  g_energy_consumed_kwh = g_energy_produced_kwh = 0.0;
  g_loaded = false;
  g_dir = (dir && dir[0]) ? dir : find_default_dump();
  if (g_dir.empty()) return false;

  const std::string sd = g_dir + "/sd";
  const std::string spiffs = g_dir + "/spiffs";

  for (const std::string& name : list_files(sd)) {
    const std::string path = sd + "/" + name;
    add_bytes(path, &g_sd_bytes);
    if (starts_with(name, "data_") && ends_with(name, ".bin")) {
      load_day_file(path);
    } else if (name == "rollup.bin") {
      load_rollup_file(path, false);
    }
  }

  for (const std::string& name : list_files(spiffs)) {
    add_bytes(spiffs + "/" + name, &g_spiffs_bytes);
  }
  /* The ring overrides the SD rollup for overlapping hours: it is written
   * when each hour completes, the SD rollup only at midnight. */
  load_rollup_file(spiffs + "/rollup_ring.bin", true);
  load_log(sd);
  load_energy(sd + "/energy.csv");
  load_roles(spiffs + "/sensor_roles.bin");

  g_loaded = !g_samples.empty() || !g_rollups.empty();
  return g_loaded;
}

bool dump_loaded(void) { return g_loaded; }
const char* dump_directory(void) { return g_dir.c_str(); }

uint32_t dump_newest_epoch(void) {
  uint32_t newest = 0;
  if (!g_samples.empty()) newest = g_samples.rbegin()->first;
  if (!g_rollups.empty()) {
    const uint32_t hour = g_rollups.rbegin()->first * 60u + 59u * 60u;
    if (hour > newest) newest = hour;
  }
  return newest;
}

uint32_t dump_oldest_epoch(void) {
  uint32_t oldest = 0;
  if (!g_samples.empty()) oldest = g_samples.begin()->first;
  if (!g_rollups.empty()) {
    const uint32_t hour = g_rollups.begin()->first * 60u;
    if (oldest == 0 || hour < oldest) oldest = hour;
  }
  return oldest;
}

bool dump_latest_sample(gui_sample_t* out) {
  if (!out || g_samples.empty()) return false;
  *out = g_samples.rbegin()->second;
  return true;
}

bool dump_latest_rollup(RollupColumn* out) {
  if (!out || g_rollups.empty()) return false;
  *out = g_rollups.rbegin()->second;
  return true;
}

size_t dump_read_samples_chunk(uint32_t start_epoch, size_t first, size_t count,
                               gui_sample_t* out) {
  if (!out || count == 0) return 0;
  const uint32_t base = start_epoch - (start_epoch % 60u);
  for (size_t i = 0; i < count; i++) {
    const uint32_t epoch = base + (uint32_t)(first + i) * 60u;
    const auto it = g_samples.find(epoch);
    if (it != g_samples.end()) {
      out[i] = it->second;
    } else {
      fill_from_rollup(epoch, &out[i]);
    }
  }
  return count;
}

size_t dump_read_rollup_chunk(uint32_t start_epoch, size_t first, size_t count,
                              RollupColumn* out) {
  if (!out || count == 0) return 0;
  const uint32_t base = start_epoch - (start_epoch % 3600u);
  for (size_t i = 0; i < count; i++) {
    const uint32_t epoch = base + (uint32_t)(first + i) * 3600u;
    const uint32_t key = epoch / 60u;
    const auto it = g_rollups.find(key);
    if (it != g_rollups.end()) {
      out[i] = it->second;
    } else {
      memset(&out[i], 0, sizeof(out[i]));
      out[i].t = key;
      out[i].target_c100 = GUI_EMPTY;
    }
  }
  return count;
}

size_t dump_read_target_samples_chunk(uint32_t start_epoch, size_t first,
                                      size_t count, int16_t* out) {
  if (!out || count == 0) return 0;
  const uint32_t base = start_epoch - (start_epoch % 60u);
  for (size_t i = 0; i < count; i++) {
    const uint32_t epoch = base + (uint32_t)(first + i) * 60u;
    const auto it = g_samples.find(epoch);
    if (it != g_samples.end()) {
      out[i] = it->second.target_c100;
      continue;
    }
    RollupColumn col;
    out[i] = rollup_for_epoch(epoch, &col) ? col.target_c100 : GUI_EMPTY;
  }
  return count;
}

size_t dump_read_target_hours_chunk(uint32_t start_epoch, size_t first,
                                    size_t count, int16_t* out) {
  if (!out || count == 0) return 0;
  const uint32_t base = start_epoch - (start_epoch % 3600u);
  for (size_t i = 0; i < count; i++) {
    const uint32_t epoch = base + (uint32_t)(first + i) * 3600u;
    const auto it = g_rollups.find(epoch / 60u);
    out[i] = it != g_rollups.end() ? it->second.target_c100 : GUI_EMPTY;
  }
  return count;
}

const char* dump_log(int* out_len) {
  if (!g_has_log) {
    if (out_len) *out_len = 0;
    return nullptr;
  }
  if (out_len) *out_len = (int)g_log.size();
  return g_log.c_str();
}

uint8_t dump_roles_count(void) { return g_role_count; }

bool dump_roles_get(uint8_t index, uint8_t rom[8], uint8_t* function) {
  if (index >= g_role_count) return false;
  if (rom) memcpy(rom, g_role_rom[index], 8);
  if (function) *function = g_role_fn[index];
  return true;
}

uint64_t dump_sd_bytes(void) { return g_sd_bytes; }
uint64_t dump_spiffs_bytes(void) { return g_spiffs_bytes; }

bool dump_energy_latest(double* consumed_kwh, double* produced_kwh) {
  if (!g_energy_valid) return false;
  if (consumed_kwh) *consumed_kwh = g_energy_consumed_kwh;
  if (produced_kwh) *produced_kwh = g_energy_produced_kwh;
  return true;
}
