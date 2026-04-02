
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

const char rtems_test_name[] = "ORGTEST4 WATCHDOG ARGS";

static Watchdog_Control test_watchdog;
static Per_CPU_Control *test_cpu;
static volatile bool test_fired;
static uint32_t test_fire_count;
char buf[16]; 

typedef struct {
  Watchdog_Control watchdog;
  int              my_value;
  Thread_Control*  user_ptr;
} my_watchdog_t;

static my_watchdog_t suspend_wd;
static my_watchdog_t resume_wd;

static void test_watchdog_service( Watchdog_Control *watchdog )
{
  uint64_t current_ticks = test_cpu->Watchdog.ticks;
  printf( "[Task Routine][%" PRIu64 "]Watchdog service routine called!" );

  rtems_test_busy_cpu_usage( 20, 0 );

  current_ticks = test_cpu->Watchdog.ticks;
  printf( "[Task Routine][%" PRIu64 "]Watchdog service routine end!\n", current_ticks );

  rtems_task_exit();
}


static void suspend_watchdog_routine( Watchdog_Control *wd )
{
  my_watchdog_t *ctx = RTEMS_CONTAINER_OF( wd, my_watchdog_t, watchdog );

  Thread_Control* the_thread = ctx->user_ptr;

  /** get current cpu ticks */
  uint64_t current_ticks = test_cpu->Watchdog.ticks;

  printf( "[suspend watchdog][%" PRIu64 "] suspend routine start \n", current_ticks );

  // rtems_assoc_thread_states_to_string(the_thread->current_state, buf, sizeof(buf));
  // printf("[suspend watchdog][%" PRIu64 "]Before suspend, Thread state is %s\n", current_ticks, buf);

  printf( "[suspend watchdog] Suspending the thread \n" );

  rtems_task_suspend( the_thread->Object.id );

  rtems_assoc_thread_states_to_string(the_thread->current_state, buf, sizeof(buf));
  current_ticks = test_cpu->Watchdog.ticks;
  printf("[suspend watchdog][%" PRIu64 "]After suspend, Thread state is %s\n", current_ticks, buf);
}

static void resume_watchdog_routine( Watchdog_Control *wd )
{
  my_watchdog_t *ctx = RTEMS_CONTAINER_OF( wd, my_watchdog_t, watchdog );

  Thread_Control* the_thread = ctx->user_ptr;

  printf( "[resume watchdog] resume routine start \n" );

  /** get current cpu ticks */
  uint64_t current_ticks = test_cpu->Watchdog.ticks;

  // rtems_assoc_thread_states_to_string(the_thread->current_state, buf, sizeof(buf));
  // printf("[resume watchdog][%lu]Before resume, Thread state is %s\n", current_ticks, buf);

  rtems_task_resume( the_thread->Object.id );

  // rtems_assoc_thread_states_to_string(the_thread->current_state, buf, sizeof(buf));
  // current_ticks = test_cpu->Watchdog.ticks;
  // printf("[resume watchdog][%lu]After resume, Thread state is %s\n", current_ticks, buf);

  printf("[resume watchdog] Launch next period's suspend and resume watchdogs \n" );
  _Watchdog_Per_CPU_insert_ticks( &suspend_wd.watchdog, test_cpu, _Watchdog_Ticks_per_second * 2 );

  _Watchdog_Per_CPU_insert_ticks( &resume_wd.watchdog, test_cpu, _Watchdog_Ticks_per_second * 5 );
}

static void suspend_watchdog_setup( Thread_Control* the_thread)
{
  test_cpu = _Per_CPU_Get_by_index( 0 );
  _Watchdog_Preinitialize( &suspend_wd.watchdog, test_cpu );
  _Watchdog_Initialize( &suspend_wd.watchdog, suspend_watchdog_routine );

  suspend_wd.user_ptr = the_thread;
}

static void resume_watchdog_setup( Thread_Control* the_thread)
{
  test_cpu = _Per_CPU_Get_by_index( 0 );
  _Watchdog_Preinitialize( &resume_wd.watchdog, test_cpu );
  _Watchdog_Initialize( &resume_wd.watchdog, resume_watchdog_routine );

  resume_wd.user_ptr = the_thread;  
}

rtems_task Init(rtems_task_argument ignored) {
  rtems_print_printer_fprintf_putc(&rtems_test_printer);
  TEST_BEGIN();

  /** create a thread for test */
  rtems_task_create(
    rtems_build_name( 'T', 'E', 'S', 'T' ),
    11,
    RTEMS_MINIMUM_STACK_SIZE,
    RTEMS_DEFAULT_MODES,
    RTEMS_DEFAULT_ATTRIBUTES,
    &Task_id[0]
  );

  Thread_Control *the_thread;
  ISR_lock_Context lock_context;
  the_thread = _Thread_Get( Task_id[0], &lock_context );
  _ISR_lock_ISR_enable(&lock_context);

  /** start the thread */
  rtems_task_start( Task_id[0], test_watchdog_service, 0 );
  /** validate thread state */
  rtems_assoc_thread_states_to_string(the_thread->current_state, buf, sizeof(buf));
  printf("After Start, Task %u state is %s\n", Task_id[0], buf);

  /** setup watchdog */
  suspend_watchdog_setup(the_thread);
  resume_watchdog_setup(the_thread);

  rtems_test_assert( !_Watchdog_Is_scheduled( &suspend_wd.watchdog ) );
  rtems_test_assert( !_Watchdog_Is_scheduled( &resume_wd.watchdog ) );

  _Watchdog_Per_CPU_insert_ticks( &suspend_wd.watchdog, test_cpu, _Watchdog_Ticks_per_second * 2 );

  _Watchdog_Per_CPU_insert_ticks( &resume_wd.watchdog, test_cpu, _Watchdog_Ticks_per_second * 5 );

  rtems_test_assert( _Watchdog_Is_scheduled( &suspend_wd.watchdog ) );
  rtems_test_assert( _Watchdog_Is_scheduled( &resume_wd.watchdog ) );

  rtems_task_wake_after( _Watchdog_Ticks_per_second * 51 );

  printf("---- Test Completed Successfully ----\n");

  TEST_END();
  rtems_test_exit( 0 );
}


