
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

const char rtems_test_name[] = "ORGTEST6 MEM USE";

static rtems_task task_entry(rtems_task_argument arg) {
  printf("Task %d started\n", (int)arg);
  
  void *ptr;
  void *segment_ptr;
  rtems_status_code sc;

  /** Test C lib interface */
  printf("===========Task %d: C lib malloc/free test===========\n", (int)arg);
  printf("Testing malloc/free of %d MB\n", (int)arg);
  ptr = malloc(1024 * 1024 * arg); // allocate arg MB
  if(ptr) free(ptr);
  printf("Testing calloc/free of %d MB\n", (int)arg);
  ptr = calloc(arg, 1024 * 1024); // allocate arg MB
  if(ptr) free(ptr);
  printf("Testing realloc of %d MB\n", (int)arg);
  ptr = malloc(512 * 1024 * arg); // allocate arg*0.5 MB
  if(ptr) {
    ptr = realloc(ptr, 1024 * 1024 * arg); // resize to arg MB
    if(ptr) free(ptr);
  }
  printf("===========Task %d: C lib malloc/free test done===========\n", (int)arg);

  /** Test RTEMS Region */
  printf("===========Task %d: RTEMS region allocate/free test===========\n", (int)arg);
  ptr = malloc(4096);
  sc = rtems_region_create(
        rtems_build_name( 'R', 'E', 'G', '0'+(int)arg ),
        ptr,
        4096,
        1,
        RTEMS_DEFAULT_ATTRIBUTES,
        &Region_id[(int)arg]
      );
  if(sc != RTEMS_SUCCESSFUL) {
    printf("Task %d: rtems_region_create failed: %s\n", (int)arg, rtems_status_text(sc));
    rtems_task_exit();
  }
  printk("Testing rtems_region_get_segment(256)... (Should show in Log)\n");
  sc = rtems_region_get_segment(Region_id[(int)arg], 256, RTEMS_DEFAULT_OPTIONS, 0, &segment_ptr);
  if (sc == RTEMS_SUCCESSFUL) {
    rtems_region_return_segment(Region_id[(int)arg], segment_ptr);
  } else {
    printk("Region get segment failed\n");
  }
  rtems_region_delete(Region_id[(int)arg]);
  free(ptr);
  printf("===========Task %d: RTEMS region allocate/free test done===========\n", (int)arg);
  rtems_task_exit();
}

rtems_task Init(rtems_task_argument ignored) {
  rtems_print_printer_fprintf_putc(&rtems_test_printer);
  TEST_BEGIN();

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
  printf("=====> Init task sleep for 2 seconds to allow test tasks to complete <=====\n");
  rtems_task_wake_after( _Watchdog_Ticks_per_second * 2 );

  TEST_END();
  rtems_test_exit( 0 );
}


