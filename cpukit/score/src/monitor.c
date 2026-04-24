#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <rtems/score/monitor.h>

#include <rtems/cpuuse.h>
#include <rtems/libcsupport.h>
#include <rtems/print.h>
#include <rtems/printer.h>
#include <rtems/rtems/clock.h>

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#ifdef RTEMSCFG_MONITOR_NET
#include <net/if_var.h>
#ifdef RTEMSCFG_NET_CONTAINER
#include <rtems/score/netContainer.h>
#else
extern struct ifnet *ifnet __attribute__((weak));
#endif
#endif

typedef struct {
#ifdef RTEMSCFG_MONITOR_CPU
  RtemsCpuStats cpu_stats;
#endif
#ifdef RTEMSCFG_MONITOR_MEM
  Heap_Information_block mem_info;
#endif
#ifdef RTEMSCFG_MONITOR_NET
  uint64_t rx_bytes;
  uint64_t tx_bytes;
  uint64_t rx_packets;
  uint64_t tx_packets;
  uint64_t rx_errors;
  uint64_t tx_errors;
#endif
} monitor_state;

static monitor_state g_monitor_state;

static size_t monitor_format_bytes(char *buf, size_t bufsz, uint64_t value)
{
  if (value < 1024ULL) {
    return (size_t) snprintf(buf, bufsz, "%" PRIu64 "B", value);
  }

  if (value < 1024ULL * 1024ULL) {
    return (size_t) snprintf(buf, bufsz, "%" PRIu64 "K", value / 1024ULL);
  }

  return (size_t) snprintf(buf, bufsz, "%" PRIu64 "M", value / (1024ULL * 1024ULL));
}

#ifdef RTEMSCFG_MONITOR_NET
static struct ifnet *monitor_get_ifnet_head(void)
{
#ifdef RTEMSCFG_NET_CONTAINER
  return rtems_net_container_get_ifnet();
#else
  return ifnet;
#endif
}
#endif

void rtems_monitor_initialize(void)
{
  memset(&g_monitor_state, 0, sizeof(g_monitor_state));
}

void rtems_monitor_sample(void)
{
#ifdef RTEMSCFG_MONITOR_CPU
  uint32_t ticks_per_second;
  uint32_t current_ticks;

  ticks_per_second = rtems_clock_get_ticks_per_second();
  current_ticks = rtems_clock_get_ticks_since_boot();
  g_monitor_state.cpu_stats.total_seconds = current_ticks / ticks_per_second;
  g_monitor_state.cpu_stats.total_microseconds =
    ((current_ticks % ticks_per_second) * 1000000U) / ticks_per_second;
  g_monitor_state.cpu_stats.uptime_seconds = g_monitor_state.cpu_stats.total_seconds;
  g_monitor_state.cpu_stats.uptime_microseconds =
    g_monitor_state.cpu_stats.total_microseconds;
#endif

#ifdef RTEMSCFG_MONITOR_MEM
  if (malloc_info(&g_monitor_state.mem_info) != 0) {
    memset(&g_monitor_state.mem_info, 0, sizeof(g_monitor_state.mem_info));
  }
#endif

#ifdef RTEMSCFG_MONITOR_NET
  struct ifnet *ifp;
  uint64_t total_rx_bytes;
  uint64_t total_tx_bytes;
  uint64_t total_rx_packets;
  uint64_t total_tx_packets;
  uint64_t total_rx_errors;
  uint64_t total_tx_errors;

  total_rx_bytes = 0;
  total_tx_bytes = 0;
  total_rx_packets = 0;
  total_tx_packets = 0;
  total_rx_errors = 0;
  total_tx_errors = 0;

  for (ifp = monitor_get_ifnet_head(); ifp != NULL; ifp = ifp->if_next) {
    total_rx_bytes += ifp->if_ibytes;
    total_tx_bytes += ifp->if_obytes;
    total_rx_packets += ifp->if_ipackets;
    total_tx_packets += ifp->if_opackets;
    total_rx_errors += ifp->if_ierrors;
    total_tx_errors += ifp->if_oerrors;
  }

  g_monitor_state.rx_bytes = total_rx_bytes;
  g_monitor_state.tx_bytes = total_tx_bytes;
  g_monitor_state.rx_packets = total_rx_packets;
  g_monitor_state.tx_packets = total_tx_packets;
  g_monitor_state.rx_errors = total_rx_errors;
  g_monitor_state.tx_errors = total_tx_errors;
#endif
}

void rtems_monitor_get_snapshot(RtemsMonitor *out)
{
  if (out == NULL) {
    return;
  }

  memset(out, 0, sizeof(*out));

#ifdef RTEMSCFG_MONITOR_CPU
  out->cpu = g_monitor_state.cpu_stats;
#endif
#ifdef RTEMSCFG_MONITOR_MEM
  out->mem.bytes_total =
    (uint64_t) (g_monitor_state.mem_info.Free.total + g_monitor_state.mem_info.Used.total);
  out->mem.bytes_used = (uint64_t) g_monitor_state.mem_info.Used.total;
  out->mem.bytes_free = (uint64_t) g_monitor_state.mem_info.Free.total;
  out->mem.bytes_high_water = (uint64_t) g_monitor_state.mem_info.Free.largest;
#endif
#ifdef RTEMSCFG_MONITOR_NET
  out->net.rx_bytes = g_monitor_state.rx_bytes;
  out->net.tx_bytes = g_monitor_state.tx_bytes;
  out->net.rx_packets = g_monitor_state.rx_packets;
  out->net.tx_packets = g_monitor_state.tx_packets;
  out->net.rx_errors = g_monitor_state.rx_errors;
  out->net.tx_errors = g_monitor_state.tx_errors;
#endif
}

#ifdef RTEMSCFG_MONITOR_CPU
void rtems_monitor_get_cpu(RtemsCpuStats *out)
{
  if (out != NULL) {
    *out = g_monitor_state.cpu_stats;
  }
}
#endif

#ifdef RTEMSCFG_MONITOR_MEM
void rtems_monitor_get_mem(RtemsMemStats *out)
{
  if (out == NULL) {
    return;
  }

  out->bytes_total =
    (uint64_t) (g_monitor_state.mem_info.Free.total + g_monitor_state.mem_info.Used.total);
  out->bytes_used = (uint64_t) g_monitor_state.mem_info.Used.total;
  out->bytes_free = (uint64_t) g_monitor_state.mem_info.Free.total;
  out->bytes_high_water = (uint64_t) g_monitor_state.mem_info.Free.largest;
}
#endif

#ifdef RTEMSCFG_MONITOR_NET
void rtems_monitor_get_net(RtemsNetStats *out)
{
  if (out == NULL) {
    return;
  }

  out->rx_bytes = g_monitor_state.rx_bytes;
  out->tx_bytes = g_monitor_state.tx_bytes;
  out->rx_packets = g_monitor_state.rx_packets;
  out->tx_packets = g_monitor_state.tx_packets;
  out->rx_errors = g_monitor_state.rx_errors;
  out->tx_errors = g_monitor_state.tx_errors;
}
#endif

void rtems_monitor_print_line(void)
{
  char line[256];
  size_t n;

  n = 0;
  memset(line, 0, sizeof(line));

#ifdef RTEMSCFG_MONITOR_CPU
  n += (size_t) snprintf(
    line + n,
    sizeof(line) - n,
    "CPU: uptime=%" PRIu32 ".%06" PRIu32 "s ",
    g_monitor_state.cpu_stats.uptime_seconds,
    g_monitor_state.cpu_stats.uptime_microseconds
  );
#endif

#ifdef RTEMSCFG_MONITOR_MEM
  {
    char used[24];
    char total[24];

    monitor_format_bytes(used, sizeof(used), (uint64_t) g_monitor_state.mem_info.Used.total);
    monitor_format_bytes(
      total,
      sizeof(total),
      (uint64_t) (g_monitor_state.mem_info.Free.total + g_monitor_state.mem_info.Used.total)
    );
    n += (size_t) snprintf(line + n, sizeof(line) - n, "MEM:%s/%s ", used, total);
  }
#endif

#ifdef RTEMSCFG_MONITOR_NET
  {
    char rx[24];
    char tx[24];

    monitor_format_bytes(rx, sizeof(rx), g_monitor_state.rx_bytes);
    monitor_format_bytes(tx, sizeof(tx), g_monitor_state.tx_bytes);
    n += (size_t) snprintf(line + n, sizeof(line) - n, "NET:rx=%s tx=%s", rx, tx);
  }
#endif

  if (n > 0 && line[n - 1] == ' ') {
    line[n - 1] = '\0';
  }

  if (n == 0) {
    printf("MON: (no modules enabled)\n");
  } else {
    printf("%s\n", line);
  }
}

void rtems_monitor_print_detailed_report(const rtems_printer *printer)
{
  if (printer == NULL) {
    return;
  }

#ifdef RTEMSCFG_MONITOR_CPU
  rtems_cpu_usage_report_with_plugin(printer);
#endif

#ifdef RTEMSCFG_MONITOR_MEM
  rtems_printf(printer, "\n");
  rtems_printf(printer, "-------------------------------------------------------------------------------\n");
  rtems_printf(printer, "                              MEMORY USAGE\n");
  rtems_printf(printer, "-------------------------------------------------------------------------------\n");
  rtems_printf(
    printer,
    " Total Memory:     %" PRIu64 " bytes\n",
    (uint64_t) (g_monitor_state.mem_info.Free.total + g_monitor_state.mem_info.Used.total)
  );
  rtems_printf(printer, " Used Memory:      %" PRIu64 " bytes\n", (uint64_t) g_monitor_state.mem_info.Used.total);
  rtems_printf(printer, " Free Memory:      %" PRIu64 " bytes\n", (uint64_t) g_monitor_state.mem_info.Free.total);
  rtems_printf(printer, " Largest Free:     %" PRIu64 " bytes\n", (uint64_t) g_monitor_state.mem_info.Free.largest);
  rtems_printf(printer, "-------------------------------------------------------------------------------\n");
#endif

#ifdef RTEMSCFG_MONITOR_NET
  rtems_printf(printer, "\n");
  rtems_printf(printer, "-------------------------------------------------------------------------------\n");
  rtems_printf(printer, "                           NETWORK STATISTICS\n");
  rtems_printf(printer, "-------------------------------------------------------------------------------\n");
  rtems_printf(printer, " RX Bytes:         %" PRIu64 "\n", g_monitor_state.rx_bytes);
  rtems_printf(printer, " TX Bytes:         %" PRIu64 "\n", g_monitor_state.tx_bytes);
  rtems_printf(printer, " RX Packets:       %" PRIu64 "\n", g_monitor_state.rx_packets);
  rtems_printf(printer, " TX Packets:       %" PRIu64 "\n", g_monitor_state.tx_packets);
  rtems_printf(printer, " RX Errors:        %" PRIu64 "\n", g_monitor_state.rx_errors);
  rtems_printf(printer, " TX Errors:        %" PRIu64 "\n", g_monitor_state.tx_errors);
  rtems_printf(printer, "-------------------------------------------------------------------------------\n");
#endif
}
