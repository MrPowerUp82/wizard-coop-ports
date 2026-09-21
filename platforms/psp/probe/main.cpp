// PSP CPU probe: runs the shared simulation scenarios (tools/probe/probe_scenarios.hpp) on the
// real Allegrex CPU and reports microseconds per tick, before any renderer exists. The answer
// decides whether the PSP port needs a float core, a 30 Hz simulation or lower entity caps.
//
// Results appear on screen and in arcana_probe.txt next to the EBOOT (ms0:/PSP/GAME/<folder>/;
// on a Vita with Adrenaline that is ux0:pspemu/PSP/GAME/<folder>/).
#include "probe_scenarios.hpp"

#include <pspctrl.h>
#include <pspdebug.h>
#include <pspdisplay.h>
#include <pspkernel.h>
#include <psppower.h>

#include <cstdio>
#include <cstring>
#include <string>

PSP_MODULE_INFO("ArcanaProbe", 0, 1, 0);
PSP_MAIN_THREAD_ATTR(THREAD_ATTR_USER | THREAD_ATTR_VFPU);
PSP_MAIN_THREAD_STACK_SIZE_KB(256);
PSP_HEAP_SIZE_KB(-1024); // everything but 1 MB for the system

namespace {

int exitCallback(int, int, void*) { sceKernelExitGame(); return 0; }
int callbackThread(SceSize, void*) {
  const int id = sceKernelCreateCallback("Exit Callback", exitCallback, nullptr);
  sceKernelRegisterExitCallback(id);
  sceKernelSleepThreadCB();
  return 0;
}
void setupCallbacks() {
  const int thread = sceKernelCreateThread("exit_thread", callbackThread, 0x11, 0xFA0, 0, nullptr);
  if (thread >= 0) sceKernelStartThread(thread, 0, nullptr);
}

FILE* g_log = nullptr;

void out(const char* text) {
  pspDebugScreenPrintf("%s\n", text);
  if (g_log) { std::fprintf(g_log, "%s\n", text); std::fflush(g_log); }
}

} // namespace

int main(int argc, char** argv) {
  setupCallbacks();
  // Full speed, as the game would run.
  scePowerSetClockFrequency(333, 333, 166);
  pspDebugScreenInit();

  std::string dir = argc > 0 && argv[0] ? argv[0] : "ms0:/";
  dir = dir.substr(0, dir.find_last_of('/') + 1);
  g_log = std::fopen((dir + "arcana_probe.txt").c_str(), "w");

  char line[128];
  out("Arcana Survivors - probe de CPU do PSP");
  std::snprintf(line, sizeof line, "CPU %d MHz, bus %d MHz, memoria livre %u KB, GameState %u KB",
                scePowerGetCpuClockFrequency(), scePowerGetBusClockFrequency(),
                static_cast<unsigned>(sceKernelTotalFreeMemSize() / 1024), static_cast<unsigned>(sizeof(arcana::GameState) / 1024));
  out(line);
  out("Orcamento de um frame a 60 fps: 16667 us. Rodando (~2 min)...");
  out("");

  auto now = [] { return static_cast<long long>(sceKernelGetSystemTimeWide()); };
  arcana::probe::runAll(now, [&](const arcana::probe::Result& r) {
    out(r.name);
    if (r.worstUs > 0)
      std::snprintf(line, sizeof line, "   media %.0f us/tick, pior %.0f us, inimigos %d, tiros %d", r.avgUs, r.worstUs, r.enemies, r.shots);
    else
      std::snprintf(line, sizeof line, "   %.0f us", r.avgUs);
    out(line);
  });

  out("");
  out(g_log ? "Resultados salvos em arcana_probe.txt, na pasta do EBOOT."
            : "(nao foi possivel gravar arcana_probe.txt)");
  out("Aperte START para sair.");
  if (g_log) std::fclose(g_log);

  SceCtrlData pad{};
  sceCtrlSetSamplingCycle(0);
  while (true) {
    sceCtrlReadBufferPositive(&pad, 1);
    if (pad.Buttons & PSP_CTRL_START) break;
    sceDisplayWaitVblankStart();
  }
  sceKernelExitGame();
  return 0;
}
