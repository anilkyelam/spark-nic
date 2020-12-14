/*
 * An example RDMA client side code. 
 * Author: Animesh Trivedi 
 *         atrivedi@apache.org
 */

#include "rdma_common.h"
#include <sys/time.h>

/* These are basic RDMA resources */
/* These are RDMA connection related resources */
static struct rdma_event_channel *cm_event_channel = NULL;
static struct rdma_cm_id *cm_client_id = NULL;
static struct ibv_pd *pd = NULL;
static struct ibv_comp_channel *io_completion_channel = NULL;
static struct ibv_cq *client_cq = NULL;
static struct ibv_qp_init_attr qp_init_attr;
static struct ibv_qp *client_qp;
/* These are memory buffers related resources */
static struct ibv_mr *client_metadata_mr = NULL, 
             *client_src_mr = NULL, 
             *client_dst_mr = NULL, 
             *server_metadata_mr = NULL;
static struct rdma_buffer_attr client_metadata_attr, server_metadata_attr;
static struct ibv_send_wr client_send_wr, *bad_client_send_wr = NULL;
static struct ibv_recv_wr server_recv_wr, *bad_server_recv_wr = NULL;
static struct ibv_sge client_send_sge, server_recv_sge;
/* Source and Destination buffers, where RDMA operations source and sink */
static char *src = NULL, *dst = NULL; 


/* Rdtsc blocks for time measurements */
unsigned cycles_low, cycles_high, cycles_low1, cycles_high1;

static __inline__ unsigned long long rdtsc(void)
{
   __asm__ __volatile__ ("RDTSC\n\t"
            "mov %%edx, %0\n\t"
            "mov %%eax, %1\n\t": "=r" (cycles_high), "=r" (cycles_low)::
            "%rax", "rbx", "rcx", "rdx");
}

static __inline__ unsigned long long rdtsc1(void)
{
   __asm__ __volatile__ ("RDTSC\n\t"
            "mov %%edx, %0\n\t"
            "mov %%eax, %1\n\t": "=r" (cycles_high1), "=r" (cycles_low1)::
            "%rax", "rbx", "rcx", "rdx");
}

/* This is our testing function */
static int check_src_dst() 
{
    return memcmp((void*) src, (void*) dst, strlen(src));
}

/* This function prepares client side connection resources for an RDMA connection */
static int client_prepare_connection(struct sockaddr_in *s_addr)
{
    struct rdma_cm_event *cm_event = NULL;
    int ret = -1;
    /*  Open a channel used to report asynchronous communication event */
    cm_event_channel = rdma_create_event_channel();
    if (!cm_event_channel) {
        rdma_error("Creating cm event channel failed, errno: %d \n", -errno);
        return -errno;
    }
    debug("RDMA CM event channel is created at : %p \n", cm_event_channel);
    /* rdma_cm_id is the connection identifier (like socket) which is used 
     * to define an RDMA connection. 
     */
    ret = rdma_create_id(cm_event_channel, &cm_client_id, 
            NULL,
            RDMA_PS_TCP);
    if (ret) {
        rdma_error("Creating cm id failed with errno: %d \n", -errno); 
        return -errno;
    }
    /* Resolve destination and optional source addresses from IP addresses  to
     * an RDMA address.  If successful, the specified rdma_cm_id will be bound
     * to a local device. */
    ret = rdma_resolve_addr(cm_client_id, NULL, (struct sockaddr*) s_addr, 2000);
    if (ret) {
        rdma_error("Failed to resolve address, errno: %d \n", -errno);
        return -errno;
    }
    debug("waiting for cm event: RDMA_CM_EVENT_ADDR_RESOLVED\n");
    ret  = process_rdma_cm_event(cm_event_channel, 
            RDMA_CM_EVENT_ADDR_RESOLVED,
            &cm_event);
    if (ret) {
        rdma_error("Failed to receive a valid event, ret = %d \n", ret);
        return ret;
    }
    /* we ack the event */
    ret = rdma_ack_cm_event(cm_event);
    if (ret) {
        rdma_error("Failed to acknowledge the CM event, errno: %d\n", -errno);
        return -errno;
    }
    debug("RDMA address is resolved \n");

     /* Resolves an RDMA route to the destination address in order to 
      * establish a connection */
    ret = rdma_resolve_route(cm_client_id, 2000);
    if (ret) {
        rdma_error("Failed to resolve route, erno: %d \n", -errno);
           return -errno;
    }
    debug("waiting for cm event: RDMA_CM_EVENT_ROUTE_RESOLVED\n");
    ret = process_rdma_cm_event(cm_event_channel, 
            RDMA_CM_EVENT_ROUTE_RESOLVED,
            &cm_event);
    if (ret) {
        rdma_error("Failed to receive a valid event, ret = %d \n", ret);
        return ret;
    }
    /* we ack the event */
    ret = rdma_ack_cm_event(cm_event);
    if (ret) {
        rdma_error("Failed to acknowledge the CM event, errno: %d \n", -errno);
        return -errno;
    }
    printf("Trying to connect to server at : %s port: %d \n", 
            inet_ntoa(s_addr->sin_addr),
            ntohs(s_addr->sin_port));
    /* Protection Domain (PD) is similar to a "process abstraction" 
     * in the operating system. All resources are tied to a particular PD. 
     * And accessing recourses across PD will result in a protection fault.
     */
    pd = ibv_alloc_pd(cm_client_id->verbs);
    if (!pd) {
        rdma_error("Failed to alloc pd, errno: %d \n", -errno);
        return -errno;
    }
    debug("pd allocated at %p \n", pd);
    /* Now we need a completion channel, were the I/O completion 
     * notifications are sent. Remember, this is different from connection 
     * management (CM) event notifications. 
     * A completion channel is also tied to an RDMA device, hence we will 
     * use cm_client_id->verbs. 
     */
    io_completion_channel = ibv_create_comp_channel(cm_client_id->verbs);
    if (!io_completion_channel) {
        rdma_error("Failed to create IO completion event channel, errno: %d\n",
                   -errno);
    return -errno;
    }
    debug("completion event channel created at : %p \n", io_completion_channel);
    /* Now we create a completion queue (CQ) where actual I/O 
     * completion metadata is placed. The metadata is packed into a structure 
     * called struct ibv_wc (wc = work completion). ibv_wc has detailed 
     * information about the work completion. An I/O request in RDMA world 
     * is called "work" ;) 
     */
    client_cq = ibv_create_cq(cm_client_id->verbs /* which device*/, 
            CQ_CAPACITY /* maximum capacity*/, 
            NULL /* user context, not used here */,
            io_completion_channel /* which IO completion channel */, 
            0 /* signaling vector, not used here*/);
    if (!client_cq) {
        rdma_error("Failed to create CQ, errno: %d \n", -errno);
        return -errno;
    }
    debug("CQ created at %p with %d elements \n", client_cq, client_cq->cqe);
    ret = ibv_req_notify_cq(client_cq, 0);
    if (ret) {
        rdma_error("Failed to request notifications, errno: %d\n", -errno);
        return -errno;
    }
       /* Now the last step, set up the queue pair (send, recv) queues and their capacity.
         * The capacity here is define statically but this can be probed from the 
     * device. We just use a small number as defined in rdma_common.h */
       bzero(&qp_init_attr, sizeof qp_init_attr);
       qp_init_attr.cap.max_recv_sge = MAX_SGE; /* Maximum SGE per receive posting */
       qp_init_attr.cap.max_recv_wr = MAX_WR; /* Maximum receive posting capacity */
       qp_init_attr.cap.max_send_sge = MAX_SGE; /* Maximum SGE per send posting */
       qp_init_attr.cap.max_send_wr = MAX_WR; /* Maximum send posting capacity */
       qp_init_attr.qp_type = IBV_QPT_RC; /* QP type, RC = Reliable connection */
       /* We use same completion queue, but one can use different queues */
       qp_init_attr.recv_cq = client_cq; /* Where should I notify for receive completion operations */
       qp_init_attr.send_cq = client_cq; /* Where should I notify for send completion operations */
       /*Lets create a QP */
       ret = rdma_create_qp(cm_client_id /* which connection id */,
               pd /* which protection domain*/,
               &qp_init_attr /* Initial attributes */);
    if (ret) {
        rdma_error("Failed to create QP, errno: %d \n", -errno);
           return -errno;
    }
    client_qp = cm_client_id->qp;
    debug("QP created at %p \n", client_qp);
    return 0;
}

/* Pre-posts a receive buffer before calling rdma_connect () */
static int client_pre_post_recv_buffer()
{
    int ret = -1;
    server_metadata_mr = rdma_buffer_register(pd,
            &server_metadata_attr,
            sizeof(server_metadata_attr),
            (IBV_ACCESS_LOCAL_WRITE));
    if(!server_metadata_mr){
        rdma_error("Failed to setup the server metadata mr , -ENOMEM\n");
        return -ENOMEM;
    }
    server_recv_sge.addr = (uint64_t) server_metadata_mr->addr;
    server_recv_sge.length = (uint32_t) server_metadata_mr->length;
    server_recv_sge.lkey = (uint32_t) server_metadata_mr->lkey;
    /* now we link it to the request */
    bzero(&server_recv_wr, sizeof(server_recv_wr));
    server_recv_wr.sg_list = &server_recv_sge;
    server_recv_wr.num_sge = 1;
    ret = ibv_post_recv(client_qp /* which QP */,
              &server_recv_wr /* receive work request*/,
              &bad_server_recv_wr /* error WRs */);
    if (ret) {
        rdma_error("Failed to pre-post the receive buffer, errno: %d \n", ret);
        return ret;
    }
    debug("Receive buffer pre-posting is successful \n");
    return 0;
}

/* Connects to the RDMA server */
static int client_connect_to_server() 
{
    struct rdma_conn_param conn_param;
    struct rdma_cm_event *cm_event = NULL;
    int ret = -1;
    bzero(&conn_param, sizeof(conn_param));
    conn_param.initiator_depth = 3;
    conn_param.responder_resources = 3;
    conn_param.retry_count = 3; // if fail, then how many times to retry
    ret = rdma_connect(cm_client_id, &conn_param);
    if (ret) {
        rdma_error("Failed to connect to remote host , errno: %d\n", -errno);
        return -errno;
    }
    debug("waiting for cm event: RDMA_CM_EVENT_ESTABLISHED\n");
    ret = process_rdma_cm_event(cm_event_channel, 
            RDMA_CM_EVENT_ESTABLISHED,
            &cm_event);
    if (ret) {
        rdma_error("Failed to get cm event, ret = %d \n", ret);
           return ret;
    }
    ret = rdma_ack_cm_event(cm_event);
    if (ret) {
        rdma_error("Failed to acknowledge cm event, errno: %d\n", 
                   -errno);
        return -errno;
    }
    printf("The client is connected successfully \n");
    return 0;
}

/* Set up buffers to exchange data with the server. The client sends its, and then receives
 * from the server. The client-side metadata on the server is _not_ used because
 * this program is client driven. But it shown here how to do it for the illustration
 * purposes.
 * buffer: register as client buffer if provided.
 * buffer_size: if buffer is not provided, allocate buffers of this size on both client and server. 
 */
static int client_xchange_metadata_with_server(char* buffer, uint32_t buffer_size)
{
    struct ibv_wc wc[2];
    int ret = -1;
    
    client_src_mr = 
        buffer == NULL ? 
            rdma_buffer_alloc(pd,
                buffer_size,
                (IBV_ACCESS_LOCAL_WRITE|
                IBV_ACCESS_REMOTE_READ|
                IBV_ACCESS_REMOTE_WRITE)) :
            rdma_buffer_register(pd,
                buffer,
                strlen(buffer),
                (IBV_ACCESS_LOCAL_WRITE|
                IBV_ACCESS_REMOTE_READ|
                IBV_ACCESS_REMOTE_WRITE));;
    if(!client_src_mr){
        rdma_error("Failed to register the first buffer, ret = %d \n", ret);
        return ret;
    }

    /* we prepare metadata for the first buffer */
    client_metadata_attr.address = (uint64_t) client_src_mr->addr; 
    client_metadata_attr.length = client_src_mr->length; 
    client_metadata_attr.stag.local_stag = client_src_mr->lkey;
    /* now we register the metadata memory */
    client_metadata_mr = rdma_buffer_register(pd,
            &client_metadata_attr,
            sizeof(client_metadata_attr),
            IBV_ACCESS_LOCAL_WRITE);
    if(!client_metadata_mr) {
        rdma_error("Failed to register the client metadata buffer, ret = %d \n", ret);
        return ret;
    }
    /* now we fill up SGE */
    client_send_sge.addr = (uint64_t) client_metadata_mr->addr;
    client_send_sge.length = (uint32_t) client_metadata_mr->length;
    client_send_sge.lkey = client_metadata_mr->lkey;
    /* now we link to the send work request */
    bzero(&client_send_wr, sizeof(client_send_wr));
    client_send_wr.sg_list = &client_send_sge;
    client_send_wr.num_sge = 1;
    client_send_wr.opcode = IBV_WR_SEND;
    client_send_wr.send_flags = IBV_SEND_SIGNALED;
    /* Now we post it */
    ret = ibv_post_send(client_qp, 
               &client_send_wr,
           &bad_client_send_wr);
    if (ret) {
        rdma_error("Failed to send client metadata, errno: %d \n", 
                -errno);
        return -errno;
    }
    /* at this point we are expecting 2 work completion. One for our 
     * send and one for recv that we will get from the server for 
     * its buffer information */
    ret = process_work_completion_events(io_completion_channel, 
            wc, 2);
    if(ret != 2) {
        rdma_error("We failed to get 2 work completions , ret = %d \n",
                ret);
        return ret;
    }
    debug("Server sent us its buffer location and credentials, showing \n");
    show_rdma_buffer_attr(&server_metadata_attr);
    return 0;
}

/* This function does :
 * 1) Prepare memory buffers for RDMA operations 
 * 1) RDMA write from src -> remote buffer 
 * 2) RDMA read from remote bufer -> dst
 */ 
static int client_remote_memory_ops() 
{
    struct ibv_wc wc;
    int ret = -1;
    struct timeval start, end;
    long ops_count = 0;
    double duration = 0.0;
    double throughput = 0.0;
    uint64_t start_cycles, end_cycles;

    client_dst_mr = rdma_buffer_register(pd,
            dst,
            strlen(src),
            (IBV_ACCESS_LOCAL_WRITE | 
             IBV_ACCESS_REMOTE_WRITE | 
             IBV_ACCESS_REMOTE_READ));
    if (!client_dst_mr) {
        rdma_error("We failed to create the destination buffer, -ENOMEM\n");
        return -ENOMEM;
    }
    /* Step 1: is to copy the local buffer into the remote buffer. We will 
     * reuse the previous variables. */
    /* now we fill up SGE */
    client_send_sge.addr = (uint64_t) client_src_mr->addr;
    client_send_sge.length = (uint32_t) client_src_mr->length;
    client_send_sge.lkey = client_src_mr->lkey;
    /* now we link to the send work request */
    bzero(&client_send_wr, sizeof(client_send_wr));
    client_send_wr.sg_list = &client_send_sge;
    client_send_wr.num_sge = 1;
    client_send_wr.opcode = IBV_WR_RDMA_WRITE;
    client_send_wr.send_flags = IBV_SEND_SIGNALED;
    /* we have to tell server side info for RDMA */
    client_send_wr.wr.rdma.rkey = server_metadata_attr.stag.remote_stag;
    client_send_wr.wr.rdma.remote_addr = server_metadata_attr.address;


    rdtsc();
    /* Now we post it */
    ret = ibv_post_send(client_qp, 
               &client_send_wr,
           &bad_client_send_wr);
    if (ret) {
        rdma_error("Failed to write client src buffer, errno: %d \n", 
                -errno);
        return -errno;
    }
    /* at this point we are expecting 1 work completion for the write */
    ret = process_work_completion_events(io_completion_channel, 
            &wc, 1);
    if(ret != 1) {
        rdma_error("We failed to get 1 work completions , ret = %d \n",
                ret);
        return ret;
    }
    rdtsc1();

    debug("Client side WRITE is complete \n");
    start_cycles = ( ((int64_t)cycles_high << 32) | cycles_low );
    end_cycles = ( ((int64_t)cycles_high1 << 32) | cycles_low1 );
    double CPU_FREQ = 2.5e9;		// 2.5 GHz, special setting. TODO: magic number.
    printf("Client side WRITE took %lf mu-sec\n", (end_cycles - start_cycles) * 1e6 / CPU_FREQ);

    /* Now we prepare a READ using same variables but for destination */
    client_send_sge.addr = (uint64_t) client_dst_mr->addr;
    client_send_sge.length = (uint32_t) client_dst_mr->length;
    client_send_sge.lkey = client_dst_mr->lkey;
    /* now we link to the send work request */
    bzero(&client_send_wr, sizeof(client_send_wr));
    client_send_wr.sg_list = &client_send_sge;
    client_send_wr.num_sge = 1;
    client_send_wr.opcode = IBV_WR_RDMA_READ;
    client_send_wr.send_flags = IBV_SEND_SIGNALED;
    /* we have to tell server side info for RDMA */
    client_send_wr.wr.rdma.rkey = server_metadata_attr.stag.remote_stag;
    client_send_wr.wr.rdma.remote_addr = server_metadata_attr.address;
    /* Now we post it */
    ret = ibv_post_send(client_qp, 
               &client_send_wr,
           &bad_client_send_wr);
    if (ret) {
        rdma_error("Failed to read client dst buffer from the master, errno: %d \n", 
                -errno);
        return -errno;
    }
    /* at this point we are expecting 1 work completion for the write */
    ret = process_work_completion_events(io_completion_channel, 
            &wc, 1);
    if(ret != 1) {
        rdma_error("We failed to get 1 work completions , ret = %d \n",
                ret);
        return ret;
    }
    debug("Client side READ is complete \n");
    
    // This buffer is local to this method, won't need it anymore.
    rdma_buffer_deregister(client_dst_mr);	
    return 0;
}


/* Measures rtt for RDMA READ/WRITE ops for a specified message size, running multiple trials */
/* Returns RTT in micro-seconds */
static double measure_rtt(uint32_t msg_size, int read_or_write){
    int ret = -1, n;
    struct ibv_wc wc;
    struct timeval start, end;
    long ops_count = 0;
    double duration = 0.0;
    double throughput = 0.0;
    uint64_t start_cycles, end_cycles;
    struct ibv_cq *cq_ptr = NULL;
    void *context = NULL;
    struct ibv_mr *buffer1_mr = NULL;           /* Make sure to deregister these local MRs before exiting */

    /* Allocate a client buffer to write from */
    uint32_t buf_size = 2 * msg_size;
    buffer1_mr = rdma_buffer_alloc(pd,
            buf_size,
            (IBV_ACCESS_LOCAL_WRITE | 
             IBV_ACCESS_REMOTE_WRITE | 
             IBV_ACCESS_REMOTE_READ));
    if (!buffer1_mr) {
        rdma_error("We failed to create the destination buffer, -ENOMEM\n");
        return -ENOMEM;
    }

    // NOTE: May wanna fill the buffer with something other than zeroes
    /* Make WR for writing this buffer */
    client_send_sge.addr = (uint64_t) buffer1_mr->addr;
    client_send_sge.length = (uint32_t) msg_size;           // Send only msg_size
    client_send_sge.lkey = buffer1_mr->lkey;
    /* now we link to the send work request */
    bzero(&client_send_wr, sizeof(client_send_wr));
    client_send_wr.sg_list = &client_send_sge;
    client_send_wr.num_sge = 1;
    client_send_wr.opcode = read_or_write ? IBV_WR_RDMA_READ : IBV_WR_RDMA_WRITE;
    client_send_wr.send_flags = IBV_SEND_SIGNALED;          // NOTE: This tells the other NIC to send completion events
    /* we have to tell server side info for RDMA */
    client_send_wr.wr.rdma.rkey = server_metadata_attr.stag.remote_stag;
    client_send_wr.wr.rdma.remote_addr = server_metadata_attr.address;

    rdtsc();
    /* Now we post it */
    ret = ibv_post_send(client_qp, 
               &client_send_wr,
           &bad_client_send_wr);
    if (ret) {
        rdma_error("Failed to write client src buffer, errno: %d \n", -errno);
        return -errno;
    }

    /* at this point we are expecting 1 work completion for the write */
    /* We wait for the notification on the CQ channel */
    ret = ibv_get_cq_event(io_completion_channel, &cq_ptr, &context);
    if (ret) {
        rdma_error("Failed to get next CQ event due to %d \n", -errno);
        return -errno;
    }

    /* Request for more notifications. */
    ret = ibv_req_notify_cq(cq_ptr, 0);
    if (ret){
        rdma_error("Failed to request further notifications %d \n", -errno);
        return -errno;
    }

    const int NUM_TRIALS = 1e6;     // a million
    int trials = 0;
    uint64_t sum_cycles = 0;
    do {
        /* Poll the completion queue for the completion event for the earlier write */
        do {
            n = ibv_poll_cq(cq_ptr, 1, &wc);
            if (n < 0) {
                rdma_error("Failed to poll cq for wc due to %d \n", ret);
                return ret;     /* ret is errno here */
            }
        } while (n < 1);

        /* Check that the write succeeded */
        if (wc.status != IBV_WC_SUCCESS) {
            rdma_error("Work completion (WC) has error status: %s at index %d",  
                ibv_wc_status_str(wc.status), 0);
            return -(wc.status);       /* return negative value */
        }
        rdtsc1();

        trials++;
        start_cycles = ( ((int64_t)cycles_high << 32) | cycles_low );
        end_cycles = ( ((int64_t)cycles_high1 << 32) | cycles_low1 );
        sum_cycles += (end_cycles - start_cycles);
        if (trials >= NUM_TRIALS)   break;

        /* Post again */
        rdtsc();
        ret = ibv_post_send(client_qp, 
                &client_send_wr,
                &bad_client_send_wr);
        if (ret) {
            rdma_error("Failed to write client src buffer, errno: %d \n", -errno);
            return -errno;
        }
    } while(1);

    /* Similar to connection management events, we need to acknowledge CQ events */
    ibv_ack_cq_events(cq_ptr, 1 /* we received one event notification. This is not number of WC elements */);

    double CPU_FREQ = 2.5e9;		// 2.5 GHz, special setting. TODO: magic number.
    // printf("RTT is %lf mu-sec\n", sum_cycles * 1e6 / (NUM_TRIALS * CPU_FREQ));
    
    // Deregister local MRs
    rdma_buffer_deregister(buffer1_mr);	
    return sum_cycles * 1e6 / (NUM_TRIALS * CPU_FREQ);
}

/* This function disconnects the RDMA connection from the server and cleans up 
 * all the resources.
 */
static int client_disconnect_and_clean()
{
    struct rdma_cm_event *cm_event = NULL;
    int ret = -1;
    /* active disconnect from the client side */
    ret = rdma_disconnect(cm_client_id);
    if (ret) {
        rdma_error("Failed to disconnect, errno: %d \n", -errno);
        //continuing anyways
    }
    ret = process_rdma_cm_event(cm_event_channel, 
            RDMA_CM_EVENT_DISCONNECTED,
            &cm_event);
    if (ret) {
        rdma_error("Failed to get RDMA_CM_EVENT_DISCONNECTED event, ret = %d\n",
                ret);
        //continuing anyways 
    }
    ret = rdma_ack_cm_event(cm_event);
    if (ret) {
        rdma_error("Failed to acknowledge cm event, errno: %d\n", 
                   -errno);
        //continuing anyways
    }
    /* Destroy QP */
    rdma_destroy_qp(cm_client_id);
    /* Destroy client cm id */
    ret = rdma_destroy_id(cm_client_id);
    if (ret) {
        rdma_error("Failed to destroy client id cleanly, %d \n", -errno);
        // we continue anyways;
    }
    /* Destroy CQ */
    ret = ibv_destroy_cq(client_cq);
    if (ret) {
        rdma_error("Failed to destroy completion queue cleanly, %d \n", -errno);
        // we continue anyways;
    }
    /* Destroy completion channel */
    ret = ibv_destroy_comp_channel(io_completion_channel);
    if (ret) {
        rdma_error("Failed to destroy completion channel cleanly, %d \n", -errno);
        // we continue anyways;
    }
    /* Destroy memory buffers */
    rdma_buffer_deregister(server_metadata_mr);
    rdma_buffer_deregister(client_metadata_mr);	
    rdma_buffer_deregister(client_src_mr);	
    /* We free the buffers */
    free(src);
    free(dst);
    /* Destroy protection domain */
    ret = ibv_dealloc_pd(pd);
    if (ret) {
        rdma_error("Failed to destroy client protection domain cleanly, %d \n", -errno);
        // we continue anyways;
    }
    rdma_destroy_event_channel(cm_event_channel);
    printf("Client resource clean up is complete \n");
    return 0;
}

void usage() {
    printf("Usage:\n");
    printf("rdma_client: [-a <server_addr>] [-p <server_port>] [--simple]\n");
    printf("	--simple: runs a simple ping to test RDMA connection\n");
    printf("(default IP is 127.0.0.1 and port is %d)\n", DEFAULT_RDMA_PORT);
    exit(1);
}

int main(int argc, char **argv) {
    struct sockaddr_in server_sockaddr;
    int ret, option;
    bzero(&server_sockaddr, sizeof server_sockaddr);
    server_sockaddr.sin_family = AF_INET;
    server_sockaddr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    /* buffers are NULL */
    char* dummy_text = "HELLO";
    src = dst = NULL; 
    int simple = 0;

    /* Parse Command Line Arguments */
    static const struct option options[] = {
        {.name = "simple", .has_arg = no_argument, .val = 's'},
        {},
    };
    while ((option = getopt_long(argc, argv, "sa:p:", options, NULL)) != -1) {
        switch (option) {
            case 's':
                simple = 1;
                src = calloc(strlen(dummy_text) , 1);
                if (!src) {
                    rdma_error("Failed to allocate memory : -ENOMEM\n");
                    return -ENOMEM;
                }
                /* Copy the passes arguments */
                strncpy(src, dummy_text, strlen(dummy_text));
                dst = calloc(strlen(dummy_text), 1);
                if (!dst) {
                    rdma_error("Failed to allocate destination memory, -ENOMEM\n");
                    free(src);
                    return -ENOMEM;
                }
                break;
            case 'a':
                /* remember, this overwrites the port info */
                ret = get_addr(optarg, (struct sockaddr*) &server_sockaddr);
                if (ret) {
                    rdma_error("Invalid IP \n");
                    return ret;
                }
                break;
            case 'p':
                /* passed port to listen on */
                server_sockaddr.sin_port = htons(strtol(optarg, NULL, 0)); 
                break;
            default:
                usage();
                break;
            }
        }
    if (!server_sockaddr.sin_port) {
        /* no port provided, use the default port */
        server_sockaddr.sin_port = htons(DEFAULT_RDMA_PORT);
    }

    ret = client_prepare_connection(&server_sockaddr);
    if (ret) { 
        rdma_error("Failed to setup client connection , ret = %d \n", ret);
        return ret;
     }
    ret = client_pre_post_recv_buffer(); 
    if (ret) { 
        rdma_error("Failed to setup client connection , ret = %d \n", ret);
        return ret;
    }
    ret = client_connect_to_server();
    if (ret) { 
        rdma_error("Failed to setup client connection , ret = %d \n", ret);
        return ret;
    }

    /* Connection set up and ready to play */
    if (simple) {
        /* If asked for a simple program, run the original example */
        ret = client_xchange_metadata_with_server(src, strlen(src));
        if (ret) {
            rdma_error("Failed to setup client connection , ret = %d \n", ret);
            return ret;
        }

        ret = client_remote_memory_ops();
        if (ret) {
            rdma_error("Failed to finish remote memory ops, ret = %d \n", ret);
            return ret;
        }
            
        if (check_src_dst()) {
            rdma_error("src and dst buffers do not match \n");
        } else {
            printf("...\nSUCCESS, source and destination buffers match \n");
        }
    }
    else {   
        /* Set up a buffer with enough size on server */
        /* Once metadata is exchanged, server-side buffer metadata would be saved in server_metadata_attr */
        ret = client_xchange_metadata_with_server(NULL, 1024*1024);      // 1 MB
        if (ret) {
            rdma_error("Failed to setup client connection , ret = %d \n", ret);
            return ret;
        }
        
        /* Get roundtrip latencies for ops */
        uint32_t msg_size = 64;
        printf("RTT for %d B WRITES: %0.2lf mu-secs\n", msg_size, measure_rtt(msg_size, 0));       // WRITE RTT
        printf("RTT for %d B READS : %0.2lf mu-secs\n", msg_size, measure_rtt(msg_size, 1));       // READ RTT
        for (msg_size = 10; msg_size <= 3000; msg_size += 10) {
            printf("%d,%.2lf,%.2lf\n", msg_size, measure_rtt(msg_size, 0), measure_rtt(msg_size, 1));
        }
    }

    ret = client_disconnect_and_clean();
    if (ret) {
        rdma_error("Failed to cleanly disconnect and clean up resources \n");
    }
    return ret;
}

