# Accelerating Apache Spark With SmartNICs
A feasibility study for evaluating the benefits of offloading Java object serialization (~20 to 50% CPU overhead) to SmartNICs. 
Involves:
1. Studying Spark Object Layout for various workloads to understand memory layout and possibility of using RDMA-like interface to directly copy objects (true zero copy).
2. Various RDMA (memory reg and scatter/gather) experiments to understand CPU impact of various data transfer modes including boucne buffer and zero copy scatter-gather.
