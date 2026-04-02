/**
 * @file
 *
 * @ingroup RTEMSClassicCgroup
 *
 * @brief RTEMS Get Cgroup Task Count
 */

/*
 *  COPYRIGHT (c) 1989-2009.
 *  On-Line Applications Research Corporation (OAR).
 *
 *  The license and distribution terms for this file may be
 *  found in the file LICENSE in this distribution or at
 *  http://www.rtems.org/license/LICENSE.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <rtems/rtems/cgroupimpl.h>

rtems_status_code rtems_cgroup_get_task_count(
  rtems_id  id,
  uint32_t *count
)
{
  Cgroup_Control    *the_cgroup;
  ISR_lock_Context   lock_context;

  if ( !count ) {
    return RTEMS_INVALID_ADDRESS;
  }

  the_cgroup = _Cgroup_Get( id, &lock_context );
  _ISR_lock_ISR_enable( &lock_context );

  if (the_cgroup == NULL) {
    return RTEMS_INVALID_ID;
  }

  *count = the_cgroup->cgroup.thread_count;

  return RTEMS_SUCCESSFUL;
}
