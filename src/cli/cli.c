/**
 * @file       cli.c
 * @brief      Command-line interface engine
 *
 * @author     Tom Christ
 * @copyright  Copyright (c) 2026 Tom Christ; MIT License
 * @date       2026-06-07
 *
 * @version    0.1  Initial Version
 */
/******************************************************************************/
/*** Include files                                                            */
/******************************************************************************/
#include "cli/cli.h"

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "app_handles.h"
#include "wdt/wdt.h"
#include "telnet/telnet.h"

/******************************************************************************/
/*** Local variables                                                          */
/******************************************************************************/

static cli_io_t s_broadcast_io;

/******************************************************************************/
/*** Local function declaration                                               */
/******************************************************************************/

static void broadcast_write(const char *buf, size_t len, void *ctx);

/******************************************************************************/
/*** API function implementation                                              */
/******************************************************************************/

RC_t cli_Init(void)
{
  s_broadcast_io.write = broadcast_write;
  s_broadcast_io.ctx   = NULL;
  return RC_SUCCESS;
}

void cli_EngineRun(const char *i_line, const cli_io_t *i_io)
{
  (void)i_line;
  (void)i_io;
}

void cli_Task(void *pvParam)
{
  (void)pvParam;
  wdt_RegisterTask();

  char line[CLI_LINE_BUF_LEN];
  size_t pos = 0;

  for (;;)
  {
    uint8_t ch;
    if (xQueueReceive(g_cli_input_queue, &ch, pdMS_TO_TICKS(5000)) == pdTRUE)
    {
      if (ch == '\n' || ch == '\r')
      {
        if (pos > 0)
        {
          line[pos] = '\0';
          cli_EngineRun(line, &s_broadcast_io);
          pos = 0;
        }
      }
      else if (pos < CLI_LINE_BUF_LEN - 1U)
      {
        line[pos++] = (char)ch;
      }
    }
    wdt_Reset();
  }
}

/******************************************************************************/
/*** Local function implementation                                            */
/******************************************************************************/

static void broadcast_write(const char *buf, size_t len, void *ctx)
{
  (void)ctx;
  fwrite(buf, 1, len, stdout);
  telnet_Write(buf, len);
}
