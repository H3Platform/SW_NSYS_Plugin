#include <getopt.h>
#include <iostream>
#include <map>
#include <nvtx3/nvToolsExtCounters.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <unistd.h>
#include <vector>

#include "h3ppci.h"

#define LOG_ERR(...)                                                           \
  fprintf(stderr, __VA_ARGS__);                                                \
  fputs("\n", stderr)

struct Config {
  int deviceIdx = -1;                // -1 means ALL
  std::vector<int> portIndices;      // Empty means ALL
  std::vector<std::string> modules = {"throughput"}; // throughput | error | temperature
  int intervalMs = 10;
};

struct MonitoredPort {
  h3ppciDevice_t dev;
  int deviceIdx;
  int portIndex; // Index used in API calls
  int portId;    // Logical Port ID for display
  nvtxDomainHandle_t domain;
  uint64_t counter;
  std::string deviceName;
  std::string moduleName;
};

void PrintHelp(const char *progName) {
  printf("Usage: %s [options]\n", progName);
  printf("  -i <idx>      Device index (default: all)\n");
  printf("  -p <p1,p2,..> Port Indices (comma separated, default: all)\n");
  printf("  -m <module>   Module: throughput | error | temperature | all (comma separated, default: throughput)\n");
  printf("  -t <ms>       Interval in milliseconds (default: 100)\n");
  printf("  -h            Print this help message\n");
}

Config ParseArgs(int argc, char **argv) {
  Config config;
  int opt;
  while ((opt = getopt(argc, argv, "i:p:m:t:h")) != -1) {
    switch (opt) {
    case 'i':
      config.deviceIdx = atoi(optarg);
      break;
    case 'p': {
      char *ptr = strtok(optarg, ",");
      while (ptr != nullptr) {
        config.portIndices.push_back(atoi(ptr));
        ptr = strtok(nullptr, ",");
      }
      while (optind < argc && argv[optind][0] != '-') {
        char *extraPtr = strtok(argv[optind], ",");
        while (extraPtr != nullptr) {
          config.portIndices.push_back(atoi(extraPtr));
          extraPtr = strtok(nullptr, ",");
        }
        optind++;
      }
      break;
    }
    case 'm': {
      config.modules.clear();
      auto parseModStr = [&](std::string modStr) {
        if (modStr == "all") {
          config.modules = {"throughput", "error", "temperature"};
        } else {
          size_t pos = 0;
          while ((pos = modStr.find(',')) != std::string::npos) {
            config.modules.push_back(modStr.substr(0, pos));
            modStr.erase(0, pos + 1);
          }
          if (!modStr.empty()) config.modules.push_back(modStr);
        }
      };
      parseModStr(optarg);

      // Consume any extra arguments that were split by Nsight's comma separator
      while (optind < argc && argv[optind][0] != '-') {
        parseModStr(argv[optind]);
        optind++;
      }
      break;
    }
    case 't':
      config.intervalMs = atoi(optarg);
      break;
    case 'h':
      PrintHelp(argv[0]);
      exit(0);
    default:
      PrintHelp(argv[0]);
      exit(1);
    }
  }
  return config;
}

int main(int argc, char **argv) {
  Config config = ParseArgs(argc, argv);

  for (const auto& mod : config.modules) {
    if (mod != "throughput" && mod != "error" && mod != "temperature") {
      LOG_ERR("Invalid module: %s. Must be 'throughput', 'error', 'temperature' or 'all'.", mod.c_str());
      return 1;
    }
  }

  int totalDevices = 0;
  if (h3ppciGetDeviceCount(&totalDevices) != H3PPCI_SUCCESS ||
      totalDevices == 0) {
    LOG_ERR("No H3P devices found.");
    return 1;
  }

  std::vector<MonitoredPort> monitoredPorts;
  std::vector<h3ppciDevice_t> activeDevices;
  std::map<int, nvtxDomainHandle_t> deviceDomains;

  for (int d = 0; d < totalDevices; ++d) {
    if (config.deviceIdx != -1 && config.deviceIdx != d)
      continue;

    h3ppciDevice_t dev;
    if (h3ppciGetDevice(&dev, d) != H3PPCI_SUCCESS)
      continue;

    h3ppciDeviceProp prop;
    h3ppciGetDeviceProperties(&prop, dev);

    if (h3ppciInitDevice(dev) != H3PPCI_SUCCESS) {
      LOG_ERR("Failed to initialize device %d (Check permissions or Root).", d);
      continue;
    }

    char bdfStr[32];
    snprintf(bdfStr, sizeof(bdfStr), "%04x:%02x:%02x.%x", prop.domain, prop.bus,
             prop.device, prop.function);

    std::string domainName = std::string("H3P_PCIe_Switch/") + prop.name + "_" +
                             std::to_string(d) + "(" + bdfStr + ")";
    nvtxDomainHandle_t domain = nvtxDomainCreateA(domainName.c_str());
    deviceDomains[d] = domain;
    activeDevices.push_back(dev);

    int portCount = 0;
    h3ppciGetPortCount(dev, &portCount);

    for (const auto& mod : config.modules) {
      if (mod == "throughput" || mod == "error") {
        std::vector<std::string> metricNames;
        if (mod == "throughput") {
          metricNames = {"RX_MBs", "TX_MBs", "RX_Util", "TX_Util"};
        } else {
          metricNames = {"BadTLP", "BadDLLP", "RxErr", "RecDiag"};
        }

        nvtxPayloadSchemaEntry_t* entries = new nvtxPayloadSchemaEntry_t[metricNames.size()];
        for (size_t i = 0; i < metricNames.size(); i++) {
          entries[i] = {0, NVTX_PAYLOAD_ENTRY_TYPE_DOUBLE,
                        strdup(metricNames[i].c_str()), "", 0, 0, nullptr, nullptr};
        }

        nvtxPayloadSchemaAttr_t schemaAttr;
        memset(&schemaAttr, 0, sizeof(schemaAttr));
        schemaAttr.fieldMask = NVTX_PAYLOAD_SCHEMA_ATTR_FIELD_TYPE |
                               NVTX_PAYLOAD_SCHEMA_ATTR_FIELD_ENTRIES |
                               NVTX_PAYLOAD_SCHEMA_ATTR_FIELD_NUM_ENTRIES |
                               NVTX_PAYLOAD_SCHEMA_ATTR_FIELD_STATIC_SIZE;
        schemaAttr.type = NVTX_PAYLOAD_SCHEMA_TYPE_STATIC;
        schemaAttr.entries = entries;
        schemaAttr.numEntries = metricNames.size();
        schemaAttr.payloadStaticSize = metricNames.size() * sizeof(double);

        const uint64_t schemaId = nvtxPayloadSchemaRegister(domain, &schemaAttr);

        for (int p = 0; p < portCount; p++) {
          h3ppciPortInfo portInfo;
          if (h3ppciGetPortInfo(dev, p, &portInfo) != H3PPCI_SUCCESS)
            continue;

          if (!config.portIndices.empty()) {
            bool found = false;
            for (int pi : config.portIndices) {
              if (pi == portInfo.portId) {
                found = true;
                break;
              }
            }
            if (!found)
              continue;
          }

          if (config.portIndices.empty()) {
            if (portInfo.curLink.width <= 0 && !portInfo.enabled) {
              continue;
            }
          }

          std::string counterName = std::string("Port_") +
                                    std::to_string(portInfo.portId) + "_" + mod;
          nvtxCounterAttr_t cntAttr = {};
          cntAttr.structSize = sizeof(nvtxCounterAttr_t);
          cntAttr.schemaId = schemaId;
          cntAttr.name = strdup(counterName.c_str());
          cntAttr.scopeId = NVTX_SCOPE_CURRENT_VM;
          uint64_t counter = nvtxCounterRegister(domain, &cntAttr);

          monitoredPorts.push_back({dev, d, p, portInfo.portId, domain, counter, prop.name, mod});
        }
      } else if (mod == "temperature") {
        nvtxPayloadSchemaEntry_t* entries = new nvtxPayloadSchemaEntry_t[1];
        entries[0] = {0, NVTX_PAYLOAD_ENTRY_TYPE_DOUBLE, "Temperature",
                      "", 0, 0, nullptr, nullptr};

        nvtxPayloadSchemaAttr_t schemaAttr;
        memset(&schemaAttr, 0, sizeof(schemaAttr));
        schemaAttr.fieldMask = NVTX_PAYLOAD_SCHEMA_ATTR_FIELD_TYPE |
                               NVTX_PAYLOAD_SCHEMA_ATTR_FIELD_ENTRIES |
                               NVTX_PAYLOAD_SCHEMA_ATTR_FIELD_NUM_ENTRIES |
                               NVTX_PAYLOAD_SCHEMA_ATTR_FIELD_STATIC_SIZE;
        schemaAttr.type = NVTX_PAYLOAD_SCHEMA_TYPE_STATIC;
        schemaAttr.entries = entries;
        schemaAttr.numEntries = 1;
        schemaAttr.payloadStaticSize = sizeof(double);

        const uint64_t schemaId = nvtxPayloadSchemaRegister(domain, &schemaAttr);
        std::string counterName = mod;
        nvtxCounterAttr_t cntAttr = {};
        cntAttr.structSize = sizeof(nvtxCounterAttr_t);
        cntAttr.schemaId = schemaId;
        cntAttr.name = strdup(counterName.c_str());
        cntAttr.scopeId = NVTX_SCOPE_CURRENT_VM;
        uint64_t counter = nvtxCounterRegister(domain, &cntAttr);

        monitoredPorts.push_back({dev, d, -1, -1, domain, counter, prop.name, mod});
      }
    }
  }

  if (monitoredPorts.empty()) {
    LOG_ERR("No items matched criteria.");
    return 1;
  }

  std::string moduleListStr = "";
  for (size_t i = 0; i < config.modules.size(); i++) {
    moduleListStr += config.modules[i];
    if (i < config.modules.size() - 1) moduleListStr += ", ";
  }

  printf("Monitoring %zu metrics across %zu devices. Modules: [%s], Interval: %d ms\n",
         monitoredPorts.size(), activeDevices.size(), moduleListStr.c_str(), config.intervalMs);
  printf("Press Ctrl+C to stop.\n");

  std::vector<double> values(4);
  int currentSleepMs = 2;
  int idleCount = 0;

  bool monitorThroughput = false;
  for (const auto& mod : config.modules) {
    if (mod == "throughput") monitorThroughput = true;
  }

  while (true) {
    if (monitorThroughput) {
      for (auto dev_h : activeDevices) h3ppciPerfStart(dev_h);
      usleep(currentSleepMs * 1000);
      for (auto dev_h : activeDevices) h3ppciPerfStop(dev_h);
    } else {
      usleep(config.intervalMs * 1000);
    }

    bool hasTraffic = false;
    double maxTrafficBps = 0.0;

    for (auto &mp : monitoredPorts) {
      if (mp.moduleName == "throughput") {
        h3ppciPerfCal cal;
        if (h3ppciPerfGetCal(mp.dev, mp.portIndex, &cal) == H3PPCI_SUCCESS) {
          values[0] = cal.rxBps / (1024.0 * 1024.0);
          values[1] = cal.txBps / (1024.0 * 1024.0);
          values[2] = cal.rxUtilization;
          values[3] = cal.txUtilization;
          nvtxCounterSample(mp.domain, mp.counter, values.data(), 4 * sizeof(double));

          if (values[0] > maxTrafficBps) maxTrafficBps = values[0];
          if (values[1] > maxTrafficBps) maxTrafficBps = values[1];
          if (values[0] > 100 || values[1] > 100) hasTraffic = true;
        }
      } else if (mp.moduleName == "error") {
        h3ppciPortErrors errs;
        if (h3ppciGetPortErrorCounters(mp.dev, mp.portIndex, &errs) == H3PPCI_SUCCESS) {
          values[0] = (double)errs.badTlp;
          values[1] = (double)errs.badDllp;
          values[2] = (double)errs.rxErrors;
          values[3] = (double)errs.recoveryDiagnostics;
          nvtxCounterSample(mp.domain, mp.counter, values.data(), 4 * sizeof(double));
        }
      } else if (mp.moduleName == "temperature") {
        double temp = 0.0;
        if (h3ppciGetTemperature(mp.dev, &temp) == H3PPCI_SUCCESS) {
          values[0] = temp;
          nvtxCounterSample(mp.domain, mp.counter, values.data(), sizeof(double));
        }
      }
    }

    if (monitorThroughput) {
      if (hasTraffic) {
        idleCount = 0;
        currentSleepMs = config.intervalMs;
      } else {
        idleCount++;
        if (idleCount >= 5) {
          currentSleepMs = 2;
          idleCount = 5;
        }
      }
    }

    static int iterations = 0;
    if (monitoredPorts.size() == 1) {
      printf("\rSampled 1 metric... [Iter: %d]          ", ++iterations);
    } else {
      printf("\rSampling %zu metrics... [Iter: %d]          ", monitoredPorts.size(), ++iterations);
    }
    fflush(stdout);
  }

  return 0;
}
