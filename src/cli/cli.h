/**
 * @file       cli.h
 * @brief      Command-line interface engine
 *
 *             The CLI engine is a stateless function (cli_engine_run()) called
 *             by cli_task. Both UART and Telnet feed bytes into g_cli_input_queue;
 *             cli_task assembles them into lines and calls cli_engine_run().
 *             Output is broadcast to both UART and the active Telnet session
 *             via the cli_io_t output handle.
 *
 *             Supported command categories:
 *               - wifi <ssid> <password>  — configure WiFi credentials
 *               - mqtt broker|topic|user|pass|client_id <value>
 *               - mqtt heartbeat on|off [interval <s>] [topic <t>]
 *               - fp enroll <uuid> [name]  — add fingerprint (2-scan process)
 *               - fp list                  — list stored fingerprints
 *               - fp delete <id>           — delete by slot ID
 *               - fp delete-uuid <uuid>    — delete all slots for a UUID
 *               - fp delete-all            — wipe all fingerprints
 *               - user set <uuid> <name>   — set/update display name
 *               - user delete <uuid>       — remove display name
 *               - info                     — show system status
 *               - reboot                   — reboot device
 *
 * @author     Tom Christ
 * @copyright  Copyright (c) 2026 Tom Christ; MIT License
 * @date       2026-06-07
 *
 * @version    0.1  Initial Version
 */
#ifndef CLI_H_
#define CLI_H_

/******************************************************************************/
/*** Include files                                                            */
/******************************************************************************/
#include <stddef.h>
#include "basictypes.h"

/******************************************************************************/
/*** Defines                                                                  */
/******************************************************************************/

/** Maximum line length accepted by the CLI engine (including NUL) */
#define CLI_LINE_BUF_LEN 128U

/******************************************************************************/
/*** Types                                                                    */
/******************************************************************************/

/**
 * @brief Output handle passed to cli_engine_run().
 *
 * The write function broadcasts to one or more physical outputs (UART, Telnet).
 * @p ctx is an opaque pointer passed back to write unmodified.
 */
typedef struct
{
  void (*write)(const char *buf, size_t len, void *ctx);
  void *ctx;
} cli_io_t;

/******************************************************************************/
/*** API Functions                                                            */
/******************************************************************************/

/**
 * @brief Initialize the CLI engine.
 *
 * Called once from app_main before cli_task is created.
 *
 * @return RC_SUCCESS or RC_ERROR.
 */
RC_t cli_Init(void);

/**
 * @brief Process one input line.
 *
 * Parses @p i_line, executes the matching command, and writes output via @p i_io.
 * Re-entrant with respect to the output handle; do not call concurrently with
 * the same handle.
 *
 * @param[in] i_line  Null-terminated input line (without trailing newline).
 * @param[in] i_io    Output handle used for command responses.
 */
void cli_EngineRun(const char *i_line, const cli_io_t *i_io);

/**
 * @brief CLI input/output task.
 *
 * Reads bytes from g_cli_input_queue, assembles lines, and calls
 * cli_EngineRun() with a broadcast output handle (UART + active Telnet).
 * Intended to run on Core 0 at priority 3.
 *
 * @param pvParam  Unused; pass NULL when creating the task.
 */
void cli_Task(void *pvParam);

#endif /* CLI_H_ */
