#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <rtems/rtems/cgroupdata.h>

OBJECTS_INFORMATION_DEFINE_ZERO(
  _Cgroup,
  OBJECTS_CLASSIC_API,
  OBJECTS_RTEMS_CGROUPS,
  OBJECTS_NO_STRING_NAME
);
