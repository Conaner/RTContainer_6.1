/**
 * @file
 *
 * @ingroup ClassicCgroupImpl
 *
 * This include file contains the implementation header for the
 * Classic API Cgroup Manager.
 */

/*
 *  COPYRIGHT (c) 1989-2009.
 *  On-Line Applications Research Corporation (OAR).
 *
 *  The license and distribution terms for this file may be
 *  found in the file LICENSE in this distribution or at
 *  http://www.rtems.org/license/LICENSE.
 */

#ifndef _RTEMS_RTEMS_CGROUPIMPL_H
#define _RTEMS_RTEMS_CGROUPIMPL_H

#include <rtems/rtems/cgroupdata.h>
#include <rtems/score/objectimpl.h>
#include <rtems/score/corecgroupimpl.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 *  @defgroup ClassicCgroupImpl Classic Cgroup Implementation
 *
 *  @ingroup RTEMSInternalClassic
 *
 *  @{
 */

/**
 * @brief Cgroup control types.
 */
typedef enum {
  CPU_SHARES = 0,
  CPU_QUOTA = 1
}  Cgroup_Control_types;

/**
 * @brief Frees a cgroup control block to the inactive chain
 * of free cgroup control blocks.
 *
 * @param[in] the_cgroup The cgroup control block to free.
 */
static inline void _Cgroup_Free(
  Cgroup_Control *the_cgroup
)
{
  _Objects_Free( &_Cgroup_Information, &the_cgroup->Object );
}

/**
 * @brief Maps cgroup IDs to cgroup control blocks.
 *
 * @param[in] id The cgroup ID.
 * @param[out] lock_context The lock context.
 *
 * @retval object The object associated with the ID.
 * @retval NULL No object associated with this ID.
 */
static inline Cgroup_Control *_Cgroup_Get(
  Objects_Id            id,
  ISR_lock_Context    *lock_context
)
{
  return (Cgroup_Control *) _Objects_Get(
    id,
    lock_context,
    &_Cgroup_Information
  );
}

static inline Cgroup_Control *_Cgroup_Allocate( void )
{
  return (Cgroup_Control *)
    _Objects_Allocate( &_Cgroup_Information );
}

/**@}*/

#ifdef __cplusplus
}
#endif

#endif
/* end of include file */
