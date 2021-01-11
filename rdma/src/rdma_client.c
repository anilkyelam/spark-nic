/*
 * An example RDMA client side code. 
 * Author: Animesh Trivedi 
 *         atrivedi@apache.org
 */

#include "rdma_common.h"
#include <sys/time.h>
#include <assert.h>

/* These are basic RDMA resources */
/* These are RDMA connection related resources */
static struct rdma_event_channel *cm_event_channel = NULL;
static struct rdma_cm_id *cm_client_id = NULL;
static struct ibv_pd *pd = NULL;
static struct ibv_comp_channel *io_completion_channel = NULL;
static struct ibv_cq *client_cq = NULL;
static struct ibv_qp_init_attr qp_init_attr;
static struct ibv_qp *client_qp;
static struct ibv_device_attr dev_attr;
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

unsigned xcycles_low, xcycles_high, xcycles_low1, xcycles_high1;
static __inline__ unsigned long long xrdtsc(void)
{
   __asm__ __volatile__ ("RDTSC\n\t"
            "mov %%edx, %0\n\t"
            "mov %%eax, %1\n\t": "=r" (xcycles_high), "=r" (xcycles_low)::
            "%rax", "rbx", "rcx", "rdx");
}

static __inline__ unsigned long long xrdtsc1(void)
{
   __asm__ __volatile__ ("RDTSC\n\t"
            "mov %%edx, %0\n\t"
            "mov %%eax, %1\n\t": "=r" (xcycles_high1), "=r" (xcycles_low1)::
            "%rax", "rbx", "rcx", "rdx");
}


/* This is our testing function */
static int check_src_dst() 
{
    return memcmp((void*) src, (void*) dst, strlen(src));
}


/* A fast but good enough random number generator. Good enough for what? */
/* Courtesy of https://stackoverflow.com/questions/1640258/need-a-fast-random-generator-for-c */
unsigned long rand_xorshf96(void) {          //period 2^96-1
    static unsigned long x=123456789, y=362436069, z=521288629;
    unsigned long t;
    x ^= x << 16;
    x ^= x >> 5;
    x ^= x << 1;

    t = x;
    x = y;
    y = z;
    z = t ^ x ^ y;
    return z;
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

    ret = ibv_query_device(cm_client_id->verbs, &dev_attr);    
    if (ret) {
        rdma_error("Failed to get device info, errno: %d\n", -errno);
        return -errno;
    }
    printf("got device info. max qpe: %d, sge: %d, cqe: %d, max rd/at qp depth/outstanding: %d/%d max mr size: %lu\n", 
        dev_attr.max_qp_wr, dev_attr.max_sge, dev_attr.max_cqe, dev_attr.max_qp_init_rd_atom, dev_attr.max_qp_rd_atom, 
        dev_attr.max_mr_size);

    /* Now we create a completion queue (CQ) where actual I/O 
     * completion metadata is placed. The metadata is packed into a structure 
     * called struct ibv_wc (wc = work completion). ibv_wc has detailed 
     * information about the work completion. An I/O request in RDMA world 
     * is called "work" ;) 
     */
    client_cq = ibv_create_cq(cm_client_id->verbs /* which device*/, 
        CQ_CAPACITY             /* maximum device capacity*/, 
        NULL                    /* user context, not used here */,
        io_completion_channel   /* which IO completion channel */, 
        0                       /* signaling vector, not used here*/);
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

    /* TODO: Get capacity from device */
    /* Now the last step, set up the queue pair (send, recv) queues and their capacity.
     * Set capacity to device limits (since we use only one qp and one application) 
     * This only sets the limits; this will let us play around with the actual numbers */
    bzero(&qp_init_attr, sizeof qp_init_attr);
    qp_init_attr.cap.max_recv_sge = MAX_SGE;    /* Maximum SGE per receive posting;*/
    qp_init_attr.cap.max_recv_wr = MAX_WR;      /* Maximum receive posting capacity; */
    qp_init_attr.cap.max_send_sge = MAX_SGE;    /* Maximum SGE per send posting;*/
    qp_init_attr.cap.max_send_wr = MAX_WR;      /* Maximum send posting capacity; */
    qp_init_attr.qp_type = IBV_QPT_RC;                  /* QP type, RC = Reliable connection */
    /* We use same completion queue, but one can use different queues */
    qp_init_attr.recv_cq = client_cq; /* Where should I notify for receive completion operations */
    qp_init_attr.send_cq = client_cq; /* Where should I notify for send completion operations */
    ret = rdma_create_qp(cm_client_id, pd, &qp_init_attr);
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
    conn_param.initiator_depth = MAX_RD_AT_IN_FLIGHT;
    conn_param.responder_resources = MAX_RD_AT_IN_FLIGHT;
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
    ret = ibv_post_send(client_qp, &client_send_wr, &bad_client_send_wr);
    if (ret) {
        rdma_error("Failed to read client dst buffer from the master, errno: %d \n", 
                -errno);
        return -errno;
    }
    /* at this point we are expecting 1 work completion for the write */
    ret = process_work_completion_events(io_completion_channel, &wc, 1);
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


typedef struct {
    double err_code;
    double xput_ops;
    double xput_bps;
    double cq_poll_time_percent;        // in cpu cycles
    double cq_poll_count;
    double cq_empty_count;
} result_t;

/* List of ops that are being instrumented */
enum rdma_measured_op { RDMA_READ_OP, RDMA_WRITE_OP };
enum mem_reg_mode { 
    MR_MODE_PRE_REGISTER,               // Use a pre-registered buffer and use it for all transactions
    MR_MODE_PRE_REGISTER_WITH_ROTATE,   // Use a set of pre-registered buffers (for the same piece of memory) but rotate their usage
    MR_MODE_REGISTER_IN_DATAPTH,        // Register buffers in datapath as necessary
};

/* Measures throughput for RDMA READ/WRITE ops for a specified message size and number of concurrent messages (i.e., requests in flight) */
/* Returns xput in ops/sec */
static result_t measure_xput(
    uint32_t msg_size,                  // payload size
    int num_concur,                     // number of requests in flight; use 1 for RTT measurements
    enum rdma_measured_op rdma_op,      // rdma op to use i.e., read or write
    enum mem_reg_mode mr_mode,          // mem reg mode; see comments for each "enum mem_reg_mode" 
    int num_lbuffers                    // number of pre-registed buffers to rotate between; only valid for MR_MODE_PRE_REGISTER_WITH_ROTATE
) {
    int ret = -1, n, i;
    struct ibv_wc* wc;
    uint64_t start_cycles, end_cycles;
    uint64_t xstart_cycles, xend_cycles;
    struct timeval      start, end;
    struct ibv_cq *cq_ptr = NULL;
    void *context = NULL;
    struct ibv_mr **mr_buffers = NULL;           /* Make sure to deregister these local MRs before exiting */
    struct ibv_mr *tmpbuffer = NULL;
    result_t result;
    
    /* Learned that the limit for number of outstanding requests for READ and ATOMIC ops 
     * is very less; while there is no such limit for WRITES. The connection parameters initiator_depth
     * and responder_resources determine such limit, which are again limited by hardware device 
     * attributes max_qp_rd_atom and max_qp_init_rd_atom. Why is there such a limit?  */
    /* In any case, for now, disallow request for concurrency more than the limit */
    if (rdma_op == RDMA_READ_OP && num_concur > MAX_RD_AT_IN_FLIGHT) {
        rdma_error("Device cannot support more than %d outstnading READs (num_concur=%d)\n", 
            MAX_RD_AT_IN_FLIGHT, num_concur);
        result.err_code = -EOVERFLOW;
        return result;
    }

    num_lbuffers = (mr_mode == MR_MODE_PRE_REGISTER_WITH_ROTATE) ? num_lbuffers : 1;
    if (num_lbuffers <= 0) {
        rdma_error("Invalid number of client-side buffers provided: %d\n", num_lbuffers);
        result.err_code = -EINVAL;
        return result; 
    }

    /* Allocate client buffers to read/write from (use the same piece of underlying memory) */
    mr_buffers = (struct ibv_mr**) malloc(num_lbuffers * sizeof(struct ibv_mr*));
    size_t buf_size = num_concur * msg_size;
    mr_buffers[0] = rdma_buffer_alloc(pd,
        buf_size,
        (IBV_ACCESS_LOCAL_WRITE | 
            IBV_ACCESS_REMOTE_WRITE | 
            IBV_ACCESS_REMOTE_READ));
    if (!mr_buffers[0]) {
        rdma_error("We failed to create the destination buffer, -ENOMEM\n");
        result.err_code = -ENOMEM;
        return result;
    }
    debug("Buffer registered (%3d): addr = %p, length = %ld, handle = %d, lkey = %d, rkey = %d\n", 0,
        mr_buffers[0]->addr, mr_buffers[0]->length, mr_buffers[0]->handle, mr_buffers[0]->lkey, mr_buffers[0]->rkey);

    if (num_lbuffers > 1) {
        /* Register rest of the buffers using same piece of memory */
        for (i = 1; i < num_lbuffers; i++) {
            mr_buffers[i] = rdma_buffer_register(pd,
                mr_buffers[0]->addr,
                buf_size,
                (IBV_ACCESS_LOCAL_WRITE | 
                    IBV_ACCESS_REMOTE_WRITE | 
                    IBV_ACCESS_REMOTE_READ));
            // mr_buffers[i] = rdma_buffer_alloc(pd,
            //     buf_size,
            //     (IBV_ACCESS_LOCAL_WRITE | 
            //         IBV_ACCESS_REMOTE_WRITE | 
            //         IBV_ACCESS_REMOTE_READ));

            if (!mr_buffers[i]) {
                rdma_error("Registering buffer %d failed\n", i);
                result.err_code = -ENOMEM;
                return result;
            }
            
            debug("Buffer registered (%3d): addr = %p, length = %ld, handle = %d, lkey = %d, rkey = %d\n", i,
                mr_buffers[i]->addr, mr_buffers[i]->length, mr_buffers[i]->handle, mr_buffers[i]->lkey, mr_buffers[i]->rkey);
        }
    }

    /* For sanity check, fill the client buffer with different alphabets in each message. 
     * We can verify this by reading the server buffer later. Only works for RDMA WRITEs though */
    for (i = 0; i < num_concur; i++)
        memset(mr_buffers[0]->addr + (i*msg_size), 'a' + i%26, msg_size);

    /* Prepare a template WR for RDMA ops */
    client_send_sge.addr = (uint64_t) mr_buffers[0]->addr;  
    client_send_sge.length = (uint32_t) msg_size;           // Send only msg_size
    client_send_sge.lkey = mr_buffers[0]->lkey;
    /* now we link to the send work request */
    bzero(&client_send_wr, sizeof(client_send_wr));
    client_send_wr.sg_list = &client_send_sge;
    client_send_wr.num_sge = 1;
    client_send_wr.opcode = rdma_op == RDMA_READ_OP ? IBV_WR_RDMA_READ : IBV_WR_RDMA_WRITE;
    client_send_wr.send_flags = IBV_SEND_SIGNALED;          // NOTE: This tells the other NIC to send completion events
    /* we have to tell server side info for RDMA */
    client_send_wr.wr.rdma.rkey = server_metadata_attr.stag.remote_stag;
    client_send_wr.wr.rdma.remote_addr = server_metadata_attr.address;

    gettimeofday (&start, NULL);
    rdtsc();
    start_cycles = ( ((int64_t)cycles_high << 32) | cycles_low );

    /* Post until number of max requests in flight is hit */
    char *buf_ptr = mr_buffers[0]->addr;
    int	buf_offset = 0;
    int buf_num = 0;
    uint64_t wr_posted = 0, wr_acked = 0;
    for (i = 0; i < num_concur; i++) {
        /* it is safe to reuse client_send_wr object after post_() returns */
        client_send_sge.lkey = mr_buffers[buf_num]->lkey;   /* Sets which MR to use to access the local address */
        client_send_wr.wr_id = buf_offset;                  /* User-assigned id to recognize this WR on completion */
        client_send_sge.addr = (uint64_t) buf_ptr;          /* Sets which mem addr to read from/write to locally */  // FIXME
        client_send_wr.wr.rdma.remote_addr = server_metadata_attr.address + buf_offset; 
        ret = ibv_post_send(client_qp, 
                &client_send_wr,
                &bad_client_send_wr);
        if (ret) {
            rdma_error("Failed to write client src buffer, errno: %d \n", -errno);
            result.err_code = -errno;
            return result;
        }
        	
        wr_posted++;
        buf_offset  = (buf_offset + msg_size) % buf_size;
	    buf_ptr     = mr_buffers[0]->addr + buf_offset;     /* We can always use mr_buffers[0] as all buffers point to same memory */
        buf_num     = (buf_num++) % num_lbuffers;
        // buf_num     = rand_xorshf96() % num_lbuffers;

        if (mr_mode == MR_MODE_REGISTER_IN_DATAPTH) {
            /* Emulate a buffer registration op for each request in datapath so we can capture its latency in the RTT.
             * We have to both register and de-register the buffer, just registering would cause buffer overflow 
             * So the number we get is not striclty register latency but at least this will give us a ballpark number */
            tmpbuffer = rdma_buffer_register(pd,
                mr_buffers[0]->addr,                // use the same piece of memory
                buf_size,
                (IBV_ACCESS_LOCAL_WRITE | 
                    IBV_ACCESS_REMOTE_WRITE | 
                    IBV_ACCESS_REMOTE_READ));
            rdma_buffer_deregister(tmpbuffer);
        }
    }

    /* at this point we are expecting work completions for the requests */
    /* We wait for the notification on the CQ channel */
    ret = ibv_get_cq_event(io_completion_channel, &cq_ptr, &context);
    if (ret) {
        rdma_error("Failed to get next CQ event due to %d \n", -errno);
        result.err_code = -errno;
        return result;
    }

    /* Request for more notifications. */
    ret = ibv_req_notify_cq(cq_ptr, 0);
    if (ret) {
        rdma_error("Failed to request further notifications %d \n", -errno);
        result.err_code = -errno;
        return result;
    }

    const uint64_t minimum_duration_secs = 10;  /* number of seconds to run the experiment for at least */
    const uint64_t check_watch_interval = 1e6;  /* Check time every million requests as checking on every request might be too costly */
    int stop_posting = 0;
    uint64_t poll_time = 0, poll_count = 0, idle_count = 0;
    wc = (struct ibv_wc *) calloc (num_concur, sizeof(struct ibv_wc));
    do {
        /* Poll the completion queue for the completion event for the earlier write */
        xrdtsc();
        do {
            n = ibv_poll_cq(cq_ptr, num_concur, wc);       // get upto num_concur entries
            if (n < 0) {
                rdma_error("Failed to poll cq for wc due to %d \n", ret);
                result.err_code = ret;     /* ret is errno here */
                return result;
            }
            poll_count++;
            if (n == 0)     idle_count++;
        } while (n < 1);
        xrdtsc1();
        xstart_cycles = ( ((int64_t)xcycles_high << 32) | xcycles_low );
        xend_cycles = ( ((int64_t)xcycles_high1 << 32) | xcycles_low1 );
        poll_time += (xend_cycles - xstart_cycles);

        /* For each completed request */
        for (i = 0; i < n; i++) {
            /* Check that it succeeded */
            if (wc[i].status != IBV_WC_SUCCESS) {
                rdma_error("Work completion (WC) has error status: %d, %s at index %ld",  
                    wc[i].status, ibv_wc_status_str(wc[i].status), wc[i].wr_id);
                result.err_code =  -(wc[i].status);       /* return negative value */
                return result;
            }

            /* Is it time to look at the watch? */
            if (wr_posted % check_watch_interval == 0) {   
                rdtsc1();
                end_cycles = ( ((int64_t)cycles_high1 << 32) | cycles_low1 );
                if ((end_cycles - start_cycles) >= minimum_duration_secs * CPU_FREQ) {
                    // we can stop posting
                    stop_posting = 1;
                }
            }

            if (!stop_posting) {
                /* Issue another request that reads/writes from same locations as the completed one */
                buf_offset  = wc[i].wr_id;
                buf_ptr     = mr_buffers[0]->addr + buf_offset;
                client_send_sge.lkey = mr_buffers[buf_num]->lkey; 
                client_send_wr.wr_id = buf_offset;              /* User-assigned id to recognize this WR on completion */
                client_send_sge.addr = (uint64_t) buf_ptr;
                client_send_wr.wr.rdma.remote_addr = server_metadata_attr.address + buf_offset;
                ret = ibv_post_send(client_qp, 
                        &client_send_wr,
                        &bad_client_send_wr);
                if (ret) {
                    rdma_error("Failed to write client src buffer, errno: %d \n", -errno);
                    result.err_code = -errno;
                    return result;
                }
                wr_posted++;
                buf_num     = (buf_num++) % num_lbuffers;
                // buf_num     = rand_xorshf96() % num_lbuffers;

                if (mr_mode == MR_MODE_REGISTER_IN_DATAPTH) {
                    /* Emulate a buffer registration op for each request in datapath so we can capture its latency in the RTT.
                    * We have to both register and de-register the buffer, just registering would cause buffer overflow 
                    * So the number we get is not striclty register latency but at least this will give us a ballpark number */
                    tmpbuffer = rdma_buffer_register(pd,
                        mr_buffers[0]->addr,                // use the same piece of memory
                        buf_size,
                        (IBV_ACCESS_LOCAL_WRITE | 
                            IBV_ACCESS_REMOTE_WRITE | 
                            IBV_ACCESS_REMOTE_READ));
                    rdma_buffer_deregister(tmpbuffer);
                }
            }
        }

        /* Stop when all the posted WRs are acked */
        wr_acked += n;
    } while(wr_acked < wr_posted);
    
    rdtsc1();
    end_cycles = ( ((int64_t)cycles_high1 << 32) | cycles_low1 );
    gettimeofday (&end, NULL);
    debug("WRs posted: %lu, WRs acked: %lu\n", wr_posted, wr_acked);

    /* Similar to connection management events, we need to acknowledge CQ events */
    ibv_ack_cq_events(cq_ptr, 1 /* we received one event notification. This is not number of WC elements */);
    

    /* SANITY CHECK */
    /* In case of RDMA writes, perform a sanity check by reading the server-side buffer and checking its content */
    if (num_lbuffers == 1 && rdma_op == RDMA_WRITE_OP) {
        /* Clear the local buffer and issue a big read */
        memset(mr_buffers[0]->addr, 0, num_concur * msg_size); 

        /* Now we prepare a READ using same variables but for destination */
        client_send_sge.addr = (uint64_t) mr_buffers[0]->addr;
        client_send_sge.length = (uint32_t) msg_size * num_concur;           // entire buffer
        client_send_sge.lkey = mr_buffers[0]->lkey;
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
            rdma_error("Failed to read client dst buffer from the master, errno: %d \n", -errno);
            result.err_code = -errno;
            return result;
        }
        /* at this point we are expecting 1 work completion for the read */
        ret = process_work_completion_events(io_completion_channel, wc, 1);
        if(ret != 1 || wc->opcode != IBV_WC_RDMA_READ) {
            rdma_error("We failed to get 1 work completion for the sanity check read, ret = %d \n",ret);
            result.err_code = ret;
            return result;
        }

        /* Validate the buffer */
        char* expected = (char*) malloc(msg_size * sizeof(char));
        for (i = 0; i < num_concur; i++) {
            memset(expected, 'a' + i%26, msg_size);
            if (strncmp(expected, (char*) mr_buffers[0]->addr + (i*msg_size), msg_size) != 0) {
                printf("%d\n", strncmp(expected, (char*) mr_buffers[0]->addr + (i*msg_size), msg_size));
                rdma_error("Sanity check failed. \nBuffer read from server does not contain expected data at msg index: %d.\n\
                    Expected: %.*s \n\
                    Actual: %.*s \n", i+1, msg_size, expected, msg_size, (char*) mr_buffers[0]->addr + (i*msg_size));
                result.err_code = 1;
                return result;
            }
        }
        debug("SANITY check complete!\n");
    }

    /* Deregister local MRs */
    for (i = 0; i < num_lbuffers; i++)
        rdma_buffer_deregister(mr_buffers[i]);	

    /* Calculate duration. See if RDTSC timer agrees with regular ctime.
     * ctime is more accurate on longer timescales as rdtsc depends on cpu frequency which is not stable
     * but rdtsc is low overhead so we can use that from (roughly) keeping track of time while we use ctime 
     * to calculate numbers */
    double duration_rdtsc = (end_cycles - start_cycles) / CPU_FREQ;
    double duration_ctime = (double)((end.tv_sec - start.tv_sec) + (end.tv_usec - start.tv_usec) * 1.0e-6);
    debug("Duration as measured by RDTSC: %.2lf secs, by ctime: %.2lf secs\n", duration_rdtsc, duration_ctime);

    double goodput_pps = wr_acked / duration_ctime;
    double goodput_bps = goodput_pps * msg_size * 8;
    debug("Goodput = %.2lf Gbps. Sampled for %.2lf seconds\n", goodput_bps / 1e9, duration_ctime);
    
    /* Fill in result */
    result.xput_bps = goodput_bps / 1e9;
    result.xput_ops = goodput_pps;
    result.cq_poll_time_percent = poll_time * 100.0 / CPU_FREQ / duration_rdtsc;
    result.cq_poll_count = poll_count * 1.0 / wr_acked;     // TODO: Is dividing by wr the right way to interpret this number?
    result.cq_empty_count = idle_count * 1.0 / wr_acked;     // TODO: Is dividing by wr the right way to interpret this number?
    return result;
}

/* Data transfer modes */
enum data_transfer_mode { 
    DTR_MODE_NO_GATHER,         // Assume that data is already gathered in a single big buffer
    DTR_MODE_CPU_GATHER,        // Assume that data is scattered but is gathered by CPU into a single big buffer before xmit
    DTR_MODE_NIC_GATHER,        // Assume that data is scattered but is gathered by NIC with a scatter-gather op during xmit
    DTR_MODE_PIECE_BY_PIECE,    // Assume that data is scattered but each piece is sent in a different rdma write op
};

/* Measures throughput for RDMA READ/WRITE ops for a specified message size and number of concurrent messages (i.e., requests in flight) */
/* Returns xput in ops/sec */
static result_t measure_xput_scatgath(
    uint32_t msg_size,                  // payload size
    uint32_t num_sg_pieces,            // number of scatter-gather pieces
    int num_concur,                     // number of requests in flight; use 1 for RTT measurements
    enum rdma_measured_op rdma_op,      // rdma op to use i.e., read or write
    enum data_transfer_mode dtr_mode    // data transfer mode
) {
    int ret = -1, n, i, j;
    struct ibv_wc* wc;
    uint64_t start_cycles, end_cycles;
    uint64_t xstart_cycles, xend_cycles;
    struct timeval      start, end;
    struct ibv_cq *cq_ptr = NULL;
    void *context = NULL;
    int num_mr_bufs;
    result_t result;
    
    struct ibv_mr **mr_buffers = NULL;           /* Make sure to deregister these local MRs before exiting */
    struct ibv_mr *one_big_buffer = NULL;       /* Make sure to deregister these local MRs before exiting */
    
    if (rdma_op != RDMA_WRITE_OP) {
        /* Only write ops supported for now */
        rdma_error("only write supported for scatter-gather ops\n");
        result.err_code = -EINVAL;
        return result; 
    }

    /* Learned that the limit for number of outstanding requests for READ and ATOMIC ops 
     * is very less; while there is no such limit for WRITES. The connection parameters initiator_depth
     * and responder_resources determine such limit, which are again limited by hardware device 
     * attributes max_qp_rd_atom and max_qp_init_rd_atom. Why is there such a limit?  */
    /* In any case, for now, disallow request for concurrency more than the limit */
    if (rdma_op == RDMA_READ_OP && num_concur > MAX_RD_AT_IN_FLIGHT) {
        rdma_error("Device cannot support more than %d outstnading READs (num_concur=%d)\n", 
            MAX_RD_AT_IN_FLIGHT, num_concur);
        result.err_code = -EOVERFLOW;
        return result;
    }

    /* Some constraints on payload and scatter-gather piece sizes */
    assert(num_sg_pieces <= MAX_SGE);                   
    assert(msg_size % num_sg_pieces == 0);              /* that msg size splits into equal parts */
    uint32_t sg_piece_size = msg_size / num_sg_pieces;

    /* If sending each piece in a separate op, the scenario is equivalent to just measuring xput 
     * for msg_size = piece_size (assuming that we split the original payload into equal-sized 
     * pieces *and* that we are running on a single core and no specific ordering is attached to 
     * transmitting these pieces i.e., concurrency is determined solely by num_concur: the max 
     * max number of requests allowed in flight). But of course, the returned throughput (ops/sec)
     * is divided by number of pieces per message because in this method, an operation represents 
     * sending the whole payload, not just one of its pieces. */
    if (dtr_mode == DTR_MODE_PIECE_BY_PIECE) {
        result = measure_xput(
            sg_piece_size,
            num_concur,
            rdma_op,
            MR_MODE_PRE_REGISTER_WITH_ROTATE,
            num_sg_pieces);
        result.xput_ops /= num_sg_pieces;
        return result;
    }

    /* One big buffer to hold aggregated payloads (times the num of requests in flight) */
    size_t big_buf_size = msg_size * num_concur;
    one_big_buffer = rdma_buffer_alloc(pd,
        big_buf_size,
        (IBV_ACCESS_LOCAL_WRITE | 
            IBV_ACCESS_REMOTE_WRITE | 
            IBV_ACCESS_REMOTE_READ));
    if (!one_big_buffer) {
        rdma_error("Failed to create the big source buffer, -ENOMEM\n");
        result.err_code = -ENOMEM;
        return result;
    }
    debug("Buffer registered (%3d): addr = %p, length = %ld, handle = %d, lkey = %d, rkey = %d\n", 0,
        one_big_buffer->addr, one_big_buffer->length, one_big_buffer->handle, one_big_buffer->lkey, one_big_buffer->rkey);

    /* Allocate multiple small buffers to emulate scattered source data (times the num of requests in flight)
     *(each message will be gathered in pieces from all these buffers)  */
    num_mr_bufs = num_sg_pieces;
    size_t src_buf_size = sg_piece_size * num_concur;
    mr_buffers = (struct ibv_mr**) malloc(num_mr_bufs * sizeof(struct ibv_mr*));
    for (i = 0; i < num_mr_bufs; i++) {
        mr_buffers[i] = rdma_buffer_alloc(pd,
            src_buf_size,
            (IBV_ACCESS_LOCAL_WRITE | 
                IBV_ACCESS_REMOTE_WRITE | 
                IBV_ACCESS_REMOTE_READ));
        if (!mr_buffers[i]) {
            rdma_error("We failed to create the destination buffer, -ENOMEM\n");
            result.err_code = -ENOMEM;
            return result;
        }
        debug("Buffer registered (%3d): addr = %p, length = %ld, handle = %d, lkey = %d, rkey = %d\n", 0,
            mr_buffers[i]->addr, mr_buffers[i]->length, mr_buffers[i]->handle, mr_buffers[i]->lkey, mr_buffers[i]->rkey);
    }

    /* For sanity check, fill the scattered source buffers with different alphabets in each message. 
     * We can verify this by reading the server buffer later. Only works for RDMA WRITEs though */
    for (i = 0; i < num_concur; i++)
        for (j = 0; j < num_mr_bufs; j++)
            memset(mr_buffers[j]->addr + (i*sg_piece_size), 'a' + i%26, sg_piece_size);

    /* Pre-Prepare SGE lists and WRs for the big buffer for different concurrent requests */
    struct ibv_send_wr big_buf_wrs[num_concur];
    bzero(&big_buf_wrs, sizeof(big_buf_wrs));
    struct ibv_sge big_buf_sge[num_concur];
    int	big_buf_offset = 0;
    for (i = 0; i < num_concur; i++) {
        big_buf_sge[i].addr = (uint64_t) (one_big_buffer->addr + big_buf_offset);  
        big_buf_sge[i].length = (uint32_t) msg_size;           // Send only msg_size
        big_buf_sge[i].lkey = one_big_buffer->lkey;   
        
        big_buf_wrs[i].wr_id = i;                               /* User-assigned id to recognize this WR on completion */
        big_buf_wrs[i].sg_list = &big_buf_sge[i];
        big_buf_wrs[i].num_sge = 1;
        big_buf_wrs[i].opcode = rdma_op == RDMA_READ_OP ? IBV_WR_RDMA_READ : IBV_WR_RDMA_WRITE;
        big_buf_wrs[i].send_flags = IBV_SEND_SIGNALED;

        /* we have to tell server side info for RDMA */
        big_buf_wrs[i].wr.rdma.rkey = server_metadata_attr.stag.remote_stag;
        big_buf_wrs[i].wr.rdma.remote_addr = server_metadata_attr.address + big_buf_offset;
        
        big_buf_offset  = (big_buf_offset + msg_size) % big_buf_size;
    }


    /* Pre-Prepare SGE lists and WRs for gathering data from scattered buffers for different concurent requests */
    struct ibv_send_wr src_buf_wrs[num_concur];
    bzero(&src_buf_wrs, sizeof(src_buf_wrs));
    struct ibv_sge src_sge_arr[num_concur][num_mr_bufs];
    int src_buf_offset = 0;
    big_buf_offset = 0;
    for (i = 0; i < num_concur; i++) {
        for (j = 0; j < num_mr_bufs; j++) {
            src_sge_arr[i][j].addr = (uint64_t) (mr_buffers[j]->addr + src_buf_offset);  
            src_sge_arr[i][j].length = (uint32_t) sg_piece_size;   // Send only sg piece
            src_sge_arr[i][j].lkey = mr_buffers[j]->lkey;
        }

        src_buf_wrs[i].wr_id = i;                               /* User-assigned id to recognize this WR on completion */
        src_buf_wrs[i].sg_list = &src_sge_arr[i][0];
        src_buf_wrs[i].num_sge = num_mr_bufs;
        src_buf_wrs[i].opcode = rdma_op == RDMA_READ_OP ? IBV_WR_RDMA_READ : IBV_WR_RDMA_WRITE;
        src_buf_wrs[i].send_flags = IBV_SEND_SIGNALED;

        /* we have to tell server side info for RDMA */
        src_buf_wrs[i].wr.rdma.rkey = server_metadata_attr.stag.remote_stag;
        src_buf_wrs[i].wr.rdma.remote_addr = server_metadata_attr.address + big_buf_offset;

        src_buf_offset  = (src_buf_offset + sg_piece_size) % src_buf_size;
        big_buf_offset  = (big_buf_offset + msg_size) % big_buf_size;
    }

    /* Start timer */
    gettimeofday (&start, NULL);
    rdtsc();
    start_cycles = ( ((int64_t)cycles_high << 32) | cycles_low );

    /* Post until number of max requests in flight is hit */
    uint64_t wr_posted = 0, wr_acked = 0;
    for (i = 0; i < num_concur; i++) {

        /* In case of cpu gather, emulate it by copying data from scattered src buffers to the big buffer */
        if (dtr_mode == DTR_MODE_CPU_GATHER) {
            for (j = 0; j < num_mr_bufs; j++) {
                memcpy(
                    (void*)(big_buf_sge[i].addr + j*sg_piece_size),     // to: big buffer at position of j-th piece
                    (void*)src_sge_arr[i][j].addr,                      // from: scattered data piece number j
                    src_sge_arr[i][j].length
                );
            }
        }

        /* Use this snippet to print a mem buffer while debugging */
        // *((char*)one_big_buffer->addr + big_buf_size - 1) = '\0';
        // printf("%s\n", (char*)one_big_buffer->addr); 

        struct ibv_send_wr* wr_to_send = dtr_mode == DTR_MODE_NIC_GATHER ? &src_buf_wrs[i] : &big_buf_wrs[i];
        ret = ibv_post_send(client_qp, 
                wr_to_send,
                &bad_client_send_wr);
        if (ret) {
            rdma_error("Failed to write client src buffer, errno: %d \n", -errno);
            result.err_code = -errno;
            return result;
        }
        	
        wr_posted++;
    }

    /* at this point we are expecting work completions for the requests */
    /* We wait for the notification on the CQ channel */
    ret = ibv_get_cq_event(io_completion_channel, &cq_ptr, &context);
    if (ret) {
        rdma_error("Failed to get next CQ event due to %d \n", -errno);
        result.err_code = -errno;
        return result;
    }

    /* Request for more notifications. */
    ret = ibv_req_notify_cq(cq_ptr, 0);
    if (ret) {
        rdma_error("Failed to request further notifications %d \n", -errno);
        result.err_code = -errno;
        return result;
    }

    const uint64_t minimum_duration_secs = 10;  /* number of seconds to run the experiment for at least */
    const uint64_t check_watch_interval = 1e6;  /* Check time every million requests as checking on every request might be too costly */
    int stop_posting = 0;
    uint64_t poll_time = 0, poll_count = 0, idle_count = 0;
    wc = (struct ibv_wc *) calloc (num_concur, sizeof(struct ibv_wc));
    do {
        /* Poll the completion queue for the completion event for the earlier write */
        xrdtsc();
        do {
            n = ibv_poll_cq(cq_ptr, num_concur, wc);       // get upto num_concur entries
            if (n < 0) {
                rdma_error("Failed to poll cq for wc due to %d \n", ret);
                result.err_code = ret;     /* ret is errno here */
                return result;
            } 
            poll_count++;
            if (n == 0)     idle_count++;
        } while (n < 1);
        xrdtsc1();
        xstart_cycles = ( ((int64_t)xcycles_high << 32) | xcycles_low );
        xend_cycles = ( ((int64_t)xcycles_high1 << 32) | xcycles_low1 );
        poll_time += (xend_cycles - xstart_cycles);

        /* For each completed request */
        for (i = 0; i < n; i++) {
            /* Check that it succeeded */
            if (wc[i].status != IBV_WC_SUCCESS) {
                rdma_error("Work completion (WC) has error status: %d, %s at index %ld",  
                    wc[i].status, ibv_wc_status_str(wc[i].status), wc[i].wr_id);
                result.err_code =  -(wc[i].status);       /* return negative value */
                return result;
            }

            /* Is it time to look at the watch? */
            if (wr_posted % check_watch_interval == 0) {   
                rdtsc1();
                end_cycles = ( ((int64_t)cycles_high1 << 32) | cycles_low1 );
                if ((end_cycles - start_cycles) >= minimum_duration_secs * CPU_FREQ) {
                    // we can stop posting
                    stop_posting = 1;
                }
            }

            if (!stop_posting) {
                /* Issue another request that reads/writes from same locations as the completed one */
                int idx = wc[i].wr_id;
                
                /* In case of cpu gather, emulate it by copying data from scattered src buffers to the big buffer */
                if (dtr_mode == DTR_MODE_CPU_GATHER) {
                    memcpy(
                        (void*)(big_buf_sge[idx].addr + j*sg_piece_size),     // to: big buffer at position of j-th piece
                        (void*)src_sge_arr[idx][j].addr,                      // from: scattered data piece number j
                        src_sge_arr[idx][j].length
                    );
                }

                struct ibv_send_wr* wr_to_send = dtr_mode == DTR_MODE_NIC_GATHER ? &src_buf_wrs[idx] : &big_buf_wrs[idx];
                ret = ibv_post_send(client_qp, 
                    wr_to_send,
                    &bad_client_send_wr);
                if (ret) {
                    rdma_error("Failed to write client src buffer, errno: %d \n", -errno);
                    result.err_code = -errno;
                    return result;
                }
                wr_posted++;
            }
        }

        /* Stop when all the posted WRs are acked */
        wr_acked += n;
    } while(wr_acked < wr_posted);
    
    rdtsc1();
    end_cycles = ( ((int64_t)cycles_high1 << 32) | cycles_low1 );
    gettimeofday (&end, NULL);
    debug("WRs posted: %lu, WRs acked: %lu\n", wr_posted, wr_acked);

    /* Similar to connection management events, we need to acknowledge CQ events */
    ibv_ack_cq_events(cq_ptr, 1 /* we received one event notification. This is not number of WC elements */);

    /* SANITY CHECK */
    /* In case of RDMA writes, perform a sanity check by reading the server-side buffer and checking its content */
    if (rdma_op == RDMA_WRITE_OP && dtr_mode != DTR_MODE_NO_GATHER) {
        /* Clear the local buffer and issue a big read */
        memset(one_big_buffer->addr, 0, num_concur * msg_size); 

        /* Now we prepare a READ using same variables but for destination */
        client_send_sge.addr = (uint64_t) one_big_buffer->addr;
        client_send_sge.length = (uint32_t) msg_size * num_concur;           // entire buffer
        client_send_sge.lkey = one_big_buffer->lkey;
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
            rdma_error("Failed to read client dst buffer from the master, errno: %d \n", -errno);
            result.err_code = -errno;
            return result;
        }
        /* at this point we are expecting 1 work completion for the read */
        ret = process_work_completion_events(io_completion_channel, wc, 1);
        if(ret != 1 || wc->opcode != IBV_WC_RDMA_READ) {
            rdma_error("We failed to get 1 work completion for the sanity check read, ret = %d \n",ret);
            result.err_code = ret;
            return result;
        }

        /* Validate the buffer */
        char* expected = (char*) malloc(msg_size * sizeof(char));
        for (i = 0; i < num_concur; i++) {
            memset(expected, 'a' + i%26, msg_size);
            if (strncmp(expected, (char*) one_big_buffer->addr + (i*msg_size), msg_size) != 0) {
                printf("%d\n", strncmp(expected, (char*) one_big_buffer->addr + (i*msg_size), msg_size));
                rdma_error("Sanity check failed. \nBuffer read from server does not contain expected data at msg index: %d.\n\
                    Expected: %.*s \n\
                    Actual: %.*s \n", i+1, msg_size, expected, msg_size, (char*) one_big_buffer->addr + (i*msg_size));
                result.err_code = 1;
                return result;
            }
        }
        debug("SANITY check complete!\n");
    }

    /* Deregister local MRs */
    for (i = 0; i < num_mr_bufs; i++)
        rdma_buffer_deregister(mr_buffers[i]);	
    rdma_buffer_deregister(one_big_buffer);

    /* Calculate duration. See if RDTSC timer agrees with regular ctime.
     * ctime is more accurate on longer timescales as rdtsc depends on cpu frequency which is not stable
     * but rdtsc is low overhead so we can use that from (roughly) keeping track of time while we use ctime 
     * to calculate numbers */
    double duration_rdtsc = (end_cycles - start_cycles) / CPU_FREQ;
    double duration_ctime = (double)((end.tv_sec - start.tv_sec) + (end.tv_usec - start.tv_usec) * 1.0e-6);
    debug("Duration as measured by RDTSC: %.2lf secs, by ctime: %.2lf secs\n", duration_rdtsc, duration_ctime);

    double goodput_pps = wr_acked / duration_ctime;
    double goodput_bps = goodput_pps * msg_size * 8;
    debug("Goodput = %.2lf Gbps. Sampled for %.2lf seconds\n", goodput_bps / 1e9, duration_ctime);
    debug("Poll CQ time (cycles) and poll count per window: %lf, %lf\n", poll_time * 100.0 / CPU_FREQ / duration_rdtsc, poll_count * num_concur *1.0 / wr_acked);

    /* Fill in result */
    result.xput_bps = goodput_bps / 1e9;
    result.xput_ops = goodput_pps;
    result.cq_poll_time_percent = poll_time * 100.0 / CPU_FREQ / duration_rdtsc;
    result.cq_poll_count = poll_count * 1.0 / wr_acked;     // TODO: Is dividing by wr the right way to interpret this number?
    result.cq_empty_count = idle_count * 1.0 / wr_acked;     // TODO: Is dividing by wr the right way to interpret this number?
    return result;
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
    printf("rdma_client: [-a <server_addr>] [-p <server_port>] [-s/--simple]\n");
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
    int rtt_flag = 0, xput_flag = 0, xputv2_flag = 0;
    int num_concur = 1;
    int num_mbuf = 1;
    int msg_size = 0;
    int write_to_file = 0;
    char outfile[200] ;

    /* Parse Command Line Arguments */
    static const struct option options[] = {
        {.name = "simple", .has_arg = no_argument, .val = 's'},
        {.name = "rtt", .has_arg = no_argument, .val = 'r'},
        {.name = "xput", .has_arg = no_argument, .val = 'x'},
        {.name = "xputv2", .has_arg = no_argument, .val = 'y'},
        {.name = "concur", .has_arg = required_argument, .val = 'c'},
        {.name = "buffers", .has_arg = required_argument, .val = 'b'},
        {.name = "msgsize", .has_arg = required_argument, .val = 'm'},
        {.name = "out", .has_arg = required_argument, .val = 'o'},
        {}
    };
    while ((option = getopt_long(argc, argv, "sa:p:rxyc:b:m:o:", options, NULL)) != -1) {
        switch (option) {
            case 's':
                /* run the basic example to test connection */
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
            case 'r':
                /* Run RTT measurement (for all msg sizes) */
                rtt_flag = 1;     
                break;
            case 'x':
                /* Run xput measurement */
                xput_flag = 1;
                break;
            case 'y':
                /* Run advanced xput measurements for different data transfer modes  */
                xputv2_flag = 1;
                break;
            case 'c':
                /* concurrency i.e., number of requests in flight */
                /* num_concur < 0: invalid; = 0: vary from 1 to 128; > 0: use the number. Default is 1 */
                num_concur = strtol(optarg, NULL, 0); 
                if (num_concur < 0 || num_concur > MAX_WR) {
                    rdma_error("Invalid num_concur. Should be between 0 and %d\n", MAX_WR);
                    return -1;
                }
                break;  
            case 'b':
                /* number of MR buffers to use */
                /* num_mbuf < 0: invalid; = 0: vary from 1 to 256; > 0: use the number. Default is 1 */
                num_mbuf = strtol(optarg, NULL, 0); 
                if (num_mbuf < 0 || num_mbuf > MAX_MR) {
                    rdma_error("Invalid num_mbuf. Should be between 1 and %d\n", MAX_MR);
                    return -1;
                }
                break; 
            case 'm':
                /* input payload size to use */
                /* msg_size < 0: invalid; = 0: vary from 64 to 4096; > 0: use the number. Default is 0 (vary) */
                msg_size = strtol(optarg, NULL, 0); 
                if (msg_size < 0 || msg_size > 4096) {
                    rdma_error("Invalid msg_size. Should be between 1 and %d\n", 4096);
                    return -1;
                }
                break;  
            case 'o':
                /* output file to write to */
                write_to_file = 1;
                strcpy(outfile, optarg);
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

    /* Client-side RDMA setup */
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

    /* Connection now set up and ready to play */
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
        
        int min_num_concur = num_concur == 0 ? 1 : num_concur;
        int max_num_concur = num_concur == 0 ? 256 : num_concur;        /* Empirically measured that anything above this number does not matter for single core */

        int min_msg_size = msg_size == 0 ? 64 : msg_size;
        int max_msg_size = msg_size == 0 ? 4096 : msg_size;
        int msg_size_incr = 64;

        int min_num_mbuf = num_mbuf == 0 ? 1 : num_mbuf;
        // int max_num_mbuf = num_mbuf == 0 ? MAX_MR : num_mbuf;
        int max_num_mbuf = num_mbuf == 0 ? 1e6 : num_mbuf;

        /* Get roundtrip latencies for ops */
        if (rtt_flag) {
            // printf("RTT for %d B WRITES: %0.2lf mu-secs\n", 64, measure_rtt(64, RDMA_WRITE_OP));        // WRITE RTT
            // printf("RTT for %d B READS : %0.2lf mu-secs\n", 64, measure_rtt(64, RDMA_READ_OP));         // READ RTT

            /* We can also get them using the xput method with 1 request in flight; results agreed with above method. */
            // printf("RTT for %d B WRITES: %0.2lf mu-secs\n", 64, 1e6 / measure_xput(64, 1, RDMA_WRITE_OP) );     // WRITE RTT
            // printf("RTT for %d B READS : %0.2lf mu-secs\n", 64, 1e6 / measure_xput(64, 1, RDMA_READ_OP));       // READ RTT


            /* Generate RTT numbers for different msg sizes */
            FILE *fptr;
            if (write_to_file) {
                printf("Writing output to: %s\n", outfile);
                fptr = fopen(outfile, "w");
                fprintf(fptr, "msg size,write,read\n");
            }
            printf("=========== RTTs =============\n");
            printf("msg size,write,read\n");
            for (msg_size = min_msg_size; msg_size <= max_msg_size; msg_size += msg_size_incr) {
                double wrtt = 1e6 / measure_xput(msg_size, 1, RDMA_WRITE_OP, MR_MODE_PRE_REGISTER_WITH_ROTATE, num_mbuf).xput_ops;
                double rrtt = 1e6 / measure_xput(msg_size, 1, RDMA_READ_OP, MR_MODE_PRE_REGISTER_WITH_ROTATE, num_mbuf).xput_ops;
                printf("%d,%.2lf,%.2lf\n", msg_size, wrtt, rrtt);
                if (write_to_file) {
                    fprintf(fptr, "%d,%.2lf,%.2lf\n", msg_size, wrtt, rrtt);
                    fflush(fptr);
                }
            }
            if (write_to_file)  fclose(fptr);
        }

        /* Get xputs for ops */
        if (xput_flag) {
            // printf("Goodput for %3d B READS (%3d QPE) : %0.2lf gbps\n", 1024, 1, 
            //     measure_xput(1024, 1, RDMA_READ_OP) * 1024 * 8 / 1e9);       // READ Xput
            // printf("Goodput for %3d B WRITES (%3d QPE) : %0.2lf gbps\n", 1024, 1, 
            //     measure_xput(1024, 1, RDMA_WRITE_OP) );       // WRITE Xput

            FILE *fptr;
            if (write_to_file) {
                printf("Writing output to: %s\n", outfile);
                fptr = fopen(outfile, "w");
                fprintf(fptr, "msg size,window size,mbufs,write_ops,write_gbps,read_ops,read_gbps,write_pp,write_pcq,write_ecq\n");
            }
            printf("=========== Xput =============\n");
            printf("msg size,window size,mbufs,write_ops,write_gbps,read_ops,read_gbps,write_pp,write_pcq,write_ecq\n");
            for (num_concur = min_num_concur; num_concur <= max_num_concur; num_concur *= 2)
                for (num_mbuf = min_num_mbuf; num_mbuf <= max_num_mbuf; num_mbuf += 500)    // FIXME
                    for (msg_size = min_msg_size; msg_size <= max_msg_size; msg_size += 64) {
                        result_t wput = measure_xput(msg_size, num_concur, RDMA_WRITE_OP, MR_MODE_PRE_REGISTER_WITH_ROTATE, num_mbuf);
                        double rput_ops = (num_concur <= MAX_RD_AT_IN_FLIGHT) ? measure_xput(msg_size, num_concur, RDMA_READ_OP, MR_MODE_PRE_REGISTER_WITH_ROTATE, num_mbuf).xput_ops : 0;
                        double rput_gbps = rput_ops * msg_size * 8 / 1e9;
                        printf("%d,%d,%d,%.2lf,%.2lf,%.2lf,%.2lf,%.2lf,%.2lf,%.2lf\n", 
                            msg_size, num_concur, num_mbuf, wput.xput_ops, wput.xput_bps, 
                            rput_ops, rput_gbps, 
                            wput.cq_poll_time_percent, wput.cq_poll_count, wput.cq_empty_count);
                        if (write_to_file) {
                            fprintf(fptr, "%d,%d,%d,%.2lf,%.2lf,%.2lf,%.2lf,%.2lf,%.2lf,%.2lf\n", 
                                msg_size, num_concur, num_mbuf, wput.xput_ops, wput.xput_bps,
                                rput_ops, rput_gbps, 
                                wput.cq_poll_time_percent, wput.cq_poll_count, wput.cq_empty_count);
                            fflush(fptr);
                        }
                    }
            if (write_to_file)  fclose(fptr);
        }

        /* Get advanced xput numbers for various data transfer modes */
        if (xputv2_flag) {
            FILE *fptr;
            if (write_to_file) {
                printf("Writing output to: %s\n", outfile);
                fptr = fopen(outfile, "w");
                fprintf(fptr, "msg size,window size,sg pieces,"
                    "base_ops,base_gbps,base_pp,base_pcq,base_ecq,"
                    "cpu_gather_ops,cpu_gather_gpbs,cpu_gather_pp,cpu_gather_pcq,cpu_gather_ecq,"
                    "nic_gather_ops,nic_gather_gbps,nic_gather_pp,nic_gather_pcq,nic_gather_ecq,"
                    "piece_by_piece_ops,piece_by_piece_gbps,piece_by_piece_pp,piece_by_piece_pcq,piece_by_piece_ecq\n");
            }
            printf("=========== Xput =============\n");
            printf("msg size,window size,sg pieces,"
                "base_ops,base_gbps,base_pp,base_pcq,base_ecq,"
                "cpu_gather_ops,cpu_gather_gpbs,cpu_gather_pp,cpu_gather_pcq,cpu_gather_ecq,"
                "nic_gather_ops,nic_gather_gbps,nic_gather_pp,nic_gather_pcq,nic_gather_ecq,"
                "piece_by_piece_ops,piece_by_piece_gbps,piece_by_piece_pp,piece_by_piece_pcq,piece_by_piece_ecq\n");
            int num_pieces;
            for (num_concur = min_num_concur; num_concur <= max_num_concur; num_concur *= 2)
                for (msg_size = min_msg_size; msg_size <= max_msg_size; msg_size += 64) 
                    for (num_pieces = 1; num_pieces < MAX_SGE; num_pieces++) {
                        if (msg_size % num_pieces != 0)     // Only consider factors
                            continue;
                        result_t mode1 = measure_xput_scatgath(msg_size, num_pieces, num_concur, RDMA_WRITE_OP, DTR_MODE_NO_GATHER);
                        result_t mode2 = measure_xput_scatgath(msg_size, num_pieces, num_concur, RDMA_WRITE_OP, DTR_MODE_CPU_GATHER);
                        result_t mode3 = measure_xput_scatgath(msg_size, num_pieces, num_concur, RDMA_WRITE_OP, DTR_MODE_NIC_GATHER);
                        result_t mode4 = measure_xput_scatgath(msg_size, num_pieces, num_concur, RDMA_WRITE_OP, DTR_MODE_PIECE_BY_PIECE);
                        printf("%d,%d,%d,%.2lf,%.2lf,%.2lf,%.2lf,%.2lf,%.2lf,%.2lf,%.2lf,%.2lf,%.2lf,%.2lf,%.2lf,%.2lf,%.2lf,%.2lf,%.2lf,%.2lf,%.2lf,%.2lf,%.2lf\n", 
                            msg_size, num_concur, num_pieces, 
                            mode1.xput_ops, mode1.xput_bps, mode1.cq_poll_time_percent, mode1.cq_poll_count, mode1.cq_empty_count, 
                            mode2.xput_ops, mode2.xput_bps, mode2.cq_poll_time_percent, mode2.cq_poll_count, mode2.cq_empty_count, 
                            mode3.xput_ops, mode3.xput_bps, mode3.cq_poll_time_percent, mode3.cq_poll_count, mode3.cq_empty_count, 
                            mode4.xput_ops, mode4.xput_bps, mode4.cq_poll_time_percent, mode4.cq_poll_count, mode4.cq_empty_count);
                        if (write_to_file) {
                            fprintf(fptr, "%d,%d,%d,%.2lf,%.2lf,%.2lf,%.2lf,%.2lf,%.2lf,%.2lf,%.2lf,%.2lf,%.2lf,%.2lf,%.2lf,%.2lf,%.2lf,%.2lf,%.2lf,%.2lf,%.2lf,%.2lf,%.2lf\n", 
                                msg_size, num_concur, num_pieces, 
                                mode1.xput_ops, mode1.xput_bps, mode1.cq_poll_time_percent, mode1.cq_poll_count, mode1.cq_empty_count, 
                                mode2.xput_ops, mode2.xput_bps, mode2.cq_poll_time_percent, mode2.cq_poll_count, mode2.cq_empty_count, 
                                mode3.xput_ops, mode3.xput_bps, mode3.cq_poll_time_percent, mode3.cq_poll_count, mode3.cq_empty_count, 
                                mode4.xput_ops, mode4.xput_bps, mode4.cq_poll_time_percent, mode4.cq_poll_count, mode4.cq_empty_count);
                            fflush(fptr);
                        }
                }
            if (write_to_file)  fclose(fptr);
        }
    }

    ret = client_disconnect_and_clean();
    if (ret) {
        rdma_error("Failed to cleanly disconnect and clean up resources \n");
    }
    return ret;
}

