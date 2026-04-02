
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#define CONFIGURE_INIT
#include "system.h"

#ifdef RTEMS_CGROUP
#include <rtems/test.h>
#include <rtems/score/chainimpl.h>
#include <rtems/score/corecgroup.h>
#include <rtems/score/corecgroupimpl.h>
#include <rtems/score/isrlock.h>
#include <rtems/score/statesimpl.h>
#include <rtems/rtems/status.h>
#include <string.h>

#include <rtems/score/watchdogimpl.h>
#include <rtems/score/thread.h>
#include <rtems/score/threadimpl.h>

#include <rtems/rtems/cgroupimpl.h>
#include <rtems/rtems/support.h>
#include <rtems/rtems/tasks.h>
#include <rtems/rtems/timer.h>
#include <rtems/assoc.h>

#include <inttypes.h>

const char rtems_test_name[] = "CGTEST4 CGROUP CPU QUOTA";

#define CGROUP_WAIT_STATE STATES_WAITING_FOR_CGROUP_CPU_QUOTA

static CORE_cgroup_Control*  test_cgroup;

static CORE_cgroup_config  test_core_config = {
  .cpu_shares = 1,
  .cpu_quota = 1000,
  .cpu_period = 0,
  .memory_limit = 0,
  .blkio_limit = 0
};

static rtems_task worker_task(rtems_task_argument arg)
{
  printf("Worker task %lu started\n", arg);
  printf("---- cgroup has %" PRIu64 " cpu quota available ----\n", test_cgroup->cpu_quota_available);
  (void) arg;
  rtems_test_busy_cpu_usage( arg, 1e5 );
  /** Again check thread states */
  for(int i=0; i<5; i++) {
    ISR_lock_Context lock_context;
    Thread_Control* thread;
    thread = _Thread_Get(Task_id[i], &lock_context);
    if (thread != NULL) {
      _ISR_lock_ISR_enable(&lock_context);
      char buf[16];
      rtems_assoc_thread_states_to_string(thread->current_state, buf, sizeof(buf));
      printf(" <====== Task %p state is %s ======>\n", thread, buf);
    }
  }
  printf("Worker task %lu finished\n", arg);
  rtems_task_exit();
}

static void create_test_tasks()
{
  rtems_status_code status;
  rtems_task_priority priority = 1;
  size_t stack_size = RTEMS_MINIMUM_STACK_SIZE;

  for(int i = 0; i < 5; i++) {
    status = rtems_task_create(
        rtems_build_name( 'T', 'E', 'S', 'T' ),
        9-i,
        RTEMS_MINIMUM_STACK_SIZE,
        RTEMS_DEFAULT_MODES,
        RTEMS_DEFAULT_ATTRIBUTES,
        &Task_id[i]
    );
    rtems_test_assert(status == RTEMS_SUCCESSFUL);

    status = rtems_task_start(Task_id[i], worker_task, i);
    rtems_test_assert(status == RTEMS_SUCCESSFUL);
  }
}


static void verify_quota_consumption(void)
{
  /** validate thread state */
  for(int i=0; i<5; i++) {
    ISR_lock_Context lock_context;
    Thread_Control* thread;
    thread = _Thread_Get(Task_id[i], &lock_context);
    if (thread != NULL) {  
      _ISR_lock_ISR_enable(&lock_context);        
      char buf[16];
      rtems_assoc_thread_states_to_string(thread->current_state, buf, sizeof(buf));
      printf(" <====== Task %u state is %s ======>\n", Task_id[i], buf);
    }
  }

  printf("---- before first consume, cgroup has %" PRIu64 " cpu quota available ----\n", test_cgroup->cpu_quota_available);

  _CORE_cgroup_Consume_cpu_quota(test_cgroup, test_core_config.cpu_quota - 1);
  rtems_test_assert(test_cgroup->cpu_quota_available == 1);

  /** validate thread state */
  for(int i=0; i<5; i++) {
    ISR_lock_Context lock_context;
    Thread_Control* thread;
    thread = _Thread_Get(Task_id[i], &lock_context);
    if (thread != NULL) {  
      _ISR_lock_ISR_enable(&lock_context);        
      char buf[16];
      rtems_assoc_thread_states_to_string(thread->current_state, buf, sizeof(buf));
      printf(" <====== Task %u state is %s ======>\n", Task_id[i], buf);
    }
  }

  /** memory barrier */
  asm volatile ("" ::: "memory");

  printf("---- After consuming, cgroup has %" PRIu64 "cpu quota available ----\n", test_cgroup->cpu_quota_available);

  _CORE_cgroup_Consume_cpu_quota(test_cgroup, 1);
  rtems_test_assert(test_cgroup->cpu_quota_available == 0);

  /** memory barrier */
  asm volatile ("" ::: "memory");
  printf("---- After second consuming, cgroup has %" PRIu64 " cpu quota available ----\n", test_cgroup->cpu_quota_available);

  /** Again check thread states */
  for(int i=0; i<5; i++) {
    ISR_lock_Context lock_context;
    Thread_Control* thread;
    thread = _Thread_Get(Task_id[i], &lock_context);
    if (thread != NULL) {
      _ISR_lock_ISR_enable(&lock_context);
      char buf[16];
      rtems_assoc_thread_states_to_string(thread->current_state, buf, sizeof(buf));
      printf(" <====== Task %u state is %s ======>\n", Task_id[i], buf);
    }
  }

  /** wait for next cgroup period, validate resume function */
  rtems_task_wake_after( test_core_config.cpu_period );
  /** Again check thread states */
  for(int i=0; i<5; i++) {
    ISR_lock_Context lock_context;
    Thread_Control* thread;
    thread = _Thread_Get(Task_id[i], &lock_context);
    if (thread != NULL) {
      _ISR_lock_ISR_enable(&lock_context);
      char buf[16];
      rtems_assoc_thread_states_to_string(thread->current_state, buf, sizeof(buf));
      printf(" <====== Task %u state is %s ======>\n", Task_id[i], buf);
    }
  }
  /** wait for next cgroup period, validate resume function */
  rtems_task_wake_after( test_core_config.cpu_period );
  /** wait for next cgroup period, validate resume function */
  rtems_task_wake_after( test_core_config.cpu_period );
  /** wait for next cgroup period, validate resume function */
  rtems_task_wake_after( test_core_config.cpu_period );
  /** wait for next cgroup period, validate resume function */
  rtems_task_wake_after( test_core_config.cpu_period );
}

rtems_task Init(rtems_task_argument argument)
{
  (void) argument;

  rtems_print_printer_fprintf_putc(&rtems_test_printer);
  TEST_BEGIN();

  // test_core_config.cpu_period = rtems_clock_get_ticks_per_second() * 5;
  test_core_config.cpu_quota = rtems_clock_get_ticks_per_second() * 2;
  test_core_config.cpu_period = _Watchdog_Ticks_per_second * 5;
  printf("==== ticks per second are %lu ====\n", rtems_clock_get_ticks_per_second());

  /** Create a new cgroup */
  rtems_name cg_name = rtems_build_name( 'C', 'G', '2', ' ' );
  rtems_id cg_id;
  rtems_cgroup_create( cg_name, &cg_id, &test_core_config );

  create_test_tasks();

  rtems_status_code status;
  

  for(int i = 0; i < 5; i++) {
    status = rtems_cgroup_add_task(cg_id, Task_id[i]);
    rtems_test_assert(status == RTEMS_SUCCESSFUL);
  }

  ISR_lock_Context lock_context;
  Cgroup_Control*  the_cgroup = _Cgroup_Get(cg_id, &lock_context);
  _ISR_lock_ISR_enable( &lock_context );

  test_cgroup = &the_cgroup->cgroup;

  // verify_quota_consumption();

  rtems_task_wake_after( _Watchdog_Ticks_per_second * 20 );

  printf("---- Test Completed Successfully ----\n");

  /** Again check thread states */
  for(int i=0; i<5; i++) {
    ISR_lock_Context lock_context;
    Thread_Control* thread;
    thread = _Thread_Get(Task_id[i], &lock_context);
    if (thread != NULL) {
      _ISR_lock_ISR_enable(&lock_context);
      char buf[16];
      rtems_assoc_thread_states_to_string(thread->current_state, buf, sizeof(buf));
      printf(" <====== Task %u state is %s ======>\n", Task_id[i], buf);
    }
  }

  TEST_END();
  rtems_test_exit(0);
}
#endif /* RTEMS_CGROUP */

