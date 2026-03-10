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
    printf("  --width N        Matrix width (default: 32)\n");
    printf("  --height N       Matrix height (default: 24)\n");
    printf("  --scenario N     Run only scenario N (1-indexed)\n");
    printf("  --viz            Enable 2D visualization\n");
    printf("  --csv FILE       Export to CSV\n");
    printf("  --frames N1-N2   Only show frames N1 to N2 (with --viz)\n");
    printf("  --list           List all scenarios and exit\n");
    printf("  --help           Show this help\n");
}

void listScenarios(uint16_t width, uint16_t height) {
    auto scenarios = getAllScenarios();
    printf("Available scenarios (%zu total), matrix %dx%d:\n\n", scenarios.size(), width, height);
    for (size_t i = 0; i < scenarios.size(); i++) {
        printf("%3zu. %s\n", i + 1, scenarios[i].name.c_str());
        printf("      Mode: %d, Duration: %lu ms, Frames: %lu\n",
               scenarios[i].mode, scenarios[i].durationMs,
               scenarios[i].durationMs / scenarios[i].frameIntervalMs + 1);
    }
}

void runScenario(const TestScenario& scenario, uint16_t width, uint16_t height, 
                 Analyzer& analyzer, bool showViz, int frameStart, int frameEnd,
                 const char* csvFile, int scenarioIdx) {
    printf("\n");
    printf("═══════════════════════════════════════════════════════════════════════════\n");
    printf("SCENARIO %d: %s\n", scenarioIdx, scenario.name.c_str());
    printf("═══════════════════════════════════════════════════════════════════════════\n");
    printf("  Description: %s\n", scenario.description.c_str());
    printf("  Matrix: %dx%d, Duration: %lu ms, Frame interval: %lu ms\n",
           width, height, scenario.durationMs, scenario.frameIntervalMs);
    
    FrameRecorder recorder(width, height, scenario.durationMs / scenario.frameIntervalMs + 10);
    LedColor* pixels = new LedColor[(uint32_t)width * height];
    
    setMillis(0);
    
    unsigned long frameCount = 0;
    for (unsigned long t = 0; t <= scenario.durationMs; t += scenario.frameIntervalMs) {
        setMillis(t);
        
        calculateAllPixels(
            pixels, width, height, t,
            scenario.mode, scenario.brightness,
            scenario.staticR, scenario.staticG, scenario.staticB,
            scenario.bgStyle, scenario.bgSpeed, scenario.waveLength
        );
        
        recorder.recordFrame(pixels);
        frameCount++;
    }
    
    printf("  Frames recorded: %d\n", recorder.getFrameCount());
    
    auto flickers = analyzer.detectFlicker(recorder);
    analyzer.printFlickerReport(flickers);
    
    if (showViz) {
        printf("\n  2D Visualization:\n");
        int start = frameStart >= 0 ? frameStart : 0;
        int end = frameEnd >= 0 ? frameEnd : recorder.getFrameCount() - 1;
        if (end >= recorder.getFrameCount()) end = recorder.getFrameCount() - 1;
        
        for (int f = start; f <= end; f++) {
            analyzer.printFrame2D(recorder, f);
        }
    }
    
    if (csvFile) {
        char fname[256];
        snprintf(fname, sizeof(fname), "%s_%d.csv", csvFile, scenarioIdx);
        
        FILE* fp = fopen(fname, "w");
        if (fp) {
            fprintf(fp, "frame,time_ms,x,y,R,G,B\n");
            for (uint16_t f = 0; f < recorder.getFrameCount(); f++) {
                const LedColor* frame = recorder.getFrame(f);
                for (uint16_t y = 0; y < height; y++) {
                    for (uint16_t x = 0; x < width; x++) {
                        uint32_t idx = y * width + x;
                        fprintf(fp, "%d,%lu,%d,%d,%d,%d,%d\n",
                                f, (unsigned long)f * scenario.frameIntervalMs, x, y,
                                frame[idx].R, frame[idx].G, frame[idx].B);
                    }
                }
            }
            fclose(fp);
            printf("  CSV exported to: %s\n", fname);
        }
    }
    
    delete[] pixels;
}

int main(int argc, char* argv[]) {
    uint8_t flickerThreshold = 15;
    uint16_t width = 32;
    uint16_t height = 24;
    int scenarioNum = -1;
    bool showViz = false;
    const char* csvFile = nullptr;
    int frameStart = -1;
    int frameEnd = -1;
    
    static struct option long_options[] = {
        {"threshold", required_argument, 0, 't'},
        {"width",     required_argument, 0, 'W'},
        {"height",    required_argument, 0, 'H'},
        {"scenario",  required_argument, 0, 's'},
        {"viz",       no_argument,       0, 'v'},
        {"csv",       required_argument, 0, 'c'},
        {"frames",    required_argument, 0, 'f'},
        {"list",      no_argument,       0, 'L'},
        {"help",      no_argument,       0, 'h'},
        {0, 0, 0, 0}
    };
    
    int opt;
    while ((opt = getopt_long(argc, argv, "t:W:H:s:vc:f:Lh", long_options, nullptr)) != -1) {
        switch (opt) {
            case 't':
                flickerThreshold = (uint8_t)atoi(optarg);
                break;
            case 'W':
                width = (uint16_t)atoi(optarg);
                break;
            case 'H':
                height = (uint16_t)atoi(optarg);
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
            case 'f':
                if (sscanf(optarg, "%d-%d", &frameStart, &frameEnd) != 2) {
                    frameStart = atoi(optarg);
                    frameEnd = frameStart;
                }
                break;
            case 'L':
                listScenarios(width, height);
                return 0;
            case 'h':
            default:
                printUsage(argv[0]);
                return opt == 'h' ? 0 : 1;
        }
    }
    
    printf("\n");
    printf("╔═══════════════════════════════════════════════════════════════════════════╗\n");
    printf("║            LED MATRIX 2D TEST HARNESS - Effect Visualization             ║\n");
    printf("╚═══════════════════════════════════════════════════════════════════════════╝\n");
    printf("\n");
    printf("Configuration:\n");
    printf("  Matrix size:    %dx%d\n", width, height);
    printf("  Flicker thresh: %d\n", flickerThreshold);
    printf("  Visualization:  %s\n", showViz ? "enabled" : "disabled");
    printf("  CSV export:     %s\n", csvFile ? csvFile : "disabled");
    
    Analyzer analyzer(flickerThreshold);
    auto scenarios = getAllScenarios();
    
    if (scenarioNum > 0 && scenarioNum <= (int)scenarios.size()) {
        runScenario(scenarios[scenarioNum - 1], width, height, analyzer, showViz, frameStart, frameEnd, csvFile, scenarioNum);
    } else {
        for (size_t i = 0; i < scenarios.size(); i++) {
            runScenario(scenarios[i], width, height, analyzer, showViz, frameStart, frameEnd, csvFile, i + 1);
        }
    }
    
    printf("\n═══════════════════════════════════════════════════════════════════════════\n");
    printf("TEST COMPLETE\n");
    printf("═══════════════════════════════════════════════════════════════════════════\n\n");
    
    return 0;
}
