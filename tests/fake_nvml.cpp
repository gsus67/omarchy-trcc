// Stand-in for libnvidia-ml.so used by the engine self-test: one GPU at
// 120 W, 65 °C, 90 % load and 2 of 8 GiB of VRAM. Counts sessions so the test can tell whether
// the engine queried the GPU.
static int sessions = 0;
struct Utilization { unsigned gpu, memory; };
struct Memory { unsigned long long total, free, used; };
extern "C" {
int nvmlInit_v2() { ++sessions; return 0; }
int nvmlShutdown() { return 0; }
int nvmlDeviceGetCount_v2(unsigned *count) { *count = 1; return 0; }
int nvmlDeviceGetHandleByIndex_v2(unsigned, void **device) { *device = &sessions; return 0; }
int nvmlDeviceGetPowerUsage(void *, unsigned *milliwatts) { *milliwatts = 120000; return 0; }
int nvmlDeviceGetTemperature(void *, int, unsigned *celsius) { *celsius = 65; return 0; }
int nvmlDeviceGetUtilizationRates(void *, Utilization *use) { use->gpu = 90; use->memory = 10; return 0; }
int nvmlDeviceGetMemoryInfo(void *, Memory *memory) { memory->total = 8ull << 30; memory->used = 2ull << 30; memory->free = 6ull << 30; return 0; }
int fakeNvmlSessions() { return sessions; }
}
