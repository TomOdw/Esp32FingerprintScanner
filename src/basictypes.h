/**
 * @file       basictypes.h
 * @brief      //TODO
 *
 * @author     Tom Christ
 * @copyright  Copyright (c) 2026 Tom Christ; MIT License
 * @date       2026-06-0a7
 *
 * @version    0.1  Initial Version
 */
#ifndef BASICTYPES_H_
#define BASICTYPES_H_

/******************************************************************************/
/*** Include files                                                            */
/******************************************************************************/
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/******************************************************************************/
/*** Types                                                                    */
/******************************************************************************/

/**
 * @brief //TODO
 */
typedef enum{
  RC_SUCCESS = 0,
  RC_ERROR,
  RC_INVALID_ARG,
  RC_ALREADY_INITIALIZED,
  RC_NOT_INITIALIZED,
  RC_BUFFER_FULL,
  RC_BUFFER_EMPTY,
  RC_NOT_CONNECTED,
  RC_TIMEOUT,
  RC_PROTOCOL_ERROR,
}RC_t;

#endif /* BASICTYPES_H_ */
