/**
 * @file
 *
 * @ingroup RTEMSClassicCgroup
 *
 * @brief Classic API Cgroup Data Structures
 *
 * This include file contains the data structures used by the
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

#ifndef _RTEMS_RTEMS_CGROUPDATA_H
#define _RTEMS_RTEMS_CGROUPDATA_H

#include <rtems/rtems/cgroup.h>
#include <rtems/score/object.h>
#include <rtems/score/corecgroup.h>
#include <rtems/score/objectdata.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @addtogroup ClassicCgroupImpl
 *
 * @{
 */

/**
 * @brief Classic API Cgroup control block.
 *
 * This structure contains the information necessary to manage
 * a cgroup within the Classic API.
 */
typedef struct {
  /**
   * @brief Object control for this cgroup.
   */
  Objects_Control Object;

  /**
   * @brief Core cgroup control structure.
   */
  CORE_cgroup_Control cgroup;
} Cgroup_Control;

/**
 * @brief The Classic Cgroup objects information.
 */
extern Objects_Information _Cgroup_Information;

/**
 * @brief Macro to define the objects information for cgroups.
 */
#define CGROUP_INFORMATION_DEFINE( max ) \
  OBJECTS_INFORMATION_DEFINE( \
    _Cgroup, \
    OBJECTS_CLASSIC_API, \
    OBJECTS_RTEMS_CGROUPS, \
    Cgroup_Control, \
    max, \
    OBJECTS_NO_STRING_NAME, \
    NULL \
  )

/** @} */

#ifdef __cplusplus
}
#endif

#endif
/* end of include file */
