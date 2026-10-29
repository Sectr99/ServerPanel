// ArduinoJson documents in PSRAM. A document is built from many small blocks, and malloc puts anything under 4 KB in
// internal RAM, the scarce kind that TLS connections and the Wi-Fi co-processor link need. Use JsonDocument doc(&psram).
#pragma once
#include <ArduinoJson.h>
#include <esp_heap_caps.h>

struct PsramAllocator : ArduinoJson::Allocator {
  void* allocate(size_t n) override { return heap_caps_malloc(n, MALLOC_CAP_SPIRAM); }
  void deallocate(void* p) override { heap_caps_free(p); }
  void* reallocate(void* p, size_t n) override { return heap_caps_realloc(p, n, MALLOC_CAP_SPIRAM); }
};
inline PsramAllocator psram;
