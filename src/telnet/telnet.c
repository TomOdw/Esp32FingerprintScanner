/**
 * @file       telnet.c
 * @brief      Telnet server (single session)
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
#include "telnet/telnet.h"

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "app_handles.h"
#include "wdt/wdt.h"

/******************************************************************************/
/*** Local variables                                                          */
/******************************************************************************/

static int s_session_fd = -1;

/******************************************************************************/
/*** API function implementation                                              */
/******************************************************************************/

RC_t telnet_Write(const char *i_buf, size_t i_len)
{
  if (s_session_fd < 0 || i_buf == NULL || i_len == 0)
  {
    return RC_ERROR;
  }
  (void)i_len;
  return RC_SUCCESS;
}

void telnet_Task(void *pvParam)
{
  (void)pvParam;
  wdt_RegisterTask();

  for (;;)
  {
    wdt_Reset();
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}
