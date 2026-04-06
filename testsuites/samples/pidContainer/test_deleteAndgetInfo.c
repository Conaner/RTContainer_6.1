#include <stdio.h>
#include <rtems.h>
#include <rtems/score/container.h>
#include <rtems/score/pidContainer.h>
#include <tmacros.h>
#include <rtems/score/threadimpl.h>

const char rtems_test_name[] = "PID CONTAINER DELETE & INFO TEST";

typedef struct {
    rtems_id tid;
    Thread_Control *thread;
} FindThreadArg;

static bool find_thread_by_tid(Thread_Control *thread, void *arg) {
    FindThreadArg *fta = (FindThreadArg *)arg;
    if (thread->Object.id == fta->tid) {
        fta->thread = thread;
        return false; // 找到后停止遍历
    }
    return true; // 继续遍历
}

static rtems_task test_task(rtems_task_argument arg)
{
    printf("test_task %d running!\n", arg);
    rtems_task_exit();
}

static void print_thread_info(const char *msg, Thread_Control *thread)
{
    ThreadContainerInfo info = rtems_pid_container_get_thread_info(thread);
    if (info.isRoot == 1)
        printf("%s: 线程在根容器，containerID=%d, id=%d\n", msg, info.containerID, info.vid_or_id);
    else if (info.isRoot == 0)
        printf("%s: 线程在子容器，containerID=%d, vid=%d\n", msg, info.containerID, info.vid_or_id);
    else
        printf("%s: 未找到线程所在容器\n", msg);
}

static void print_all_pid_containers(void)
{
    Container *container = rtems_container_get_root();
    if (!container) {
        printf("rtems_container_get_root() == NULL\n");
        return;
    }
    printf("当前存在的PID容器ID：");
    // 根容器
    if (container->pidContainer)
        printf("%d ", rtems_pid_container_get_id(container->pidContainer));

    // 其它容器
    PidContainerNode *node = container->pidContainerListHead;
    while (node) {
        if (node->pidContainer)
            printf("%d %d", rtems_pid_container_get_id(node->pidContainer), rtems_pid_container_get_rc(node->pidContainer));
        node = node->next;
    }
    printf("\n");
}

static rtems_task Init(rtems_task_argument ignored)
{
    rtems_id tid1, tid2, tid3;
    rtems_status_code sc;

    rtems_print_printer_fprintf_putc(&rtems_test_printer);
    TEST_BEGIN();

#ifdef RTEMSCFG_PID_CONTAINER
    Container *container = rtems_container_get_root();
    PidContainer *rootPidContainer = container->pidContainer;

    // 创建3个任务
    sc = rtems_task_create(rtems_build_name('T','1',' ',' '), 1, RTEMS_MINIMUM_STACK_SIZE,
        RTEMS_DEFAULT_MODES, RTEMS_DEFAULT_ATTRIBUTES, &tid1);
    if (sc == RTEMS_SUCCESSFUL) rtems_task_start(tid1, test_task, 1);

    sc = rtems_task_create(rtems_build_name('T','2',' ',' '), 1, RTEMS_MINIMUM_STACK_SIZE,
        RTEMS_DEFAULT_MODES, RTEMS_DEFAULT_ATTRIBUTES, &tid2);
    // printf("创建任务2: sc=%d, tid2=0x%x\n", sc, tid2);
    if (sc == RTEMS_SUCCESSFUL) rtems_task_start(tid2, test_task, 2);

    sc = rtems_task_create(rtems_build_name('T','3',' ',' '), 1, RTEMS_MINIMUM_STACK_SIZE,
        RTEMS_DEFAULT_MODES, RTEMS_DEFAULT_ATTRIBUTES, &tid3);
    // printf("创建任务3: sc=%d, tid3=0x%x\n", sc, tid3);
    if (sc == RTEMS_SUCCESSFUL) rtems_task_start(tid3, test_task, 3);

    // 查找Thread_Control指针
    Thread_Control *thread1 = NULL, *thread2 = NULL, *thread3 = NULL;
    // ThreadNode *node = rootPidContainer->threadListHead;
    // while (node) {
    //     Thread_Control *thread = (Thread_Control *)node->thread;
    //     if (thread->Object.id == tid1) thread1 = thread;
    //     if (thread->Object.id == tid2) thread2 = thread;
    //     if (thread->Object.id == tid3) thread3 = thread;
    //     node = node->next;
    // }
    FindThreadArg fta1 = { .tid = tid1, .thread = NULL };
    rtems_pid_container_foreach_thread(rootPidContainer, find_thread_by_tid, &fta1);
    thread1 = fta1.thread;
    FindThreadArg fta2 = { .tid = tid2, .thread = NULL };
    rtems_pid_container_foreach_thread(rootPidContainer, find_thread_by_tid, &fta2);
    thread2 = fta2.thread;
    FindThreadArg fta3 = { .tid = tid3, .thread = NULL };
    rtems_pid_container_foreach_thread(rootPidContainer, find_thread_by_tid, &fta3);
    thread3 = fta3.thread;


    // 创建两个新的PID容器
    PidContainer *containerA = rtems_pid_container_create();
    PidContainer *containerB = rtems_pid_container_create();

    // 打印初始信息
    print_thread_info("初始 thread1", thread1);
    print_thread_info("初始 thread2", thread2);
    print_thread_info("初始 thread3", thread3);

    // 把 thread2、thread3 分别移到 containerA、containerB
    rtems_pid_container_move_task(rootPidContainer, containerA, thread2);
    // printf("rootPidContainer rc=%d, containerA rc=%d\n", rootPidContainer->rc, containerA->rc);
    rtems_pid_container_move_task(rootPidContainer, containerB, thread3);
    // printf("rootPidContainer rc=%d, containerA rc=%d\n", rootPidContainer->rc, containerA->rc);

    print_thread_info("切换后 thread1", thread1);
    print_thread_info("切换后 thread2", thread2);
    print_thread_info("切换后 thread3", thread3);

    // 再把 thread2 从 containerA 移到 containerB
    rtems_pid_container_move_task(containerA, containerB, thread2);
    // printf("rootPidContainer rc=%d, containerA rc=%d\n", rootPidContainer->rc, containerA->rc);

    print_thread_info("thread2 移到 containerB 后", thread2);

    if (!rtems_pid_container_exists(containerA)) 
    {
        printf("containerA 已经被删除或不存在\n");
    } else {
        printf("containerA 仍然存在，ID=%d\n", rtems_pid_container_get_id(containerA));
    }

    // 删除 containerB（thread2、thread3 应回到根容器）
    printf("删除 containerB（ID=%d）\n", rtems_pid_container_get_id(containerB));
    rtems_pid_container_delete(containerB);

    // 再次打印所有线程信息
    print_thread_info("删除后 thread1", thread1);
    print_thread_info("删除后 thread2", thread2);
    print_thread_info("删除后 thread3", thread3);

    // 打印当前还存在的PID容器ID
    print_all_pid_containers();
#else
    printf("RTEMSCFG_PID_CONTAINER not defined\n");
#endif

    TEST_END();
    rtems_test_exit(0);
}

#define CONFIGURE_APPLICATION_DOES_NOT_NEED_CLOCK_DRIVER
#define CONFIGURE_APPLICATION_NEEDS_SIMPLE_CONSOLE_DRIVER
#define CONFIGURE_MAXIMUM_TASKS            8
#define CONFIGURE_RTEMS_INIT_TASKS_TABLE
#define CONFIGURE_INIT_TASK_ATTRIBUTES RTEMS_FLOATING_POINT
#define CONFIGURE_INITIAL_EXTENSIONS RTEMS_TEST_INITIAL_EXTENSION
#define CONFIGURE_INIT
#include <rtems/confdefs.h>