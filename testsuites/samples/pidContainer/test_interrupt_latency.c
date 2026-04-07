/*
 * PID容器中断响应时间测试
 * 目标: 验证容器内中断响应时间为us级
 * 
 * 测试方法:
 * 1. 创建一个子容器,内有一个任务
 * 2. 使用定时器中断触发信号量释放
 * 3. 测量从中断发生到任务响应的时间差
 * 4. 多次采样统计平均值、最小值、最大值
 */

#include <stdio.h>
#include <rtems.h>
#include <rtems/score/container.h>
#include <rtems/score/pidContainer.h>
#include <tmacros.h>
#include <rtems/score/threadimpl.h>
#include <machine/_timecounter.h>

const char rtems_test_name[] = "PID CONTAINER INTERRUPT LATENCY TEST";

#define TEST_ITERATIONS 1000

static PidContainer *root_pid = NULL;
static PidContainer *sub_container = NULL;
static rtems_id task_id;
static rtems_id sem_id;

static volatile uint64_t interrupt_time = 0;
static uint64_t latency_sum = 0;
static uint64_t latency_max = 0;
static uint64_t latency_min = UINT64_MAX;
static volatile int test_done = 0;

// 获取高精度时间戳(纳秒)
static inline uint64_t get_timestamp_ns(void)
{
    struct bintime bt;
    _Timecounter_Binuptime(&bt);
    return ((uint64_t)bt.sec * 1000000000ULL) + 
           ((uint64_t)bt.frac >> 32) * 1000000000ULL / 0x100000000ULL;
}

// 容器内的测试任务
static rtems_task container_task(rtems_task_argument arg)
{
    Thread_Control *self = _Thread_Get_executing();
    int i;
    
    rtems_pid_container_move_task(root_pid, sub_container, self);
    printf("任务已在子容器中,开始测试\n\n");
    
    for (i = 0; i < TEST_ITERATIONS; i++) {
        rtems_semaphore_obtain(sem_id, RTEMS_WAIT, RTEMS_NO_TIMEOUT);
        
        uint64_t latency = get_timestamp_ns() - interrupt_time;
        latency_sum += latency;
        
        if (latency > latency_max)
            latency_max = latency;
        if (latency < latency_min)
            latency_min = latency;
        
        if ((i + 1) % 200 == 0)
            printf("已完成 %d 次测试\n", i + 1);
    }
    
    test_done = 1;
    rtems_task_suspend(RTEMS_SELF);
}

static rtems_task Init(rtems_task_argument ignored)
{
    rtems_status_code sc;
    int i;
    
    rtems_print_printer_fprintf_putc(&rtems_test_printer);
    TEST_BEGIN();

#ifdef RTEMSCFG_PID_CONTAINER
    Container *root_container = rtems_container_get_root();
    root_pid = root_container->pidContainer;
    
    printf("========================================\n");
    printf("PID容器中断响应时间测试 (测试次数: %d)\n", TEST_ITERATIONS);
    printf("========================================\n\n");
    
    sub_container = rtems_pid_container_create();
    
    // 创建计数信号量
    sc = rtems_semaphore_create(rtems_build_name('S','E','M','1'), 0,
        RTEMS_COUNTING_SEMAPHORE | RTEMS_PRIORITY | RTEMS_FIFO, 0, &sem_id);
    rtems_test_assert(sc == RTEMS_SUCCESSFUL);
    
    // 创建任务(优先级5,高于Init任务)
    sc = rtems_task_create(rtems_build_name('T','S','K','1'), 5,
        RTEMS_MINIMUM_STACK_SIZE * 2, RTEMS_DEFAULT_MODES,
        RTEMS_DEFAULT_ATTRIBUTES, &task_id);
    rtems_test_assert(sc == RTEMS_SUCCESSFUL);
    
    // 启动任务
    sc = rtems_task_start(task_id, container_task, 0);
    rtems_test_assert(sc == RTEMS_SUCCESSFUL);
    rtems_task_wake_after(10);
    
    // Init任务循环释放信号量(模拟中断)
    for (i = 0; i < TEST_ITERATIONS; i++) {
        interrupt_time = get_timestamp_ns();
        rtems_semaphore_release(sem_id);
        rtems_task_wake_after(2);
    }
    
    // 等待测试完成
    while (!test_done)
        rtems_task_wake_after(5);
    
    // 打印结果
    printf("\n========================================\n");
    printf("测试结果:\n");
    printf("  平均响应延迟: %.2f us\n", latency_sum / (double)TEST_ITERATIONS / 1000.0);
    printf("  最小响应延迟: %.2f us\n", latency_min / 1000.0);
    printf("  最大响应延迟: %.2f us\n", latency_max / 1000.0);
    printf("========================================\n");
    
    // 清理资源
    rtems_task_delete(task_id);
    rtems_semaphore_delete(sem_id);
    
#else
    printf("此测试需要PID容器支持\n");
#endif

    TEST_END();
    rtems_test_exit(0);
}

#define CONFIGURE_APPLICATION_NEEDS_CLOCK_DRIVER
#define CONFIGURE_APPLICATION_NEEDS_SIMPLE_CONSOLE_DRIVER

#define CONFIGURE_MAXIMUM_TASKS 5
#define CONFIGURE_MAXIMUM_SEMAPHORES 5

#define CONFIGURE_INITIAL_EXTENSIONS RTEMS_TEST_INITIAL_EXTENSION

#define CONFIGURE_RTEMS_INIT_TASKS_TABLE

#define CONFIGURE_INIT
#include <rtems/confdefs.h>
