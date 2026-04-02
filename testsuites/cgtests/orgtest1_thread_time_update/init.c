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

const char rtems_test_name[] = "ORGTEST1 THREAD TIME UPDATE";

static bool TimerFired = false;
static bool TaskSwitch = false;

void FireCallback(rtems_id timer_id, void *user_data);
rtems_task Task(rtems_task_argument arg);
rtems_task Task2(rtems_task_argument arg);

void FireCallback(rtems_id timer_id, void *user_data) {
  printf("Timer fired! ID=%u\n", timer_id);
  TimerFired = true;
}

rtems_task Task(rtems_task_argument arg) {
  rtems_id timer_id;
  rtems_status_code sc;

  printf("Hello from Task! arg=%lu\n", (unsigned long)arg);
  TaskSwitch = true;

  sc = rtems_timer_create(rtems_build_name('T', 'I', 'M', 'R'), &timer_id);
  if (sc != RTEMS_SUCCESSFUL) {
    printf("Timer creation failed with status %d\n", sc);
    rtems_test_exit(1);
  }
  sc = rtems_timer_fire_after(timer_id, RTEMS_MILLISECONDS_TO_TICKS(1000),
                              FireCallback, "hello");
  if (sc != RTEMS_SUCCESSFUL) {
    printf("Timer start failed with status %d\n", sc);
    rtems_test_exit(1);
  }

  while (!TimerFired) {
    // busy wait
  }

  rtems_task_priority old_priority;
  sc = rtems_task_set_priority(RTEMS_SELF, 100, &old_priority);

  rtems_task_wake_after(RTEMS_YIELD_PROCESSOR);
}

rtems_task Task2(rtems_task_argument arg) {
  printf("***********Hello from Task2! arg=%lu***********\n", (unsigned long)arg);
  rtems_status_code sc;
  rtems_task_priority old_priority;
  sc = rtems_task_set_priority(RTEMS_SELF, 100, &old_priority);

  rtems_task_wake_after(RTEMS_YIELD_PROCESSOR);
}

rtems_task Init(rtems_task_argument ignored) {
  rtems_print_printer_fprintf_putc(&rtems_test_printer);
  TEST_BEGIN();

  printf("==================START ORGTEST1: THREAD TIME "
         "UPDATE=================\n");

  for (int i = 0; i < 11; i++)
    Task_name[i] = i;

  rtems_status_code status;

  /* get current using scheduler */
  rtems_id scheduler_id;
  status = rtems_task_get_scheduler(RTEMS_SELF, &scheduler_id);
  const Scheduler_Control *scheduler = _Scheduler_Get_by_id(scheduler_id);
  if (scheduler == NULL) {
    printf("Failed to get scheduler control block for scheduler ID %u\n",
           scheduler_id);
    rtems_test_exit(1);
  }

  uint32_t scheduler_name = scheduler->name;
  char c1, c2, c3, c4;
  rtems_name_to_characters(scheduler_name, &c1, &c2, &c3, &c4);
  printf("Current scheduler is %c%c%c%c\n", c1, c2, c3, c4);

  if (status != RTEMS_SUCCESSFUL) {
    printf("Failed to get scheduler ID, status %d\n", status);
    rtems_test_exit(1);
  }

  /* get priority of Init thread */
  rtems_task_priority init_priority;
  status = rtems_task_set_priority(RTEMS_SELF, RTEMS_CURRENT_PRIORITY,
                                   &init_priority);
  printf("Init task priority is %u\n", init_priority);
  if (status != RTEMS_SUCCESSFUL) {
    printf("Failed to get Init task priority, status %d\n", status);
    rtems_test_exit(1);
  }

  /* create a new thread and check its CPU time used */
  status = rtems_task_create(rtems_build_name('T', 'A', 'S', 'K'), 5,
                             RTEMS_MINIMUM_STACK_SIZE, RTEMS_DEFAULT_MODES,
                             RTEMS_DEFAULT_ATTRIBUTES, &Task_id[1]);
  if (status != RTEMS_SUCCESSFUL) {
    printf("Task creation failed with status %d\n", status);
    rtems_test_exit(1);
  }
  status = rtems_task_start(Task_id[1], Task, Task_name[0]);
  if (status != RTEMS_SUCCESSFUL) {
    printf("Task start failed with status %d\n", status);
    rtems_test_exit(1);
  }
  printf("Task created and started with ID %u\n", Task_id[1]);

  /* create another thread to verify the scheduling policy */
  status = rtems_task_create(rtems_build_name('T', 'A', 'S', '2'), 4,
                             RTEMS_MINIMUM_STACK_SIZE, RTEMS_DEFAULT_MODES,
                             RTEMS_DEFAULT_ATTRIBUTES, &Task_id[2]);
  if (status != RTEMS_SUCCESSFUL) {
    printf("Task 2 creation failed with status %d\n", status);
    rtems_test_exit(1);
  }
  status = rtems_task_start(Task_id[2], Task2, Task_name[1]);
  if (status != RTEMS_SUCCESSFUL) {
    printf("Task 2 start failed with status %d\n", status);
    rtems_test_exit(1);
  }

  rtems_task_wake_after( RTEMS_YIELD_PROCESSOR );
  // while (!TaskSwitch) {
  //   // wait for task switch
  // }

  ISR_lock_Context lock_context;
  Thread_Control *the_thread = _Thread_Get(Task_id[1], &lock_context);
  if (the_thread == NULL) {
    printf("Failed to get thread control block for task %u\n", Task_id[1]);
    rtems_test_exit(1);
  }
  const Timestamp_Control *cpu_time_used = &(the_thread->cpu_time_used);
  uint64_t cpu_time_used_ns = _Timestamp_Get_as_nanoseconds(cpu_time_used);
  printf("Task %u has used %llu ns of CPU time\n", Task_id[1],
         cpu_time_used_ns);
  if (cpu_time_used_ns == 0) {
    printf("Error: Task %u has used 0 ns of CPU time\n", Task_id[1]);
    rtems_test_exit(1);
  }
  printf("Task %u CPU time used test passed\n", Task_id[1]);

  TEST_END();
  rtems_test_exit(0);
}
