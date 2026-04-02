
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#define CONFIGURE_INIT
#include "system.h"

#ifdef RTEMS_CGROUP
#include <rtems/score/corecgroupimpl.h>
#include <rtems/score/schedulerimpl.h>
#include <rtems/score/threadimpl.h>
#include <rtems/score/timestampimpl.h>

#include <rtems/rtems/cgroupimpl.h>
#include <rtems/rtems/support.h>
#include <rtems/rtems/tasks.h>
#include <rtems/rtems/timer.h>

CORE_cgroup_Control* core_cgroup;

const char rtems_test_name[] = "CGTEST3 THREAD SUSPEND & RESUME";

static rtems_task worker(rtems_task_argument arg) {
  puts("worker: suspend itself");
  // rtems_task_suspend(RTEMS_SELF);
  _CORE_cgroup_Suspend( core_cgroup, STATES_WAITING_FOR_CGROUP_CPU_QUOTA);
  puts("worker: resumed");
  _CORE_cgroup_Suspend( core_cgroup, STATES_WAITING_FOR_CGROUP_CPU_QUOTA);
  rtems_test_exit(0);
}

static rtems_task controller(rtems_task_argument arg) {
  rtems_status_code sc;
  /* ensure worker has been suspended */
  sc = rtems_task_is_suspended(Task_id[1]);

  // rtems_test_assert(sc == RTEMS_SUCCESSFUL);

  puts("controller: resume worker");
  // sc = rtems_task_resume(Task_id[1]);
  _CORE_cgroup_Resume( core_cgroup, STATES_WAITING_FOR_CGROUP_CPU_QUOTA);
  // rtems_test_assert(sc == RTEMS_SUCCESSFUL);

  puts("controller gets cpu again\n");
  rtems_task_wake_after(RTEMS_YIELD_PROCESSOR);
  rtems_test_exit(0);
}

rtems_task Init(rtems_task_argument ignored) {
  rtems_print_printer_fprintf_putc(&rtems_test_printer);
  TEST_BEGIN();

  printf("==================START CGTEST3: CGROUP SUSPEND & RESUME=================\n");

  CORE_cgroup_config core_config = {
    .cpu_shares = 1,
    .cpu_quota = _Watchdog_Ticks_per_second * 100,
    .cpu_period = _Watchdog_Ticks_per_second * 100,
    .memory_limit = 0,
    .blkio_limit = 0
  };

  rtems_id cg_id;

  rtems_cgroup_create(
    rtems_build_name( 'C', 'G', 'T', 'S' ),
    &cg_id,
    &core_config
  );

  Cgroup_Control* cgroup;
  ISR_lock_Context lock_context;
  cgroup = _Cgroup_Get(cg_id, &lock_context);
  _ISR_lock_ISR_enable(&lock_context);
  core_cgroup = &cgroup->cgroup;

  rtems_task_create(rtems_build_name('W','O','R','K'), 1,
    RTEMS_MINIMUM_STACK_SIZE, RTEMS_DEFAULT_MODES,
    RTEMS_DEFAULT_ATTRIBUTES, &Task_id[1]);
  rtems_task_start(Task_id[1], worker, 0);

  rtems_cgroup_add_task(cg_id, Task_id[1]);

  rtems_task_create(rtems_build_name('C','T','R','L'), 2,
    RTEMS_MINIMUM_STACK_SIZE, RTEMS_DEFAULT_MODES,
    RTEMS_DEFAULT_ATTRIBUTES, &Task_id[2]);
  rtems_task_start(Task_id[2], controller, 0);

  rtems_task_wake_after(RTEMS_YIELD_PROCESSOR);

  rtems_task_wake_after(RTEMS_YIELD_PROCESSOR);

  puts("===============END CGTEST3: CGROUP SUSPEND & RESUME================");

  TEST_END();
  rtems_test_exit(0);
}
#endif /* RTEMS_CGROUP */