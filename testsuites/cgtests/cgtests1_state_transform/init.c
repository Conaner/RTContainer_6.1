#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#define CONFIGURE_INIT
#include "system.h"

#ifdef RTEMS_CGROUP
#include <rtems/score/corecgroupimpl.h>
#include <rtems/rtems/cgroupimpl.h>
#endif

const char rtems_test_name[] = "CGTESTS1 STATE TRANSFORM";

rtems_task Init(
  rtems_task_argument ignored
)
{
  rtems_print_printer_fprintf_putc(&rtems_test_printer);
  TEST_BEGIN();
  #ifdef RTEMS_CGROUP
    printf( "==================START CGTESTs1: STATE TRANSFORM=================\n" );

    // rtems_name cg_name = rtems_build_name( 'C', 'G', '1', ' ' );
    // rtems_id cg_id;
    // Cgroup_Control *cg_control;
    // Thread_queue_Context tq_ctx;
    // rtems_cgroup_create( cg_name, &cg_id );
    // cg_control = _Cgroup_Get( cg_id, &tq_ctx );
    // CORE_cgroup_Control the_cgroup = cg_control->cgroup;
    // if(cg_id <= 0) {
    //   printf( "==================CGROUP creation failed, id is %u!====================\n", cg_id);
    //   rtems_test_exit( 1 );
    // }
    // else {
    //   printf( "==================CGROUP created successfully, id is %u!====================\n", cg_id);
    // }
    // printf("===================== CG STATE IS %s=====================\n", _CORE_cgroup_Get_state(&the_cgroup));
    // if(strcmp(_CORE_cgroup_Get_state(&the_cgroup), "READY") != 0) {
    //   rtems_test_exit( 1 );
    // } 

    // printf("==================Transform STATE TO RUNNING====================\n");
    // _CORE_cgroup_Dispatch(&the_cgroup);
    // printf("===================== CG STATE IS %s=====================\n", _CORE_cgroup_Get_state(&the_cgroup));
    // if(strcmp(_CORE_cgroup_Get_state(&the_cgroup), "RUNNING") != 0) {
    //   rtems_test_exit( 1 );
    // }

    // printf("===================Transform STATE TO BLOCKED ====================\n");
    // _CORE_cgroup_Suspend(&the_cgroup);
    // printf("===================== CG STATE IS %s=====================\n", _CORE_cgroup_Get_state(&the_cgroup));
    // if(strcmp(_CORE_cgroup_Get_state(&the_cgroup), "BLOCKED") != 0) {
    //   rtems_test_exit( 1 );
    // }

    // printf("===================Transform STATE TO READY=======================\n");
    // _CORE_cgroup_Resume(&the_cgroup);
    // printf("===================== CG STATE IS %s=====================\n", _CORE_cgroup_Get_state(&the_cgroup));
    // if(strcmp(_CORE_cgroup_Get_state(&the_cgroup), "READY") != 0) {
    //   rtems_test_exit( 1 );
    // }
  #else
    printf( "==================CGROUP not enabled!====================\n");
  #endif
  TEST_END();
  rtems_test_exit( 0 );
}
