# 15.3 Containers and Kubernetes

RDMA-dependent workloads -- ML training, high-performance databases, distributed storage -- are increasingly deployed in containers on Kubernetes. The challenge: containers share the host kernel, so the clean hardware partitioning that SR-IOV provides for VMs does not directly apply. Containers need a different set of mechanisms for RDMA device access, isolation, and resource accounting.

## RDMA in Containers: Fundamentals

A container is a process (or group of processes) running on the host kernel with namespace isolation for PID, network, mount, and other resource views. For RDMA to work inside a container, several requirements must be met:

### Device Access

RDMA devices are exposed through character device files under `/dev/infiniband/`:

- `/dev/infiniband/uverbsN` -- user-space verbs access (one per RDMA device)
- `/dev/infiniband/rdma_cm` -- RDMA Connection Manager
- `/dev/infiniband/issm*`, `/dev/infiniband/umad*` -- management interfaces

A container must have access to these device files to use RDMA. In Docker, this is accomplished with the `--device` flag:

```bash
# Grant access to RDMA devices
docker run --device=/dev/infiniband/uverbs0 \
           --device=/dev/infiniband/rdma_cm \
           -it rdma-app:latest

# Or grant access to all infiniband devices
docker run --device=/dev/infiniband/ \
           -it rdma-app:latest
```

<div class="warning">

Simply exposing `/dev/infiniband/` devices to a container provides RDMA functionality but **does not provide isolation**. Without additional mechanisms, a container with access to an RDMA device can see and interact with RDMA resources (QPs, MRs, CQs) created by other containers sharing the same physical device. This is a security concern in multi-tenant environments. RDMA namespace isolation (discussed below) addresses this limitation.

</div>

### Network Namespace Considerations

Standard Linux network namespaces isolate TCP/IP networking: each namespace has its own network interfaces, routing tables, and firewall rules. However, RDMA devices historically existed outside the network namespace model -- all containers and processes on a host shared the same view of RDMA devices and resources.

This created a fundamental tension: containerized RDMA applications could see each other's resources, violating the isolation guarantees that containers are expected to provide.

## RDMA Namespace Isolation

Starting with Linux kernel 5.3, the kernel supports **RDMA network namespace isolation**.[^1] When enabled, each network namespace has its own isolated view of RDMA devices and resources. RDMA devices can be assigned to specific network namespaces, and processes in one namespace cannot see or access RDMA resources in another.

### Modes of Operation

The RDMA subsystem supports two namespace modes, controlled per device:

**Shared mode** (default): The RDMA device is visible to all network namespaces. All containers sharing the device can see each other's resources. This is the legacy behavior.

**Exclusive mode**: The RDMA device is assigned to a specific network namespace. Only processes in that namespace can access the device, and they have a fully isolated view of RDMA resources. In exclusive mode, each namespace sees its own independent QP number space, PD handles, and MR keys -- a process in one namespace cannot forge an rkey to access another namespace's memory regions, even if it knows the rkey value. This is critical for multi-tenant RDMA security: without exclusive mode, rkey guessing attacks are trivial because the NIC hardware trusts any rkey presented by any process sharing the device.

```bash
# Check current RDMA namespace mode
rdma system show netns

# Set system-wide RDMA namespace mode to exclusive
# (this is a system-level setting, not per-device)
rdma system set netns exclusive

# Create a network namespace and move an RDMA device into it
ip netns add rdma_ns1
rdma dev set mlx5_0 netns rdma_ns1

# Verify the device is in the namespace
ip netns exec rdma_ns1 rdma dev show
```

### RDMA Cgroup Controller

The RDMA cgroup controller[^5] limits the RDMA resources that a container (cgroup) can consume. This prevents a single container from exhausting shared NIC resources such as QPs, CQs, MRs, and completion vectors.

```bash
# Set RDMA resource limits for a cgroup
# cgroup v1 path (legacy):
echo "mlx5_0 hca_handle=32 hca_object=4096" > \
    /sys/fs/cgroup/rdma/container1/rdma.max

# cgroup v2 path (current, used by systemd-based distros):
# echo "mlx5_0 hca_handle=32 hca_object=4096" > \
#     /sys/fs/cgroup/container1/rdma.max

# Resource types that can be limited:
#   hca_handle  - Number of RDMA device handles
#   hca_object  - Total number of RDMA objects (QPs, CQs, MRs, etc.)

# Check current usage
cat /sys/fs/cgroup/rdma/container1/rdma.current
```

<div class="note">

The RDMA cgroup controller provides coarse-grained resource limits (total objects) rather than fine-grained per-resource-type limits. For more detailed resource management, rely on the NIC's SR-IOV VF resource limits or application-level controls. The cgroup controller is primarily a safety mechanism to prevent resource exhaustion, not a precise allocation tool.

</div>

## Kubernetes RDMA Integration

Kubernetes requires explicit mechanisms to discover, advertise, and schedule RDMA devices for pods. The Kubernetes device plugin framework provides this capability.

### RDMA Device Plugins

The **k8s-rdma-shared-dev-plugin**[^2] (also known as the RDMA shared device plugin) is the most widely used plugin for making RDMA devices available to Kubernetes pods. It discovers RDMA devices on each node and advertises them as extended resources that pods can request.

**Shared mode**: Multiple pods share the same RDMA device. The plugin provides access to the device files and ensures proper permissions, but isolation depends on the kernel's RDMA namespace support.

```yaml
# Device plugin DaemonSet configuration
apiVersion: v1
kind: ConfigMap
metadata:
  name: rdma-devices
  namespace: kube-system
data:
  config.json: |
    {
      "periodicUpdateInterval": 300,
      "configList": [
        {
          "resourceName": "rdma_shared_device_a",
          "rdmaHcaMax": 1000,
          "selectors": {
            "ifNames": ["ens2f0"]
          }
        }
      ]
    }
```

**Pod requesting RDMA resources:**

```yaml
apiVersion: v1
kind: Pod
metadata:
  name: rdma-workload
spec:
  containers:
  - name: app
    image: rdma-app:latest
    resources:
      limits:
        rdma/rdma_shared_device_a: 1
    securityContext:
      capabilities:
        add: ["IPC_LOCK"]  # Required for memory registration
```

<div class="tip">

The `IPC_LOCK` capability is essential for RDMA in containers. RDMA requires pinning memory pages (via `mlock()`) to prevent them from being swapped out, as the NIC accesses physical memory directly via DMA. Without `IPC_LOCK`, memory registration calls will fail with `EPERM`. In Kubernetes, add this capability via the pod's security context.

For large memory registrations, also consider requesting **hugepages** (`hugepages-2Mi` or `hugepages-1Gi` resources). Hugepages reduce TLB pressure during RDMA operations and speed up memory registration because the kernel pins fewer, larger pages. Many high-performance RDMA applications allocate their buffer pools from hugepage-backed memory for this reason.

</div>

### SR-IOV Network Operator

For deployments that require hardware-isolated RDMA (SR-IOV VFs assigned to individual pods), the **SR-IOV Network Operator**[^3] automates the lifecycle management of SR-IOV VFs in Kubernetes:

1. **Discovery**: The operator discovers SR-IOV-capable NICs on each node.
2. **VF creation**: It creates VFs according to a `SriovNetworkNodePolicy` configuration.
3. **Device plugin**: It runs the SR-IOV device plugin to advertise VFs as Kubernetes resources.
4. **CNI plugin**: It provides an SR-IOV CNI plugin that attaches VFs to pod network namespaces.

```yaml
# SriovNetworkNodePolicy - create 8 VFs with RDMA
apiVersion: sriovnetwork.openshift.io/v1
kind: SriovNetworkNodePolicy
metadata:
  name: rdma-policy
  namespace: sriov-network-operator
spec:
  nodeSelector:
    feature.node.kubernetes.io/network-sriov.capable: "true"
  resourceName: rdma_sriov
  numVfs: 8
  nicSelector:
    vendor: "15b3"        # Mellanox/NVIDIA
    deviceID: "101b"      # ConnectX-6
    pfNames: ["ens2f0"]
  deviceType: netdevice
  isRdma: true            # Enable RDMA on VFs
---
# SriovNetwork - define network attachment
apiVersion: sriovnetwork.openshift.io/v1
kind: SriovNetwork
metadata:
  name: rdma-network
  namespace: sriov-network-operator
spec:
  resourceName: rdma_sriov
  networkNamespace: default
  ipam: |
    { "type": "host-local", "subnet": "192.168.1.0/24" }
```

**Pod using SR-IOV RDMA:**

```yaml
apiVersion: v1
kind: Pod
metadata:
  name: rdma-sriov-pod
  annotations:
    k8s.v1.cni.cncf.io/networks: rdma-network
spec:
  containers:
  - name: app
    image: rdma-app:latest
    resources:
      limits:
        # The resource prefix depends on your SR-IOV device plugin
        # configuration. On OpenShift the default is "openshift.io/",
        # on vanilla K8s it is typically configured in the device
        # plugin's config. The suffix must match resourceName from
        # your SriovNetworkNodePolicy.
        openshift.io/rdma_sriov: 1
    securityContext:
      capabilities:
        add: ["IPC_LOCK"]
```

### NVIDIA Network Operator

The **NVIDIA Network Operator**[^4] (formerly Mellanox Network Operator) provides a comprehensive solution for deploying RDMA-capable networking in Kubernetes. It manages the complete stack:

- **NIC driver installation**: Deploys OFED (OpenFabrics Enterprise Distribution) drivers on nodes via a DaemonSet.
- **Device plugin**: Manages RDMA and SR-IOV device plugins.
- **CNI plugins**: Deploys and configures network CNI plugins (Multus, SR-IOV CNI, IPoIB CNI).
- **RDMA shared device plugin**: Configures shared RDMA device access.
- **Secondary network configuration**: Manages non-default network interfaces for RDMA traffic.

```yaml
# NicClusterPolicy - comprehensive RDMA networking setup
apiVersion: mellanox.com/v1alpha1
kind: NicClusterPolicy
metadata:
  name: nic-cluster-policy
spec:
  ofedDriver:
    image: doca-driver
    repository: nvcr.io/nvidia/mellanox
    version: "24.04-0.6.6.0-0"
  rdmaSharedDevicePlugin:
    image: k8s-rdma-shared-dev-plugin
    repository: ghcr.io/mellanox
    version: "1.4.0"
    config: |
      {
        "configList": [
          {
            "resourceName": "rdma_shared_device_a",
            "rdmaHcaMax": 63,
            "selectors": { "ifNames": ["ens2f0"] }
          }
        ]
      }
  secondaryNetwork:
    multus:
      image: multus-cni
      repository: ghcr.io/k8snetworkplumbingwg
      version: "v4.0.2"
```

### CNI Plugin Considerations

Kubernetes networking is implemented through CNI (Container Network Interface) plugins. For RDMA workloads, several CNI-related considerations apply:

**Multus CNI**: Kubernetes pods have a single default network interface. RDMA traffic typically requires a separate, high-performance network interface. **Multus** is a meta-CNI plugin that enables attaching multiple network interfaces to a pod -- one for standard Kubernetes networking and additional interfaces for RDMA.

**IPoIB CNI**: For InfiniBand deployments, the IPoIB (IP over InfiniBand) CNI plugin creates IPoIB network interfaces inside pods, providing both IP connectivity and RDMA access over the InfiniBand fabric.

**Macvlan/IPVLAN**: For RoCE deployments, macvlan or ipvlan CNI plugins can provide pods with direct access to the host's RDMA-capable network interface.

## Example: Complete Kubernetes RDMA Deployment

The following example demonstrates deploying an RDMA-capable application on Kubernetes with SR-IOV:

```yaml
# 1. NetworkAttachmentDefinition for RDMA network
apiVersion: k8s.cni.cncf.io/v1
kind: NetworkAttachmentDefinition
metadata:
  name: rdma-net
  annotations:
    k8s.v1.cni.cncf.io/resourceName: rdma/rdma_sriov
spec:
  config: |
    {
      "cniVersion": "0.3.1",
      "type": "sriov",
      "vlan": 100,
      "ipam": {
        "type": "host-local",
        "subnet": "10.56.0.0/16",
        "rangeStart": "10.56.1.10",
        "rangeEnd": "10.56.1.250"
      }
    }
---
# 2. MPI Job using RDMA (with mpi-operator)
apiVersion: kubeflow.org/v2beta1
kind: MPIJob
metadata:
  name: rdma-benchmark
spec:
  slotsPerWorker: 1
  runPolicy:
    cleanPodPolicy: Running
  mpiReplicaSpecs:
    Launcher:
      replicas: 1
      template:
        spec:
          containers:
          - name: launcher
            image: mpi-benchmarks:latest
            command:
            - mpirun
            - --allow-run-as-root
            - -np
            - "4"
            - --bind-to
            - none
            - /usr/local/bin/osu_latency
    Worker:
      replicas: 4
      template:
        metadata:
          annotations:
            k8s.v1.cni.cncf.io/networks: rdma-net
        spec:
          containers:
          - name: worker
            image: mpi-benchmarks:latest
            resources:
              limits:
                rdma/rdma_sriov: 1
                cpu: "4"
                memory: "8Gi"
            securityContext:
              capabilities:
                add: ["IPC_LOCK"]
```

<div class="warning">

RDMA in Kubernetes introduces security considerations beyond standard container isolation. Without IOMMU, an RDMA device can DMA to arbitrary physical memory addresses, bypassing kernel memory protection entirely. **Always ensure IOMMU is enabled** (`intel_iommu=on` or `amd_iommu=on`) on all RDMA-capable nodes -- with IOMMU active, DMA is restricted to memory regions the kernel has explicitly mapped for that device's IOMMU domain, preventing unauthorized memory access. Additionally, restrict RDMA device access to trusted workloads using Kubernetes RBAC and admission controllers, and enable RDMA namespace isolation (exclusive mode) for multi-tenant environments.

</div>

## Scheduling Considerations

Kubernetes scheduling for RDMA workloads must account for hardware topology:

- **NUMA affinity**: RDMA performance is sensitive to NUMA topology. Use the Topology Manager (kubelet `--topology-manager-policy=single-numa-node`) to ensure that a pod's CPUs, memory, and RDMA device are all on the same NUMA node.
- **GPU-NIC affinity**: For ML workloads using GPUDirect RDMA, the GPU and RDMA NIC should be on the same PCIe root complex (ideally the same PCIe switch) to avoid crossing the CPU's root complex, which adds latency and consumes CPU memory bandwidth. The NVIDIA GPU Operator and Network Operator work together to enforce this affinity. On multi-socket systems, a GPU and NIC on different NUMA nodes can halve GPUDirect RDMA throughput due to cross-socket QPI/UPI traffic.
- **Resource accounting**: Ensure that the device plugin correctly reports the number of available RDMA devices (shared or VF-based) to prevent over-subscription.

```yaml
# Pod with topology-aware scheduling
spec:
  containers:
  - name: app
    resources:
      limits:
        nvidia.com/gpu: 1
        rdma/rdma_sriov: 1
        cpu: "8"
        memory: "32Gi"
  topologySpreadConstraints:
  - maxSkew: 1
    topologyKey: kubernetes.io/hostname
    whenUnsatisfiable: DoNotSchedule
```

The RDMA-in-Kubernetes stack has matured substantially. The combination of SR-IOV Network Operator, NVIDIA Network Operator, and Kubernetes-native resource management makes it practical to deploy RDMA workloads in containers with proper isolation, scheduling, and lifecycle management -- though the operational complexity of managing OFED drivers, device plugins, CNI plugins, and topology-aware scheduling across a fleet should not be underestimated.

[^1]: RDMA network namespace support was merged for Linux 5.3. See Parav Pandit's patchset and the [`rdma-system(8)` man page](https://www.man7.org/linux/man-pages/man8/rdma-system.8.html) for the `netns` subcommand documentation.

[^2]: [Mellanox/k8s-rdma-shared-dev-plugin](https://github.com/Mellanox/k8s-rdma-shared-dev-plugin) on GitHub. The plugin advertises RDMA devices as Kubernetes extended resources under the `rdma/` prefix.

[^3]: [k8snetworkplumbingwg/sriov-network-operator](https://github.com/k8snetworkplumbingwg/sriov-network-operator) on GitHub. Despite the `sriovnetwork.openshift.io` API group name (a historical artifact from the project's Red Hat origins), the operator works on vanilla Kubernetes as well as OpenShift.

[^4]: [Mellanox/network-operator](https://github.com/Mellanox/network-operator) on GitHub. The `NicClusterPolicy` CRD uses the `mellanox.com/v1alpha1` API group. See the [NVIDIA Network Operator documentation](https://docs.nvidia.com/networking/display/kubernetes2570/deployment-guide-kubernetes.html) for deployment guides.

[^5]: The RDMA cgroup controller is documented in the [Linux kernel RDMA controller documentation](https://docs.kernel.org/admin-guide/cgroup-v1/rdma.html). It is supported in both cgroup v1 and v2 (since kernel 4.11). The two resource types, `hca_handle` and `hca_object`, provide coarse-grained limits on RDMA device handles and total RDMA objects respectively.
