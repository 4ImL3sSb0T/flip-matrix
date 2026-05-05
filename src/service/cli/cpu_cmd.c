#include "FreeRTOS.h"
#include "task.h"
#include "service/cli/shell.h"
#include "service/cli/log/log.h"

int cpu_usage(int argc, char *argv[])
{
  (void)argc;
  (void)argv;
  static char buf[512];
  vTaskGetRunTimeStats(buf);
  logPrintln("Task            Abs Time    %% Time");
  logPrintln("------------------------------------");
  logPrintln("%s", buf);
  return 0;
}
SHELL_EXPORT_CMD(
SHELL_CMD_PERMISSION(0)|SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN)|SHELL_CMD_DISABLE_RETURN,
cpu, cpu_usage, show task CPU usage);
