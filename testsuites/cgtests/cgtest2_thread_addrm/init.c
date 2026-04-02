#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#define CONFIGURE_INIT
#include "system.h"

#ifdef RTEMS_CGROUP
#include <rtems/score/corecgroupimpl.h>
#include <rtems/rtems/cgroupimpl.h>
#endif

const char rtems_test_name[] = "CGTEST2 THREAD ADD & REMOVE";

rtems_task Task(rtems_task_argument arg)
{
  printf("Hello from Task! arg=%lu\n", (unsigned long) arg);
  while(true);
}

rtems_task Init(
  rtems_task_argument ignored
)
{
  rtems_print_printer_fprintf_putc(&rtems_test_printer);
  TEST_BEGIN();
  for(int i = 0; i < 11; i++) Task_name[i] = i;
  #ifdef RTEMS_CGROUP
    printf( "==================START CGTEST1: THREAD ADD & REMOVE=================\n" );

    rtems_name cg_name = rtems_build_name( 'C', 'G', '2', ' ' );
    rtems_id cg_id;
    CORE_cgroup_config core_config = {
      .cpu_shares = 1024,
      .cpu_quota = 10,
      .cpu_period = 100,
      .memory_limit = 16 * 1024 * 1024,
      .blkio_limit = 0
    };
    rtems_cgroup_create( cg_name, &cg_id, &core_config );
    if(cg_id <= 0) {
      printf( "==================CGROUP creation failed, id is %u!====================\n", cg_id);
      rtems_test_exit( 1 );
    }
    else {
      printf( "==================CGROUP created successfully, id is %u!====================\n", cg_id);
    }
    
    /* create a new thread and add it to the above cgroup */
    rtems_id task_id;
    rtems_status_code status;
    status = rtems_task_create(
      rtems_build_name('T', 'A', 'S', 'K'),
        1,
        RTEMS_MINIMUM_STACK_SIZE,
        RTEMS_DEFAULT_MODES,
        RTEMS_DEFAULT_ATTRIBUTES,
        &Task_id[1]
    );
    printf("Task creation returned status %d\n", status);
    if (status != RTEMS_SUCCESSFUL) {
      printf("Task creation failed with status %d\n", status);
      rtems_test_exit(1);
    }
    // status = rtems_task_start(task_id, Task, Task_name[0]);
    if (status != RTEMS_SUCCESSFUL) {
      printf("Task start failed with status %d\n", status);
      rtems_test_exit(1);
    }
    printf("Task created and started with ID %u\n", Task_id[1]);   
    status = rtems_cgroup_add_task(cg_id, task_id);
    if (status != RTEMS_SUCCESSFUL) {
      printf("Failed to add task %u to cgroup %u, status %d\n", task_id, cg_id, status);
      rtems_test_exit(1);
    } else {
      printf("Task %u added to cgroup %u successfully\n", task_id, cg_id);
    }
    /* check the task count */
    uint32_t task_count;
    status = rtems_cgroup_get_task_count(cg_id, &task_count);
    if( status != RTEMS_SUCCESSFUL ) {
      printf("Failed to get task count for cgroup %u, status %d\n", cg_id, status);
      rtems_test_exit(1);
    }
    if( task_count != 1 ) {
      printf("Unexpected task count %d for cgroup %u, expected 1\n", task_count, cg_id);
      rtems_test_exit(1);
    }
    printf("Cgroup %u has %d tasks\n", cg_id, task_count);
    /* remove the task from the cgroup */
    status = rtems_cgroup_remove_task(cg_id, task_id);
    if (status != RTEMS_SUCCESSFUL) {
      printf("Failed to remove task %u from cgroup %u, status %d\n", task_id, cg_id, status);
      rtems_test_exit(1);
    }
    printf("Task %u removed from cgroup %u successfully\n", task_id, cg_id);
    /* check the task count */
    status = rtems_cgroup_get_task_count(cg_id, &task_count);
    if( status != RTEMS_SUCCESSFUL ) {
      printf("Failed to get task count for cgroup %u, status %d\n", cg_id, status);
      rtems_test_exit(1);
    }
    if( task_count != 0 ) {
      printf("Unexpected task count %d for cgroup %u, expected 0\n", task_count, cg_id);
      rtems_test_exit(1);
    }   
    printf("Cgroup %u has %d tasks after removal\n", cg_id, task_count);

    /* try add more tasks to cgroup */
    for(int i = 1; i < 11; i++) {
      status = rtems_task_create(
        rtems_build_name('T', 'A', 'S', 'K'),
          1,
          RTEMS_MINIMUM_STACK_SIZE,
          RTEMS_DEFAULT_MODES,
          RTEMS_DEFAULT_ATTRIBUTES,
          Task_id + i
      );
      if (status != RTEMS_SUCCESSFUL) {
        printf("Task creation failed with status %d\n", status);
        rtems_test_exit(1);
      }
    //   status = rtems_task_start(task_id, Task, Task_name[i]);
      if (status != RTEMS_SUCCESSFUL) {
        printf("Task start failed with status %d\n", status);
        rtems_test_exit(1);
      }
      printf("Task created and started with ID %u\n", task_id);   
      status = rtems_cgroup_add_task(cg_id, task_id);
      if (status != RTEMS_SUCCESSFUL) {
        printf("Failed to add task %u to cgroup %u, status %d\n", task_id, cg_id, status);
        rtems_test_exit(1);
      } else {
        printf("Task %u added to cgroup %u successfully\n", task_id, cg_id);
      }
    }
    /* check the task count */
    status = rtems_cgroup_get_task_count(cg_id, &task_count);
    if( status != RTEMS_SUCCESSFUL ) {
      printf("Failed to get task count for cgroup %u, status %d\n", cg_id, status);
      rtems_test_exit(1);
    }
    if( task_count != 10 ) {
      printf("Unexpected task count %d for cgroup %u, expected 10\n", task_count, cg_id);
      rtems_test_exit(1);
    }
    printf("Cgroup %u has %d tasks after adding 10 tasks\n", cg_id, task_count);
    /* remove tasks from cgroup one by one, checking the task count after each removal */
    for(int i = 1; i < 11; i++) {
      status = rtems_cgroup_remove_task(cg_id, Task_id[i]);
      if (status != RTEMS_SUCCESSFUL) {
        printf("Failed to remove task %d from cgroup %u, status %d\n", i, cg_id, status);
        rtems_test_exit(1);
      }
      printf("Task %d removed from cgroup %u successfully\n", i, cg_id);
      /* check the task count */
      status = rtems_cgroup_get_task_count(cg_id, &task_count);
      if( status != RTEMS_SUCCESSFUL ) {
        printf("Failed to get task count for cgroup %u, status %d\n", cg_id, status);
        rtems_test_exit(1);
      }
      if( task_count != (10 - i) ) {
        printf("Unexpected task count %d for cgroup %u, expected %d\n", task_count, cg_id, (10 - i));
        rtems_test_exit(1);
      }
    }
  #else
    printf( "==================CGROUP not enabled!====================\n");
  #endif
  TEST_END();
  rtems_test_exit( 0 );
}
