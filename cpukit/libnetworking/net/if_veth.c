/*
 * Virtual Ethernet Interface Driver for RTEMS Network Containers
 * 
 * This driver implements virtual ethernet interfaces that work in pairs,
 * enabling inter-container communication through packet forwarding.
 */

#include <string.h>
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/socket.h>
#include <sys/sockio.h>
#include <sys/errno.h>
#include <sys/ioctl.h>

#include <net/if.h>
#include <net/if_var.h>
#include <net/if_types.h>
#include <net/route.h>
#include <net/if_veth.h>

#include <netinet/in.h>
#include <netinet/in_var.h>
#include <netinet/if_ether.h>
#include <net/ethernet.h>

#include <rtems.h>
#include <rtems/rtems_bsdnet.h>

#ifdef RTEMSCFG_NET_CONTAINER
#include <rtems/score/netContainer.h>
#include <rtems/score/threadimpl.h>
#include <rtems/score/container.h>
#endif

/*
 * Mbuf Queue Operations
 */

int mbuf_queue_init(struct mbuf_queue *queue, int max_size)
{
    if (queue == NULL || max_size <= 0) {
        return -1;
    }
    
    TAILQ_INIT(&queue->head);
    queue->count = 0;
    queue->max_size = max_size;
    
    /* Initialize ISR spinlock (no semaphore needed) */
    _ISR_lock_Initialize(&queue->lock, "veth queue");
    
    return 0;
}

int mbuf_queue_enqueue(struct mbuf_queue *queue, struct mbuf *m)
{
    struct mbuf_queue_entry *entry;
    ISR_lock_Context lock_context;
    
    if (queue == NULL || m == NULL) {
        return -1;
    }
    
    /* Acquire ISR lock */
    _ISR_lock_ISR_disable_and_acquire(&queue->lock, &lock_context);
    
    if (0) {
        return -1;
    }
    
    /* Check if queue is full */
    if (queue->count >= queue->max_size) {
        _ISR_lock_Release_and_ISR_enable(&queue->lock, &lock_context);
        m_freem(m);  /* Drop the packet */
        return -1;
    }
    
    /* Allocate queue entry */
    entry = (struct mbuf_queue_entry *)malloc(sizeof(*entry), M_DEVBUF, M_NOWAIT);
    if (entry == NULL) {
        _ISR_lock_Release_and_ISR_enable(&queue->lock, &lock_context);
        m_freem(m);
        return -1;
    }
    
    entry->m = m;
    TAILQ_INSERT_TAIL(&queue->head, entry, entries);
    queue->count++;
    
    /* Release lock */
    _ISR_lock_Release_and_ISR_enable(&queue->lock, &lock_context);
    
    return 0;
}

struct mbuf *mbuf_queue_dequeue(struct mbuf_queue *queue)
{
    struct mbuf_queue_entry *entry;
    struct mbuf *m;
    ISR_lock_Context lock_context;
    
    if (queue == NULL) {
        return NULL;
    }
    
    /* Acquire ISR lock */
    _ISR_lock_ISR_disable_and_acquire(&queue->lock, &lock_context);
    
    /* Check if queue is empty */
    entry = TAILQ_FIRST(&queue->head);
    if (entry == NULL) {
        _ISR_lock_Release_and_ISR_enable(&queue->lock, &lock_context);
        return NULL;
    }
    
    /* Remove entry from queue */
    TAILQ_REMOVE(&queue->head, entry, entries);
    queue->count--;
    m = entry->m;
    
    /* Release lock */
    _ISR_lock_Release_and_ISR_enable(&queue->lock, &lock_context);
    
    /* Free the entry structure */
    free(entry, M_DEVBUF);
    
    return m;
}

void mbuf_queue_destroy(struct mbuf_queue *queue)
{
    struct mbuf *m;
    
    if (queue == NULL) {
        return;
    }
    
    /* Dequeue and free all pending packets */
    while ((m = mbuf_queue_dequeue(queue)) != NULL) {
        m_freem(m);
    }
    
    /* ISR lock doesn't need explicit destruction */
    _ISR_lock_Destroy(&queue->lock);
}

/*
 * Generate random MAC address for veth interface
 * Format: 02:00:00:xx:xx:xx (locally administered unicast)
 */
void veth_generate_mac(unsigned char *mac)
{
    static unsigned int counter = 0;
    rtems_interval ticks;
    
    /* Get current ticks for randomization */
    ticks = rtems_clock_get_ticks_since_boot();
    
    mac[0] = 0x02;  /* Locally administered, unicast */
    mac[1] = 0x00;
    mac[2] = 0x00;
    mac[3] = (ticks >> 16) & 0xFF;
    mac[4] = (ticks >> 8) & 0xFF;
    mac[5] = (ticks ^ counter++) & 0xFF;
}

/*
 * Veth receiver task
 * Continuously processes packets from the receive queue
 */
void veth_rx_task(rtems_task_argument arg)
{
    struct veth_softc *sc = (struct veth_softc *)arg;
    struct mbuf *m;
    struct ifnet *ifp = &sc->arpcom.ac_if;
    
    while (sc->running) {
        /* Dequeue packet from receive queue */
        m = mbuf_queue_dequeue(&sc->rx_queue);
        
        if (m != NULL) {
            struct ether_header *eh;
            
#ifdef RTEMSCFG_NET_CONTAINER
            /* Temporarily switch to interface's container context */
            Thread_Control *executing = _Thread_Get_executing();
            Container *saved_container = NULL;
            
            if (sc->container) {
                saved_container = executing->container;
                /* Create temporary container structure pointing to veth's net container */
                Container temp_container;
                memset(&temp_container, 0, sizeof(temp_container));
                temp_container.netContainer = (NetContainer *)sc->container;
                executing->container = &temp_container;
            }
#endif
            
            /* Extract ethernet header from mbuf */
            eh = mtod(m, struct ether_header *);
            
            /* Update statistics */
            sc->rx_packets++;
            sc->rx_bytes += m->m_pkthdr.len;
            ifp->if_ipackets++;
            ifp->if_ibytes += m->m_pkthdr.len;
            
            /* Set the receiving interface */
            m->m_pkthdr.rcvif = ifp;
            
            /* Adjust mbuf to skip ethernet header */
            m->m_data += sizeof(struct ether_header);
            m->m_len -= sizeof(struct ether_header);
            m->m_pkthdr.len -= sizeof(struct ether_header);
            
            /* Pass packet to upper layers */
            ether_input(ifp, eh, m);
            
#ifdef RTEMSCFG_NET_CONTAINER
            /* Restore original container context */
            if (sc->container) {
                executing->container = saved_container;
            }
#endif
        } else {
            /* No packets available, sleep briefly */
            rtems_task_wake_after(1);  /* Sleep for 1 tick */
        }
    }
    
    /* Task termination */
    rtems_task_delete(RTEMS_SELF);
}

/*
 * Veth start routine - transmit packets
 */
void veth_start(struct ifnet *ifp)
{
    struct veth_softc *sc = (struct veth_softc *)ifp->if_softc;
    struct mbuf *m;
    int len;
    
    if (!sc->running || sc->peer == NULL) {
        return;
    }
    
    /* Process all packets in the send queue */
    while (1) {
        IF_DEQUEUE(&ifp->if_snd, m);
        if (m == NULL) {
            break;
        }
        
        len = m->m_pkthdr.len;
        
        /* Enqueue packet to peer's receive queue */
        if (mbuf_queue_enqueue(&sc->peer->rx_queue, m) == 0) {
            /* Update transmit statistics */
            sc->tx_packets++;
            sc->tx_bytes += len;
            ifp->if_opackets++;
            ifp->if_obytes += len;
        } else {
            /* Queue full, drop packet */
            sc->tx_dropped++;
            ifp->if_oerrors++;
            printf("WARNING: veth%d: peer queue full, packet dropped\n", ifp->if_unit);
            /* mbuf already freed in mbuf_queue_enqueue */
        }
    }
}

/*
 * Veth initialization routine
 */
void veth_init(void *arg)
{
    struct veth_softc *sc = (struct veth_softc *)arg;
    struct ifnet *ifp = &sc->arpcom.ac_if;
    rtems_status_code status;
    
    /* Mark interface as running */
    sc->running = 1;
    ifp->if_flags |= IFF_RUNNING;
    ifp->if_flags &= ~IFF_OACTIVE;
    
    /* Create receiver task if not already created */
    if (sc->rx_task_id == 0) {
        status = rtems_task_create(
            rtems_build_name('V', 'E', 'R', 'X'),
            100,                    /* Priority */
            RTEMS_MINIMUM_STACK_SIZE * 4,
            RTEMS_DEFAULT_MODES,
            RTEMS_DEFAULT_ATTRIBUTES,
            &sc->rx_task_id
        );
        
        if (status == RTEMS_SUCCESSFUL) {
            status = rtems_task_start(
                sc->rx_task_id,
                veth_rx_task,
                (rtems_task_argument)sc
            );
            
            if (status != RTEMS_SUCCESSFUL) {
                printf("veth: Failed to start receiver task\n");
                sc->rx_task_id = 0;
            }
        } else {
            printf("veth: Failed to create receiver task\n");
        }
    }
}

/*
 * Veth ioctl handler
 */
int veth_ioctl(struct ifnet *ifp, ioctl_command_t command, caddr_t data)
{
    struct veth_softc *sc = ifp->if_softc;
    struct ifreq *ifr = (struct ifreq *)data;
    int error = 0;
    int s;
    
    s = splimp();
    
    switch (command) {
    case SIOCSIFADDR:
    case SIOCGIFADDR:
        /* Handle address changes */
        error = ether_ioctl(ifp, command, data);
        
        /* Save container context when IP address is configured */
        if (command == SIOCSIFADDR && error == 0) {
#ifdef RTEMSCFG_NET_CONTAINER
            Thread_Control *executing = _Thread_Get_executing();
            if (executing && executing->container && executing->container->netContainer) {
                sc->container = executing->container->netContainer;
            }
#endif
        }
        break;
        
    case SIOCSIFFLAGS:
        /* Handle interface flags */
        if (ifp->if_flags & IFF_UP) {
            if (!(ifp->if_flags & IFF_RUNNING)) {
                veth_init(sc);
            }
        } else {
            if (ifp->if_flags & IFF_RUNNING) {
                /* Shut down the interface */
                sc->running = 0;
                ifp->if_flags &= ~IFF_RUNNING;
            }
        }
        break;
        
    case SIOCSIFMTU:
        /* Set MTU */
        if (ifr->ifr_mtu < 68 || ifr->ifr_mtu > 65535) {
            error = EINVAL;
        } else {
            ifp->if_mtu = ifr->ifr_mtu;
        }
        break;
        
    case SIOCADDMULTI:
    case SIOCDELMULTI:
        /* Veth doesn't really need multicast support, but accept it */
        error = 0;
        break;
        
    default:
        error = EINVAL;
        break;
    }
    
    splx(s);
    return error;
}

/*
 * Create a new veth interface
 */
struct veth_softc *veth_create(const char *name, void *container_group)
{
    struct veth_softc *sc;
    struct ifnet *ifp;
    unsigned char mac[6];
    
    if (name == NULL) {
        return NULL;
    }
    
    /* Allocate softc structure */
    sc = (struct veth_softc *)malloc(sizeof(*sc), M_DEVBUF, M_NOWAIT);
    if (sc == NULL) {
        printf("veth: Failed to allocate softc\n");
        return NULL;
    }
    memset(sc, 0, sizeof(*sc));
    
    /* Initialize receive queue */
    if (mbuf_queue_init(&sc->rx_queue, VETH_QUEUE_SIZE) != 0) {
        printf("veth: Failed to initialize receive queue\n");
        free(sc, M_DEVBUF);
        return NULL;
    }
    
    /* Initialize interface structure */
    ifp = &sc->arpcom.ac_if;
    ifp->if_softc = sc;
    ifp->if_name = "veth";
    ifp->if_unit = name[4] - '0';  /* Extract number from "vethX" */
    ifp->if_mtu = 1500;
    ifp->if_flags = IFF_BROADCAST | IFF_SIMPLEX | IFF_MULTICAST;
    ifp->if_init = veth_init;
    ifp->if_start = veth_start;
    ifp->if_ioctl = veth_ioctl;
    ifp->if_output = ether_output;
    ifp->if_type = IFT_ETHER;
    ifp->if_addrlen = 6;
    ifp->if_hdrlen = 14;
    
    /* Generate MAC address */
    veth_generate_mac(mac);
    memcpy(sc->arpcom.ac_enaddr, mac, 6);
    
    /* Initialize send queue */
    ifp->if_snd.ifq_maxlen = VETH_QUEUE_SIZE;
    
    /* Attach interface to the network stack */
#ifdef RTEMSCFG_NET_CONTAINER
    if (container_group != NULL) {
        /* Attach to container's network group */
        struct net_group *group = (struct net_group *)container_group;
        ifp->if_next = group->ifnet_p;
        group->ifnet_p = ifp;
    } else {
        /* Attach to global interface list */
        if_attach(ifp);
    }
#else
    if_attach(ifp);
#endif
    
    /* Attach ethernet layer */
    ether_ifattach(ifp);
    
    printf("veth: Created interface %s%d with MAC %02x:%02x:%02x:%02x:%02x:%02x\n",
           ifp->if_name, ifp->if_unit,
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    
    return sc;
}

/*
 * Set up a veth pair
 */
int veth_set_pair(struct veth_softc *veth1, struct veth_softc *veth2)
{
    if (veth1 == NULL || veth2 == NULL) {
        return -1;
    }
    
    if (veth1 == veth2) {
        printf("veth: Cannot pair interface with itself\n");
        return -1;
    }
    
    /* Set up bidirectional peer relationship */
    veth1->peer = veth2;
    veth2->peer = veth1;
    
    printf("veth: Paired %s%d <-> %s%d\n",
           veth1->arpcom.ac_if.if_name, veth1->arpcom.ac_if.if_unit,
           veth2->arpcom.ac_if.if_name, veth2->arpcom.ac_if.if_unit);
    
    return 0;
}

/*
 * Destroy a veth interface
 */
void veth_destroy(struct veth_softc *sc)
{
    struct ifnet *ifp;
    
    if (sc == NULL) {
        return;
    }
    
    ifp = &sc->arpcom.ac_if;
    
    /* Shut down the interface */
    sc->running = 0;
    
    /* Wait for receiver task to terminate */
    if (sc->rx_task_id != 0) {
        rtems_task_wake_after(10);  /* Give task time to exit */
        sc->rx_task_id = 0;
    }
    
    /* Break peer relationship */
    if (sc->peer != NULL) {
        sc->peer->peer = NULL;
        sc->peer = NULL;
    }
    
    /* Detach interface - 暂时注释掉,因为函数不存在 */
    /* if_detach(ifp); */
    
    /* Destroy receive queue */
    mbuf_queue_destroy(&sc->rx_queue);
    
    /* Free softc structure */
    free(sc, M_DEVBUF);
    
    printf("veth: Destroyed interface %s%d\n", ifp->if_name, ifp->if_unit);
}
