/**
 * @file
 *
 * @ingroup RTEMSClassicCgroup
 *
 * @brief Classic API Cgroup Manager
 *
 * This include file contains all the constants and structures associated
 * with the Classic API Cgroup Manager.
 */

/* COPYRIGHT (c) 1989-2013.
 * On-Line Applications Research Corporation (OAR).
 *
 * The license and distribution terms for this file may be
 * found in the file LICENSE in this distribution or at
 * http://www.rtems.org/license/LICENSE.
 */

#ifndef _RTEMS_RTEMS_CGROUP_H
#define _RTEMS_RTEMS_CGROUP_H

#include <rtems/rtems/attr.h>
#include <rtems/rtems/options.h>
#include <rtems/rtems/status.h>
#include <rtems/rtems/types.h>
#include <rtems/rtems/tasks.h>
#include <rtems/score/corecgroup.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 *  @defgroup ClassicCgroup Cgroups
 *
 * @ingroup RTEMSClassicAPI
 *
 * @brief The Cgroup Manager provides facilities to manage groups of tasks
 * for resource control and accounting purposes.
 *
 * @{
 */

/**
 * @brief Cgroup configuration structure for user applications.
 */
typedef struct {
  /**
   * @brief CPU shares for this cgroup (relative weight).
   */
  uint32_t cpu_shares;

  /**
   * @brief CPU quota in microseconds per period.
   */
  uint32_t cpu_quota;

  /**
   * @brief CPU period in microseconds.
   */
  uint32_t cpu_period;

  /**
   * @brief Memory limit in bytes.
   */
  size_t memory_limit;

  /**
   * @brief Block I/O limit in bytes per second.
   */
  size_t blkio_limit;

  /**
   * @brief Maximum number of tasks allowed in this cgroup.
   */
  uint32_t max_tasks;
} rtems_cgroup_config;

/**
 * @brief Cgroup states visible to user applications.
 */
typedef enum {
  RTEMS_CGROUP_READY = 0,
  RTEMS_CGROUP_RUNNING = 1,
  RTEMS_CGROUP_BLOCKED = 2
} rtems_cgroup_state;

/**
 * @brief Create a cgroup.
 *
 * This directive creates a cgroup with the specified configuration.
 *
 * @param[in] name The cgroup name.
 * @param[in] config The cgroup configuration.
 * @param[out] id The cgroup identifier.
 *
 * @retval RTEMS_SUCCESSFUL if successful or error code if unsuccessful and
 * *id filled with the message queue id
 */
rtems_status_code rtems_cgroup_create(
  rtems_name            name,
  rtems_id             *id,
  CORE_cgroup_config   *core_config
);

/**
 * @brief RTEMS Delete Cgroup
 *
 * This directive deletes the specified cgroup.
 *
 * @param[in] id The cgroup identifier.
 *
 * @retval RTEMS_SUCCESSFUL The cgroup was deleted successfully.
 * @retval RTEMS_INVALID_ID The cgroup identifier was invalid.
 * @retval RTEMS_RESOURCE_IN_USE The cgroup contains tasks.
 */
rtems_status_code rtems_cgroup_delete(
  rtems_id id
);

/**
 * @brief Add a task to a cgroup.
 *
 * This directive adds the specified task to the specified cgroup.
 *
 * @param[in] cgroup_id The cgroup identifier.
 * @param[in] task_id The task identifier.
 *
 * @retval RTEMS_SUCCESSFUL The task was added to the cgroup successfully.
 * @retval RTEMS_INVALID_ID The cgroup or task identifier was invalid.
 * @retval RTEMS_TOO_MANY The cgroup has reached its maximum task limit.
 * @retval RTEMS_NO_MEMORY There was insufficient memory.
 */
rtems_status_code rtems_cgroup_add_task(
  rtems_id cgroup_id,
  rtems_id task_id
);

/**
 * @brief Remove a task from a cgroup.
 *
 * This directive removes the specified task from the specified cgroup.
 *
 * @param[in] cgroup_id The cgroup identifier.
 * @param[in] task_id The task identifier.
 *
 * @retval RTEMS_SUCCESSFUL The task was removed from the cgroup successfully.
 * @retval RTEMS_INVALID_ID The cgroup or task identifier was invalid.
 */
rtems_status_code rtems_cgroup_remove_task(
  rtems_id cgroup_id,
  rtems_id task_id
);

/**
 * @brief Get cgroup state.
 *
 * This directive returns the current state of the specified cgroup.
 *
 * @param[in] id The cgroup identifier.
 * @param[out] state The cgroup state.
 *
 * @retval RTEMS_SUCCESSFUL The cgroup state was returned successfully.
 * @retval RTEMS_INVALID_ID The cgroup identifier was invalid.
 */
rtems_status_code rtems_cgroup_get_state(
  rtems_id             id,
  rtems_cgroup_state  *state
);

/**
 * @brief Get cgroup task count.
 *
 * This directive returns the number of tasks in the specified cgroup.
 *
 * @param[in] id The cgroup identifier.
 * @param[out] count The number of tasks.
 *
 * @retval RTEMS_SUCCESSFUL The task count was returned successfully.
 * @retval RTEMS_INVALID_ID The cgroup identifier was invalid.
 */
rtems_status_code rtems_cgroup_get_task_count(
  rtems_id  id,
  uint32_t *count
);

/** @} */

#ifdef __cplusplus
}
#endif

#endif
/* end of include file */
