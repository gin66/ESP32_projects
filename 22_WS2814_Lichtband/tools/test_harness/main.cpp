#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <getopt.h>

#include "mock_platform.h"
#include "recorder.h"
#include "analyzer.h"
#include "test_scenarios.h"
#include "../../include/led_effects.h"

unsigned long mock_millis = 0;

void printUsage(const char* prog) {
    printf("Usage: %s [options]\n", prog);
    printf("Options:\n");
    printf("  --threshold N    Flicker threshold (default: 15)\n");
    printf("  --leds N         LED count (default: 80)\n");
    printf("  --scenario N     Run only scenario N (1-indexed)\n");
    printf("  --viz            Enable ASCII visualization\n");
    printf("  --csv FILE       Export to CSV\n");
    printf("  --mode MODE      Visualization mode: rgb or gray (default: rgb)\n");
    printf("  --list           List all scenarios and exit\n");
    printf("  --help           Show this help\n");
}

void listScenarios() {
    auto scenarios = getAllScenarios();
    printf("Available scenarios (%zu total):\n\n", scenarios.size());
    for (size_t i = 0; i < scenarios.size(); i++) {
        printf("%3zu. %s\n", i + 1, scenarios[i].name.c_str());
        printf("      Mode: %d, Duration: %lu ms, Frames: %lu\n",
               scenarios[i].mode, scenarios[i].durationMs,
               scenarios[i].durationMs / scenarios[i].frameIntervalMs + 1);
    }
}

void runScenario(const TestScenario& scenario, uint16_t ledCount, 
                 Analyzer& analyzer, bool showViz, VizMode vizMode,
                 const char* csvFile) {
    printf("\n");
    printf("═══════════════════════════════════════════════════════════════════════════\n");
    printf("SCENARIO: %s\n", scenario.name.c_str());
    printf("═══════════════════════════════════════════════════════════════════════════\n");
    printf("  Description: %s\n", scenario.description.c_str());
    printf("  LEDs: %d, Duration: %lu ms, Frame interval: %lu ms\n",
           ledCount, scenario.durationMs, scenario.frameIntervalMs);
    
    FrameRecorder recorder(ledCount, scenario.durationMs / scenario.frameIntervalMs + 10);
    RgbwColor* leds = new RgbwColor[ledCount];
    LedState state;
    
    setMillis(0);
    
    unsigned long frameCount = 0;
    for (unsigned long t = 0; t <= scenario.durationMs; t += scenario.frameIntervalMs) {
        setMillis(t);
        
        calculateAllLeds(
            leds, ledCount, t,
            scenario.mode, scenario.brightness,
            scenario.waveCfg, state,
            scenario.staticR, scenario.staticG, scenario.staticB, scenario.staticW,
            scenario.rainbowSpeed, scenario.whiteSpeed
        );
        
        recorder.recordFrame(leds);
        frameCount++;
    }
    
    printf("  Frames recorded: %d\n", recorder.getFrameCount());
    
    auto flickers = analyzer.detectFlicker(recorder);
    analyzer.printFlickerReport(flickers);
    
    if (showViz) {
        printf("\n  Visualization (%s mode):\n", vizMode == VizMode::RGB ? "RGB" : "Grayscale");
        
        for (uint16_t f = 0; f < recorder.getFrameCount(); f++) {
            printf("  %04d: ", f);
            analyzer.printFrame(recorder, f, vizMode, ledCount);
        }
    }
    
    if (csvFile) {
        static int csvIndex = 0;
        char fname[256];
        snprintf(fname, sizeof(fname), "%s_%d.csv", csvFile, csvIndex++);
        
        FILE* fp = fopen(fname, "w");
        if (fp) {
            fprintf(fp, "frame,time_ms,led,W,R,G,B\n");
            for (uint16_t f = 0; f < recorder.getFrameCount(); f++) {
                const RgbwColor* frame = recorder.getFrame(f);
                for (uint16_t l = 0; l < ledCount; l++) {
                    fprintf(fp, "%d,%lu,%d,%d,%d,%d,%d\n",
                            f, (unsigned long)f * scenario.frameIntervalMs, l,
                            frame[l].W, frame[l].R, frame[l].G, frame[l].B);
                }
            }
            fclose(fp);
            printf("  CSV exported to: %s\n", fname);
        }
    }
    
    delete[] leds;
}

int main(int argc, char* argv[]) {
    uint8_t flickerThreshold = 15;
    uint16_t ledCount = 80;
    int scenarioNum = -1;
    bool showViz = false;
    const char* csvFile = nullptr;
    VizMode vizMode = VizMode::RGB;
    
    static struct option long_options[] = {
        {"threshold", required_argument, 0, 't'},
        {"leds",      required_argument, 0, 'l'},
        {"scenario",  required_argument, 0, 's'},
        {"viz",       no_argument,       0, 'v'},
        {"csv",       required_argument, 0, 'c'},
        {"mode",      required_argument, 0, 'm'},
        {"list",      no_argument,       0, 'L'},
        {"help",      no_argument,       0, 'h'},
        {0, 0, 0, 0}
    };
    
    int opt;
    while ((opt = getopt_long(argc, argv, "t:l:s:vc:m:Lh", long_options, nullptr)) != -1) {
        switch (opt) {
            case 't':
                flickerThreshold = (uint8_t)atoi(optarg);
                break;
            case 'l':
                ledCount = (uint16_t)atoi(optarg);
                break;
            case 's':
                scenarioNum = atoi(optarg);
                break;
            case 'v':
                showViz = true;
                break;
            case 'c':
                csvFile = optarg;
                break;
            case 'm':
                if (strcmp(optarg, "gray") == 0 || strcmp(optarg, "grayscale") == 0) {
                    vizMode = VizMode::Grayscale;
                } else {
                    vizMode = VizMode::RGB;
                }
                break;
            case 'L':
                listScenarios();
                return 0;
            case 'h':
            default:
                printUsage(argv[0]);
                return opt == 'h' ? 0 : 1;
        }
    }
    
    printf("\n");
    printf("╔═══════════════════════════════════════════════════════════════════════════╗\n");
    printf("║              LED EFFECTS TEST HARNESS - Flicker Analysis                 ║\n");
    printf("╚═══════════════════════════════════════════════════════════════════════════╝\n");
    printf("\n");
    printf("Configuration:\n");
    printf("  LED count:      %d\n", ledCount);
    printf("  Flicker thresh: %d\n", flickerThreshold);
    printf("  Visualization:  %s\n", showViz ? (vizMode == VizMode::RGB ? "RGB" : "Grayscale") : "disabled");
    printf("  CSV export:     %s\n", csvFile ? csvFile : "disabled");
    
    Analyzer analyzer(flickerThreshold);
    auto scenarios = getAllScenarios();
    
    if (scenarioNum > 0 && scenarioNum <= (int)scenarios.size()) {
        runScenario(scenarios[scenarioNum - 1], ledCount, analyzer, showViz, vizMode, csvFile);
    } else {
        for (const auto& scenario : scenarios) {
            runScenario(scenario, ledCount, analyzer, showViz, vizMode, csvFile);
        }
    }
    
    printf("\n═══════════════════════════════════════════════════════════════════════════\n");
    printf("TEST COMPLETE\n");
    printf("═══════════════════════════════════════════════════════════════════════════\n\n");
    
    return 0;
}
