
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#define CONFIGURE_INIT
#include "system.h"

#include <rtems/score/watchdogimpl.h>
#include <rtems/score/thread.h>
#include <rtems/score/threadimpl.h>

#include <rtems/rtems/cgroupimpl.h>
#include <rtems/rtems/support.h>
#include <rtems/rtems/tasks.h>
#include <rtems/rtems/timer.h>

#include <rtems/assoc.h>
#include <rtems/capture-cli.h>
#include <rtems/captureimpl.h>
#include <rtems/monitor.h>

const char rtems_test_name[] = "ORGTEST5 CPU MATCHED GROUP";

static rtems_task task_entry(rtems_task_argument arg) {
  printf("Task %d started\n", (int)arg);
  /** execute for different intervals */
  rtems_test_busy_cpu_usage( arg, 1e5 );

  rtems_task_exit();
}

rtems_task Init(rtems_task_argument ignored) {
  rtems_print_printer_fprintf_putc(&rtems_test_printer);
  TEST_BEGIN();

  rtems_capture_open(5000, NULL);
  rtems_capture_watch_ceiling(0);
  rtems_capture_watch_floor(200);
  rtems_capture_watch_global(true);

  rtems_capture_set_trigger(0, 0, 0, 0,
                            rtems_capture_from_any,
                            rtems_capture_switch);

  rtems_capture_print_watch_list();
  rtems_capture_set_control(true);

  /** create several threads for test */
  for(int i=0; i<=5; i++){
    rtems_task_create(
        rtems_build_name( 'T', 'S', 'T', '0'+i ),
        10-i,
        RTEMS_MINIMUM_STACK_SIZE,
        RTEMS_DEFAULT_MODES,
        RTEMS_DEFAULT_ATTRIBUTES,
        &Task_id[i]
    );
    /** start the thread */
    rtems_task_start( Task_id[i], (rtems_task_entry) task_entry, i );
  }

  /** wait for all tasks to complete */
  printf("=====> Init task sleep for 20 seconds to allow test tasks to complete <=====\n");
  rtems_task_wake_after( _Watchdog_Ticks_per_second * 20 );

  /** end and output capture results */
  rtems_capture_set_control(false);
  rtems_capture_print_trace_records(50, false);
  rtems_capture_close();


  TEST_END();
  rtems_test_exit( 0 );
}


