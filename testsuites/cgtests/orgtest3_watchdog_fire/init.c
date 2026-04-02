
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

const char rtems_test_name[] = "ORGTEST3 WATCHDOG FIRE";

static Watchdog_Control test_watchdog;
static Per_CPU_Control *test_cpu;
static volatile bool test_fired;
static uint32_t test_fire_count;
char buf[16]; 

static void test_watchdog_service( Watchdog_Control *watchdog )
{
  puts( "Watchdog service routine called!" );
  (void) watchdog;
  ++test_fire_count;
  test_fired = true;

  ISR_lock_Context lock_context;
  Thread_Control* the_thread = _Thread_Get( Task_id[0], &lock_context );
  _ISR_lock_ISR_enable(&lock_context);  
  
  rtems_task_suspend( the_thread->Object.id );

  puts( "<=========== task suspended ===========> \n" );
}

static void test_watchdog_setup( void )
{
  test_cpu = _Per_CPU_Get_by_index( 0 );
  _Watchdog_Preinitialize( &test_watchdog, test_cpu );
  _Watchdog_Initialize( &test_watchdog, test_watchdog_service );
  test_fired = false;
  test_fire_count = 0;
}

static inline uint32_t read_cpsr(void)
{
#if defined(__arm__)
  uint32_t value;
  __asm__ volatile ( "mrs %0, cpsr" : "=r" ( value ) );
  return value;
#else
  return 0;
#endif
}

rtems_task Init(rtems_task_argument ignored) {
  rtems_print_printer_fprintf_putc(&rtems_test_printer);
  TEST_BEGIN();
  #ifdef RTEMS_CGROUP
  rtems_status_code status;
  /** Create a new cgroup */
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

  /** Create several tasks to test the watchdog */
  for (int i = 0; i < 3; i++) {
    rtems_status_code status;
    printf("----------- create a new task -----------\n");
    status = rtems_task_create(
      rtems_build_name('T', 'A', 'S', 'K'),
        11,
        RTEMS_MINIMUM_STACK_SIZE,
        RTEMS_DEFAULT_MODES,
        RTEMS_DEFAULT_ATTRIBUTES,
        &Task_id[i]
    );
    if (status != RTEMS_SUCCESSFUL) {
      printf("Task creation failed with status %d\n", status);
      rtems_test_exit(1);
    }
    status = rtems_cgroup_add_task(cg_id, Task_id[i]);
    if (status != RTEMS_SUCCESSFUL) {
      printf("Failed to add task %u to cgroup %u, status %d\n", Task_id[i], cg_id, status);
      rtems_test_exit(1);
    } else {
      printf("Task %u added to cgroup %u successfully\n", Task_id[i], cg_id);
    }
    ISR_lock_Context lock_context;
    Thread_Control* thread;
    thread = _Thread_Get(Task_id[i], &lock_context);
    if (thread != NULL) {  
      printf("Task %u state is %u\n", Task_id[i], thread->current_state);  
      uint32_t cpsr_before = read_cpsr();
      _ISR_lock_ISR_enable(&lock_context);  
      uint32_t cpsr_after = read_cpsr();
      printf("CPSR before: 0x%08" PRIx32 ", after: 0x%08" PRIx32 "\n",
             cpsr_before, cpsr_after);
    }
    rtems_assoc_thread_states_to_string(thread->current_state, buf, sizeof(buf));
    printf("Task %u state is %s\n", Task_id[i], buf);
    rtems_task_start(Task_id[i], NULL, 0);
    rtems_assoc_thread_states_to_string(thread->current_state, buf, sizeof(buf));
    printf("After Start, Task %u state is %s\n", Task_id[i], buf);
  }

  /** Check the task count */
  uint32_t task_count;
  status = rtems_cgroup_get_task_count(cg_id, &task_count);
  if( status != RTEMS_SUCCESSFUL ) {
    printf("Failed to get task count for cgroup %u, status %d\n", cg_id, status);
    rtems_test_exit(1);
  }
  printf("Cgroup %u has %d tasks\n", cg_id, task_count);

  if(status != RTEMS_SUCCESSFUL) {
    printf("Test Task creation failed with status %s\n", rtems_status_text(status));
    rtems_test_exit(1);
  } else {
    printf("Test Task created with ID %u\n", Task_id[5]);
  }

  test_watchdog_setup();

  rtems_test_assert( !_Watchdog_Is_scheduled( &test_watchdog ) );

  uint64_t start_ticks = test_cpu->Watchdog.ticks;

  _Watchdog_Per_CPU_insert_ticks( &test_watchdog, test_cpu, 3 );

  rtems_test_assert( _Watchdog_Is_scheduled( &test_watchdog ) );
  rtems_test_assert( !test_fired );
  rtems_test_assert( test_fire_count == 0 );

  for ( uint32_t i = 0; i < 3 && !test_fired; ++i ) {
    _Watchdog_Tick( test_cpu );
  }

  /** validate object thread state */
  Thread_Control* test_thread;
  ISR_lock_Context lock_context;
  test_thread = _Thread_Get( Task_id[0], &lock_context );
  if (test_thread != NULL) {
    _ISR_lock_ISR_enable(&lock_context);  
    rtems_assoc_thread_states_to_string(test_thread->current_state, buf, sizeof(buf));
    printf("After Watchdog fired, Task %u state is %s\n", Task_id[0], buf);
  }

  rtems_test_assert( test_fired );
  rtems_test_assert( test_fire_count == 1 );
  rtems_test_assert( !_Watchdog_Is_scheduled( &test_watchdog ) );
  rtems_test_assert( test_cpu->Watchdog.ticks >= start_ticks + 3 );

  puts( "Watchdog callback fired as expected" );

  TEST_END();
  rtems_test_exit( 0 );
  #else
  printf( "==================CGROUP not enabled!====================\n");
  #endif
}


