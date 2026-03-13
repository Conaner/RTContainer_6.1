#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <rtems/score/netContainer.h>
#include <rtems/score/container.h>
#include <rtems/score/threadimpl.h>
#include <rtems/score/wkspace.h>

#include <string.h>
#include <stdio.h>
#include <inttypes.h>

#include <rtems/rtems/intr.h>
#include <rtems/rtems_bsdnet.h>
#include <net/if.h>
#include <net/if_var.h>
#include <net/if_veth.h>
#include <sys/socket.h>
#include <sys/domain.h>
#include <netinet/in_pcb.h>
#include <sys/systm.h>
#include <sys/malloc.h>

#define UDBHASHSIZE 64
#define TCBHASHSIZE 128

extern struct ifnet *ifnet;
extern struct ifaddr **ifnet_addrs;

static int g_netContainerId = 1;
static int g_currentNetContainerNum = 0;

static net_group* net_group_new(void);
static void net_group_free(net_group *group);
static bool switch_to_root_net(Thread_Control *thread, void *arg);

// 初始化根网络容器
int rtems_net_container_initialize_root(NetContainer **netContainer)
{
    if (netContainer == NULL)
    {
        return -1;
    }

    *netContainer = (NetContainer *)_Workspace_Allocate(sizeof(NetContainer));     // 可能存在的问题是用户初始化函数使用这个的话用户空间还没启动， 所以使用这个函数存在错误，之后调试出现问题换成malloc
    if (*netContainer == NULL)
    {
        return -1;
    }

    (*netContainer)->rc = 3;
    (*netContainer)->group = net_group_new(); 
    (*netContainer)->containerID = 1;

    g_currentNetContainerNum++;

    return 0;
}

// 创建独立的net_group
static net_group* net_group_new(void)
{
    net_group *group = (net_group *)_Workspace_Allocate(sizeof(net_group));
    if (group == NULL) {
        return NULL;
    }

    memset(group, 0, sizeof(net_group));
    
    // 初始化接口相关
    group->ifnet_p = NULL;          
    group->ifnet_addrs = NULL;      
    group->if_index_counter = 0;    
    group->if_indexlim = 8;         
    group->ifnet_config = NULL;
    
    // 初始化 UDP PCB
    group->udp_pcblist = (struct inpcbhead *)_Workspace_Allocate(sizeof(struct inpcbhead));
    if (group->udp_pcblist == NULL) {
        _Workspace_Free(group);
        return NULL;
    }
    LIST_INIT(group->udp_pcblist);
    
    group->udp_pcbinfo = (struct inpcbinfo *)_Workspace_Allocate(sizeof(struct inpcbinfo));
    if (group->udp_pcbinfo == NULL) {
        _Workspace_Free(group->udp_pcblist);
        _Workspace_Free(group);
        return NULL;
    }
    memset(group->udp_pcbinfo, 0, sizeof(struct inpcbinfo));
    group->udp_pcbinfo->listhead = group->udp_pcblist;
    group->udp_pcbinfo->hashbase = hashinit(UDBHASHSIZE, M_PCB, &group->udp_pcbinfo->hashmask);
    if (group->udp_pcbinfo->hashbase == NULL) {
        _Workspace_Free(group->udp_pcbinfo);
        _Workspace_Free(group->udp_pcblist);
        _Workspace_Free(group);
        return NULL;
    }
    
    // 初始化 TCP PCB
    group->tcp_pcblist = (struct inpcbhead *)_Workspace_Allocate(sizeof(struct inpcbhead));
    if (group->tcp_pcblist == NULL) {
        free(group->udp_pcbinfo->hashbase, M_PCB);
        _Workspace_Free(group->udp_pcbinfo);
        _Workspace_Free(group->udp_pcblist);
        _Workspace_Free(group);
        return NULL;
    }
    LIST_INIT(group->tcp_pcblist);
    
    group->tcp_pcbinfo = (struct inpcbinfo *)_Workspace_Allocate(sizeof(struct inpcbinfo));
    if (group->tcp_pcbinfo == NULL) {
        _Workspace_Free(group->tcp_pcblist);
        free(group->udp_pcbinfo->hashbase, M_PCB);
        _Workspace_Free(group->udp_pcbinfo);
        _Workspace_Free(group->udp_pcblist);
        _Workspace_Free(group);
        return NULL;
    }
    memset(group->tcp_pcbinfo, 0, sizeof(struct inpcbinfo));
    group->tcp_pcbinfo->listhead = group->tcp_pcblist;
    group->tcp_pcbinfo->hashbase = hashinit(TCBHASHSIZE, M_PCB, &group->tcp_pcbinfo->hashmask);
    if (group->tcp_pcbinfo->hashbase == NULL) {
        _Workspace_Free(group->tcp_pcbinfo);
        _Workspace_Free(group->tcp_pcblist);
        free(group->udp_pcbinfo->hashbase, M_PCB);
        _Workspace_Free(group->udp_pcbinfo);
        _Workspace_Free(group->udp_pcblist);
        _Workspace_Free(group);
        return NULL;
    }

#ifdef RTEMSCFG_Carousel_CONTAINER
    /* 默认启用Carousel流调度器，默认速率 10 Mbps */
    group->carousel = carousel_init(10 * 1000000);
    group->carousel_default_rate = 10 * 1000000;
    group->carousel_enabled = (group->carousel != NULL);
    
    if (group->carousel == NULL) {
        printf("Warning: Failed to initialize Carousel for net_group\n");
    }
#endif

    // 分配ifnet_addrs
    group->ifnet_addrs = (struct ifaddr **)_Workspace_Allocate(
        group->if_indexlim * sizeof(struct ifaddr *)
    );
    if (group->ifnet_addrs == NULL) {
#ifdef RTEMSCFG_Carousel_CONTAINER
        if (group->carousel != NULL) {
            carousel_destroy(group->carousel);
        }
#endif
        free(group->tcp_pcbinfo->hashbase, M_PCB);
        _Workspace_Free(group->tcp_pcbinfo);
        _Workspace_Free(group->tcp_pcblist);
        free(group->udp_pcbinfo->hashbase, M_PCB);
        _Workspace_Free(group->udp_pcbinfo);
        _Workspace_Free(group->udp_pcblist);
        _Workspace_Free(group);
        return NULL;
    }
    memset(group->ifnet_addrs, 0, group->if_indexlim * sizeof(struct ifaddr *));
    
    /* 初始化 IPv4 地址链表 */
    group->in_ifaddr = NULL;
    
    // 初始化路由表为NULL,将在第一次访问时延迟初始化 */
    for (int i = 0; i < 32; i++) {
        group->rt_tables[i] = NULL;
    }
    
    /* 注释掉自动创建veth,由应用程序手动创建veth对 */
    /* group->veth_sc = veth_create("veth0", group); */
    group->veth_sc = NULL;  /* 初始化为NULL */
    
    return group;
}
// 释放net_group
static void net_group_free(net_group *group)
{
    if (group == NULL) {
        return;
    }
    
#ifdef RTEMSCFG_Carousel_CONTAINER
    /* 清理Carousel调度器 */
    if (group->carousel != NULL) {
        carousel_destroy(group->carousel);
        group->carousel = NULL;
    }
#endif
    
    // 释放 TCP PCB
    if (group->tcp_pcbinfo) {
        if (group->tcp_pcbinfo->hashbase) {
            free(group->tcp_pcbinfo->hashbase, M_PCB);
        }
        _Workspace_Free(group->tcp_pcbinfo);
    }
    if (group->tcp_pcblist) {
        _Workspace_Free(group->tcp_pcblist);
    }
    
    // 释放 UDP PCB
    if (group->udp_pcbinfo) {
        if (group->udp_pcbinfo->hashbase) {
            free(group->udp_pcbinfo->hashbase, M_PCB);
        }
        _Workspace_Free(group->udp_pcbinfo);
    }
    if (group->udp_pcblist) {
        _Workspace_Free(group->udp_pcblist);
    }
    
    if (group->ifnet_addrs != NULL) {
        _Workspace_Free(group->ifnet_addrs);
    }
    
    _Workspace_Free(group);
}

// 为容器创建独立的 loopback 接口
static int create_loopback_for_container(NetContainer *netContainer)
{
    extern void rtems_bsdnet_initialize_loop_for_container(void *net_group_ptr);
    
    if (!netContainer || !netContainer->group) {
        return -1;
    }
    
    // 为容器创建独立的 loopback 接口，传递整个 NetGroup 结构
    rtems_bsdnet_initialize_loop_for_container(netContainer->group);
    
    printf("容器%d: 创建独立的 loopback 接口\n", netContainer->containerID);
    return 0;
}

// 创建子net容器
NetContainer *rtems_net_container_create(void)
{
    NetContainer *netContainer = (NetContainer *)_Workspace_Allocate(sizeof(NetContainer));
    if (netContainer == NULL)
    {
        return NULL;
    }

    netContainer->rc = 1; // 初始引用计数为1，创建者持有一个引用
    netContainer->containerID = ++g_netContainerId;
    
    // 创建net_group
    netContainer->group = net_group_new();
    if (netContainer->group == NULL) {
        _Workspace_Free(netContainer);
        return NULL;
    }

    printf("创建网络隔离容器: ID=%d\n", netContainer->containerID);

    // 为容器创建独立的 loopback 接口
    if (create_loopback_for_container(netContainer) != 0) {
        printf("警告: 容器%d loopback 接口创建失败\n", netContainer->containerID);
    }

    g_currentNetContainerNum++;
    rtems_net_container_add_to_list(netContainer);

    return netContainer;
}

static bool switch_to_root_net(Thread_Control *thread, void *arg)
{
    NetContainer *netContainer = (NetContainer *)arg;
    Container *container = rtems_container_get_root();
    NetContainer *root = container->netContainer;

    if (thread->container && thread->container->netContainer == netContainer)
    {
        thread->container->netContainer = root;
        root->rc++;
        netContainer->rc--;
        printf("线程ID=0x%08" PRIx32 " 切换到根网络容器\n", thread->Object.id);
    }
    return true;
}
// 删除子net容器
void rtems_net_container_delete(NetContainer *netContainer)
{
    if (!netContainer)
        return;

    Container *container = rtems_container_get_root();
    if (!container || !container->netContainer)
        return;

    NetContainer *root = container->netContainer;
    if (netContainer == root)
        return;

    printf("删除子net容器: ID=%d, rc=%d\n", netContainer->containerID, netContainer->rc);

    // 将所有使用该容器的任务切换到根容器
    rtems_task_iterate(switch_to_root_net, netContainer);

    // 检查当前正在执行的线程是否已转换成功
    Thread_Control *self = (Thread_Control *)_Thread_Get_executing();
    if (self && self->container && self->container->netContainer == netContainer)
    {
        self->container->netContainer = root;
        root->rc++;
        netContainer->rc--;
    }

    // 从链表中移除
    rtems_net_container_remove_from_list(netContainer);

    // 清理ifnet里面的内容
    if (netContainer->group) {
        // 
        struct ifnet *ifp = netContainer->group->ifnet_p;
        if (ifp) {
            // 关闭接口
            if (ifp->if_flags & IFF_UP) {
                ifp->if_flags &= ~IFF_UP;
            }
            
            ifp->if_addrlist = NULL;
            
            memset(&ifp->if_data, 0, sizeof(ifp->if_data));
            
            ifp->if_next = NULL;
        }
        netContainer->group->ifnet_p = NULL;
        
        // 清理ifnet_addrs
        if (netContainer->group->ifnet_addrs) {
            for (int i = 0; i < netContainer->group->if_indexlim; i++) {
                if (netContainer->group->ifnet_addrs[i] != NULL) {
                    netContainer->group->ifnet_addrs[i] = NULL;
                }
            }
        }

        netContainer->group->ifnet_config = NULL;
        netContainer->group->if_index_counter = 0;

        // 释放net_group
        net_group_free(netContainer->group);
        netContainer->group = NULL;
    }

    // 减少全局容器计数
    g_currentNetContainerNum--;
    
    // 释放容器本身
    _Workspace_Free(netContainer);
}

// 将net容器添加到链表中
void rtems_net_container_add_to_list(NetContainer *netContainer)
{
    if (!netContainer)
        return;

    Container *container = rtems_container_get_root();
    if (!container)
        return;

    NetContainerNode **head = (NetContainerNode **)&container->netContainerListHead;
    NetContainerNode *new_node = (NetContainerNode *)_Workspace_Allocate(sizeof(NetContainerNode));
    if (!new_node)
        return;

    new_node->netContainer = netContainer;
    new_node->next = *head;
    *head = new_node;

    printf("添加net容器到链表: ID=%d, 地址=%p\n", netContainer->containerID, (void *)netContainer);    //调试完成之后再注释掉
}

// 从链表中移除net容器
void rtems_net_container_remove_from_list(NetContainer *netContainer)
{
    if (!netContainer)
        return;

    Container *container = rtems_container_get_root();
    if (!container)
        return;

    NetContainerNode **head = (NetContainerNode **)&container->netContainerListHead;
    NetContainerNode *current = *head;
    NetContainerNode *prev = NULL;

    while (current)
    {
        if (current->netContainer == netContainer)
        {
            if (prev)
            {
                prev->next = current->next;
            }
            else
            {
                *head = current->next;
            }

            _Workspace_Free(current);
            printf("从链表中移除net容器: ID=%d, 地址=%p\n", netContainer->containerID, (void *)netContainer);    //调试完成之后再注释掉
            return;
        }
        prev = current;
        current = current->next;
    }
}

// 移动任务到指定的net容器
void rtems_net_container_move_task(NetContainer *srcContainer, NetContainer *destContainer, Thread_Control *thread)
{
    if (!srcContainer || !destContainer || !thread)
        return;

    if (thread->container &&
        (thread->container->netContainer == srcContainer))
    {
        thread->container->netContainer = destContainer;
        srcContainer->rc--;
        if (srcContainer->rc <= 0 && srcContainer->containerID != 1)
        {
            rtems_net_container_delete(srcContainer);
        }
        destContainer->rc++;
    }
    else
    {
        printf("线程ID=0x%08" PRIx32 " 不在源net容器中\n", thread->Object.id);
    }
}

// 获取容器ID
int rtems_net_container_get_id(NetContainer *netContainer)
{
    return netContainer ? netContainer->containerID : -1;
}

// 获取容器引用计数
int rtems_net_container_get_rc(NetContainer *netContainer)
{
    return netContainer ? netContainer->rc : 0;
}

int rtems_net_container_get_NetContainerNum(void)
{
    return g_currentNetContainerNum;
}

struct ifnet *rtems_net_container_get_ifnet(void)
{
    rtems_interrupt_level level;
    rtems_interrupt_disable(level);
    Thread_Control *executing = _Thread_Get_executing();
    rtems_interrupt_enable(level);
    if (executing && executing->container && executing->container->netContainer) {
        NetContainer *netContainer = executing->container->netContainer;
        if (netContainer->group) {
            return netContainer->group->ifnet_p;
        }
    }
    return ifnet;
}

struct ifaddr **rtems_net_container_get_ifnet_addrs(void)
{
    rtems_interrupt_level level;
    rtems_interrupt_disable(level);
    Thread_Control *executing = _Thread_Get_executing();
    rtems_interrupt_enable(level);
    if (executing && executing->container && executing->container->netContainer) {
        NetContainer *netContainer = executing->container->netContainer;
        if (netContainer->group) {
            return netContainer->group->ifnet_addrs;
        }
    }
    return ifnet_addrs;
}

#ifdef RTEMSCFG_Carousel_CONTAINER
/* ========== Carousel深度集成API实现 ========== */

/* 为网络容器启用Carousel流调度器 */
int rtems_net_container_enable_carousel(
  NetContainer *netContainer,
  uint64_t default_rate_bps
)
{
    if (netContainer == NULL || netContainer->group == NULL) {
        return -1;
    }
    
    net_group *group = netContainer->group;
    
    /* 如果已经启用，先禁用 */
    if (group->carousel != NULL) {
        printf("Carousel already enabled for container %d, recreating...\n", 
               netContainer->containerID);
        carousel_destroy(group->carousel);
        group->carousel = NULL;
    }
    
    /* 使用默认值或指定值 */
    uint64_t rate = (default_rate_bps > 0) ? default_rate_bps : (10 * 1000000);
    
    /* 初始化Carousel */
    group->carousel = carousel_init(rate);
    if (group->carousel == NULL) {
        printf("Failed to initialize Carousel for container %d\n", 
               netContainer->containerID);
        group->carousel_enabled = false;
        return -1;
    }
    
    group->carousel_default_rate = rate;
    group->carousel_enabled = true;
    
    printf("Carousel enabled for container %d (default rate: %llu bps)\n", 
           netContainer->containerID, (unsigned long long)rate);
    
    return 0;
}

/* 禁用容器的Carousel调度器 */
void rtems_net_container_disable_carousel(NetContainer *netContainer)
{
    if (netContainer == NULL || netContainer->group == NULL) {
        return;
    }
    
    net_group *group = netContainer->group;
    
    if (group->carousel != NULL) {
        printf("Disabling Carousel for container %d\n", netContainer->containerID);
        carousel_destroy(group->carousel);
        group->carousel = NULL;
        group->carousel_enabled = false;
    }
}

/* 检查容器是否启用了Carousel */
bool rtems_net_container_is_carousel_enabled(NetContainer *netContainer)
{
    if (netContainer == NULL || netContainer->group == NULL) {
        return false;
    }
    
    return netContainer->group->carousel_enabled && 
           (netContainer->group->carousel != NULL);
}

/* 为容器中的特定流设置速率 */
int rtems_net_container_set_flow_rate(
  NetContainer *netContainer,
  uint32_t src_ip,
  uint32_t dst_ip,
  uint16_t src_port,
  uint16_t dst_port,
  uint8_t protocol,
  uint64_t rate_bps
)
{
    if (netContainer == NULL || netContainer->group == NULL) {
        return -1;
    }
    
    if (!netContainer->group->carousel_enabled || 
        netContainer->group->carousel == NULL) {
        printf("Carousel not enabled for container %d\n", 
               netContainer->containerID);
        return -1;
    }
    
    return carousel_set_flow_rate(
        netContainer->group->carousel,
        src_ip, dst_ip, src_port, dst_port, protocol,
        rate_bps
    );
}

/* 获取容器的Carousel统计信息 */
int rtems_net_container_get_carousel_stats(
  NetContainer *netContainer,
  uint32_t *total_flows,
  uint64_t *total_packets_sent,
  uint64_t *total_packets_dropped
)
{
    if (netContainer == NULL || netContainer->group == NULL) {
        return -1;
    }
    
    if (!netContainer->group->carousel_enabled || 
        netContainer->group->carousel == NULL) {
        return -1;
    }
    
    carousel_get_stats(
        netContainer->group->carousel,
        total_flows,
        total_packets_sent,
        total_packets_dropped
    );
    
    return 0;
}

#endif /* RTEMSCFG_Carousel_CONTAINER */

/* 获取当前容器的 IPv4 地址链表 */
struct in_ifaddr **rtems_net_container_get_in_ifaddr(void)
{
    Thread_Control *executing = _Thread_Get_executing();
    
    /* 如果当前线程没有容器上下文,返回全局地址链表 */
    if (executing == NULL || 
        executing->container == NULL || 
        executing->container->netContainer == NULL ||
        executing->container->netContainer->group == NULL) {
        /* 返回全局地址链表的地址 */
        extern struct in_ifaddr *in_ifaddr;
        return &in_ifaddr;
    }
    
    /* 返回容器的地址链表 */
    return &(executing->container->netContainer->group->in_ifaddr);
}

/* 获取当前容器的路由表 */
struct radix_node_head **rtems_net_container_get_rt_tables(void)
{
    Thread_Control *executing = _Thread_Get_executing();
    
    // 如果当前线程没有容器上下文,返回全局路由表
    if (executing == NULL || 
        executing->container == NULL || 
        executing->container->netContainer == NULL ||
        executing->container->netContainer->group == NULL) {

        extern struct radix_node_head *rt_tables[];
        return rt_tables;
    }
    
    net_group *group = executing->container->netContainer->group;
    

    if (group->rt_tables[2] == NULL) {
        extern struct domain *domains;
        struct domain *dom;

        for (dom = domains; dom; dom = dom->dom_next) {
            if (dom->dom_rtattach && group->rt_tables[dom->dom_family] == NULL) {
                dom->dom_rtattach((void *)&group->rt_tables[dom->dom_family],
                                  dom->dom_rtoffset);
            }
        }
    }
    
    return group->rt_tables;
}

/* 获取当前容器的 UDP PCB 链表 */
struct inpcbhead *rtems_net_container_get_udp_pcblist(void)
{
    Thread_Control *executing = _Thread_Get_executing();
    
    // 如果当前线程没有容器上下文,返回全局 PCB 链表
    if (executing == NULL || 
        executing->container == NULL || 
        executing->container->netContainer == NULL ||
        executing->container->netContainer->group == NULL) {
        extern struct inpcbhead udb;
        return &udb;
    }
    
    return executing->container->netContainer->group->udp_pcblist;
}

/* 获取当前容器的 UDP PCB info */
struct inpcbinfo *rtems_net_container_get_udp_pcbinfo(void)
{
    Thread_Control *executing = _Thread_Get_executing();
    
    // 如果当前线程没有容器上下文,返回全局 PCB info
    if (executing == NULL || 
        executing->container == NULL || 
        executing->container->netContainer == NULL ||
        executing->container->netContainer->group == NULL) {
        extern struct inpcbinfo udbinfo;
        return &udbinfo;
    }
    
    return executing->container->netContainer->group->udp_pcbinfo;
}