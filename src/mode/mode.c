/**
 * @file       mode.c
 * @brief      Mode-control: Normal-Mode vs Setup-Mode boot decision
 *
 * @author     Tom Christ
 * @copyright  Copyright (c) 2026 Tom Christ; MIT License
 * @date       2026-07-25
 *
 * @version    0.1  Initial Version
 */
/******************************************************************************/
/*** Include files                                                            */
/******************************************************************************/
#include "mode/mode.h"

#include "esp_log.h"

#include "nvs/nvs_app.h"

/******************************************************************************/
/*** Local variables                                                          */
/******************************************************************************/

static const char *TAG = "mode";

/******************************************************************************/
/*** API function implementation                                             */
/******************************************************************************/

app_mode_t app_mode_Decide(void)
{
  bool setup_flag = false;
  nvs_GeneralGetSetupFlag(&setup_flag);

  char ssid[NVS_SSID_MAX_LEN] = {0};
  bool have_ssid = (nvs_WifiGetSsid(ssid, sizeof(ssid)) == RC_SUCCESS) && (ssid[0] != '\0');

  char broker[NVS_BROKER_MAX_LEN] = {0};
  bool have_broker = (nvs_MqttGetBroker(broker, sizeof(broker)) == RC_SUCCESS) && (broker[0] != '\0');

  if (setup_flag || !have_ssid || !have_broker)
  {
    ESP_LOGI(TAG, "Setup-Mode (setup_flag=%d, have_ssid=%d, have_broker=%d)",
             (int)setup_flag, (int)have_ssid, (int)have_broker);
    return APP_MODE_SETUP;
  }

  ESP_LOGI(TAG, "Normal-Mode");
  return APP_MODE_NORMAL;
}

void app_mode_EnterSetup(void)
{
  nvs_GeneralSetSetupFlag(false);
}

bool app_mode_HasAdminFingerprint(void)
{
  bool enrolled = false;
  nvs_GeneralGetAdminEnrolled(&enrolled);
  return enrolled;
}
