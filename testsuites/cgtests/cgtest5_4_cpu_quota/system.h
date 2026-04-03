#include <tmacros.h>

rtems_task Init( rtems_task_argument argument );

#define CONFIGURE_APPLICATION_NEEDS_SIMPLE_CONSOLE_DRIVER
#define CONFIGURE_APPLICATION_NEEDS_CLOCK_DRIVER

#define CONFIGURE_MAXIMUM_TASKS              11
#define CONFIGURE_MAXIMUM_TIMERS              1
#define CONFIGURE_MAXIMUM_SEMAPHORES          2
#define CONFIGURE_MAXIMUM_MESSAGE_QUEUES      1
#define CONFIGURE_MAXIMUM_CGROUPS             5
#define CONFIGURE_MAXIMUM_PERIODS             1
#define CONFIGURE_MAXIMUM_USER_EXTENSIONS     0
#define CONFIGURE_TICKS_PER_TIMESLICE       100

#define CONFIGURE_INIT_TASK_PRIORITY        10
#define CONFIGURE_RTEMS_INIT_TASKS_TABLE
#define CONFIGURE_EXTRA_TASK_STACKS         (20 * RTEMS_MINIMUM_STACK_SIZE)

#include <rtems/confdefs.h>

TEST_EXTERN rtems_id   Task_id[ 11 ];
TEST_EXTERN rtems_name Task_name[ 11 ];
TEST_EXTERN rtems_name Semaphore_name[ 4 ];
TEST_EXTERN rtems_id   Semaphore_id[ 4 ];
TEST_EXTERN rtems_name Queue_name[ 3 ];
TEST_EXTERN rtems_id   Queue_id[ 3 ];
TEST_EXTERN rtems_name Port_name[ 2 ];
TEST_EXTERN rtems_id   Port_id[ 2 ];
TEST_EXTERN rtems_name Period_name[ 2 ];
TEST_EXTERN rtems_id   Period_id[ 2 ];
TEST_EXTERN rtems_id   Junk_id;

#define Internal_port_area (void *) 0x00001000
#define External_port_area (void *) 0x00002000
