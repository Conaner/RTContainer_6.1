
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#define CONFIGURE_INIT
#include "system.h"

#include <rtems/score/corecgroupimpl.h>
#include <rtems/score/schedulerimpl.h>
#include <rtems/score/threadimpl.h>
#include <rtems/score/timestampimpl.h>

#include <rtems/rtems/cgroupimpl.h>
#include <rtems/rtems/support.h>
#include <rtems/rtems/tasks.h>
#include <rtems/rtems/timer.h>

const char rtems_test_name[] = "ORGTEST2 THREAD SUSPEND & RESUME";

static rtems_task worker(rtems_task_argument arg) {
  ISR_lock_Context lock_context;
  Thread_Control* the_thread = _Thread_Get( Task_id[1], &lock_context );
  _ISR_lock_ISR_enable(&lock_context);
  puts("worker: suspend itself");
  // rtems_task_suspend(RTEMS_SELF);
  Per_CPU_Control  *cpu_self  = _Thread_Dispatch_disable_critical(&lock_context);  
    
  _ISR_lock_ISR_enable(&lock_context);
  _Thread_Set_state(the_thread, STATES_WAITING_FOR_CGROUP_CPU_QUOTA);
  _Thread_Dispatch_enable(cpu_self);
  puts("worker: resumed");
  rtems_test_exit(0);
}

static rtems_task controller(rtems_task_argument arg) {
  rtems_status_code sc;

  ISR_lock_Context lock_context;
  Thread_Control* the_thread = _Thread_Get( Task_id[1], &lock_context );
  _ISR_lock_ISR_enable(&lock_context);

  /* ensure worker has been suspended */
  sc = rtems_task_is_suspended(Task_id[1]);
  // _Thread_Set_state(the_thread, STATES_WAITING_FOR_CGROUP_CPU_QUOTA);
  // rtems_test_assert(sc == RTEMS_SUCCESSFUL);

  puts("controller: resume worker");
  // sc = rtems_task_resume(Task_id[1]);
  Per_CPU_Control  *cpu_self  = _Thread_Dispatch_disable_critical(&lock_context);  
    
  _ISR_lock_ISR_enable(&lock_context);  

  _Thread_Clear_state(the_thread, STATES_WAITING_FOR_CGROUP_CPU_QUOTA);
  _Thread_Dispatch_enable(cpu_self);

  // rtems_test_assert(sc == RTEMS_SUCCESSFUL);

  rtems_test_exit(0);
}

rtems_task Init(rtems_task_argument ignored) {
  rtems_print_printer_fprintf_putc(&rtems_test_printer);
  TEST_BEGIN();

  printf("==================START ORGTEST2: THREAD SUSPEND & RESUME=================\n");

  rtems_task_create(rtems_build_name('W','O','R','K'), 1,
    RTEMS_MINIMUM_STACK_SIZE, RTEMS_DEFAULT_MODES,
    RTEMS_DEFAULT_ATTRIBUTES, &Task_id[1]);
  rtems_task_start(Task_id[1], worker, 0);

  rtems_task_create(rtems_build_name('C','T','R','L'), 2,
    RTEMS_MINIMUM_STACK_SIZE, RTEMS_DEFAULT_MODES,
    RTEMS_DEFAULT_ATTRIBUTES, &Task_id[2]);
  rtems_task_start(Task_id[2], controller, 0);

  rtems_task_wake_after(RTEMS_YIELD_PROCESSOR);

  puts("===============END ORGTEST2: THREAD SUSPEND & RESUME================");

  TEST_END();
  rtems_test_exit(0);
}
