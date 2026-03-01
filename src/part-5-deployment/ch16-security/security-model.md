# 16.1 RDMA Security Model

## Trust the Endpoint, Not the Network

Traditional networking security is built around a fundamental assumption: the network is hostile. Firewalls sit at network boundaries inspecting every packet. Intrusion detection systems monitor traffic for malicious patterns. TLS encrypts data so that even if an attacker can observe the wire, they cannot read the contents. Every layer of the stack assumes that something between the sender and receiver might be compromised.

RDMA inverts this assumption. The RDMA security model trusts the endpoint -- specifically, it trusts that the software running on each endpoint has been properly authorized to perform the operations it requests. The network itself is assumed to be a trusted fabric. This trust model emerged naturally from RDMA's origins in High-Performance Computing, where all nodes in a cluster are managed by a single administrative domain and the network is physically isolated from the outside world.

In this model, if a process has been granted a memory key (R_Key) for a remote memory region, the NIC will faithfully execute read and write operations against that region without further checks. There is no per-operation authentication, no per-packet encryption, and no software-level authorization check on the remote side. The NIC validates the key in hardware and performs the operation at line rate.

## No Encryption on the Wire

By default, RDMA traffic traverses the network in plaintext. Neither InfiniBand nor RoCEv2 includes encryption in its base specification. Every byte of data, every memory address, and every memory key is visible to anyone who can observe the fabric.

For InfiniBand networks, this has historically been acceptable because the fabric is physically separate from other networks. An attacker would need physical access to the InfiniBand switches or cables to sniff traffic. For RoCEv2, which runs over standard Ethernet, the situation is more concerning -- RoCE traffic shares the same physical infrastructure as other Ethernet traffic, and standard network tapping techniques apply.

<div class="warning">

**Warning:** RDMA traffic is unencrypted by default. Memory keys, remote addresses, and application data are all visible in plaintext on the wire. In RoCE deployments sharing Ethernet infrastructure with untrusted traffic, this represents a significant data exposure risk.

</div>

Some newer hardware supports encryption offload. For example, certain ConnectX adapters can perform inline encryption/decryption (MACsec for Ethernet or IPsec offload), but this is not universally deployed and adds latency. The InfiniBand specification has also introduced optional encryption features in recent revisions, but adoption remains limited.

## No Authentication Beyond PD/Key Checks

RDMA does not have a built-in authentication protocol. There is no equivalent of TLS's certificate exchange, Kerberos ticket validation, or SSH key authentication within the RDMA transport itself. When two queue pairs (QPs) are connected, the connection setup involves exchanging QP numbers, memory keys, and addresses -- typically through an out-of-band mechanism like TCP sockets or the RDMA Connection Manager (RDMA CM). But RDMA itself does not verify the identity of the remote endpoint.

The access control that does exist operates at a hardware level:

- **Protection Domains (PDs)** isolate resources within a single host. A QP can only access memory regions registered in the same PD.
- **Memory Keys (R_Keys and L_Keys)** authorize access to specific memory regions. A valid key is required for any RDMA operation.
- **Partition Keys (P_Keys)**, in InfiniBand, segment the fabric into isolated partitions. Only QPs with matching P_Keys can communicate.

These mechanisms are necessary but not sufficient for secure multi-tenant deployments. They prevent accidental cross-contamination between well-behaved applications, but they were not designed to resist active attackers.

## Kernel Bypass: The Security Double-Edged Sword

Kernel bypass is RDMA's greatest performance advantage and its most significant security challenge. In traditional networking, the kernel mediates every network operation. This mediation provides a natural enforcement point for security policies: the kernel can check permissions, enforce quotas, filter packets, and log activity. With RDMA, the application maps the NIC's hardware registers directly into its address space and issues operations without any kernel involvement.

This means:

1. **The kernel cannot inspect RDMA traffic.** Once a QP is set up, data flows directly between the application and the NIC. Kernel-based firewalls, packet filters, and security modules are completely bypassed.

2. **The kernel cannot throttle RDMA operations.** An application with access to a QP can issue operations as fast as the hardware allows. There is no kernel-level rate limiting.

3. **The kernel cannot audit RDMA activity.** Traditional system call auditing tools (like `auditd`) do not see RDMA operations because they never pass through the kernel.

4. **Resource isolation depends on hardware.** Instead of software-enforced process isolation, RDMA relies on the NIC hardware to enforce protection domain boundaries and validate memory keys.

```text
Traditional Networking:
  App → Kernel → NIC → Wire
        ↑ Security checks happen here

RDMA (Kernel Bypass):
  App → NIC → Wire
  ↑ App has direct NIC access; kernel is not in the path
```

The kernel does remain involved during setup: creating protection domains, registering memory regions, and configuring QPs all require kernel calls through the uverbs interface. But once the data path is established, the kernel is out of the loop.

## Comparison with Traditional Networking Security

To appreciate what RDMA lacks, consider what traditional networking provides:

| Security Feature | Traditional Networking | RDMA |
|---|---|---|
| Encryption in transit | TLS/SSL, IPsec | None by default |
| Per-connection authentication | TLS certificates, Kerberos | None (application must implement) |
| Packet filtering/firewalling | iptables, nftables, hardware firewalls | Not applicable (kernel bypass) |
| Per-operation authorization | Application-level checks via kernel | Hardware key validation only |
| Traffic auditing | tcpdump, kernel logging | Limited (HW counters only) |
| Rate limiting | tc, kernel queuing | Hardware-level only |
| Intrusion detection | Snort, Suricata | No equivalent |

This is not a deficiency to be ashamed of -- it is a deliberate trade-off. Every security check adds latency. RDMA achieves sub-microsecond latencies precisely because it eliminates these checks. The goal is not to add all of them back, but to understand which ones are essential for your deployment and find efficient ways to provide them.

## The Shared-Everything Problem in Multi-Tenant Environments

In a single-tenant HPC cluster, the trust model works well. All nodes run trusted code, the fabric is physically isolated, and there is a single administrative domain. The security boundary is the physical perimeter of the cluster.

Multi-tenant environments shatter this model. In a cloud data center:

- **Multiple tenants share the same NIC** via SR-IOV Virtual Functions (VFs). Each VF provides a separate RDMA device, but they share the same physical hardware.
- **Multiple tenants share the same network fabric.** VLANs and ACLs provide logical isolation, but the packets traverse the same switches and cables.
- **Tenants do not trust each other.** A vulnerability that allows one tenant to access another's memory regions is a critical security breach.

The NIC hardware must enforce isolation between VFs, ensuring that one VF cannot access another's protection domains, memory regions, or QPs. Modern NICs generally do this correctly, but the attack surface is large and the consequences of a hardware bug are severe.

<div class="note">

**Note:** SR-IOV provides hardware-level isolation between Virtual Functions, but this isolation is only as strong as the NIC firmware's enforcement. NIC firmware vulnerabilities have been discovered in the past that could potentially weaken VF isolation. Keep firmware updated and monitor vendor security advisories.

</div>

## Hardware vs. Software Security Boundaries

RDMA shifts many security boundaries from software to hardware:

- **Memory protection** moves from kernel page tables to NIC memory key validation.
- **Process isolation** moves from kernel address space separation to protection domain enforcement in the NIC.
- **Network segmentation** moves from kernel packet filtering to NIC partition key checks (InfiniBand) or switch-level VLAN enforcement (RoCE).

Hardware enforcement is fast -- it adds zero latency to the data path -- but it is also rigid. Software security can be updated with a patch; hardware security requires firmware updates or, in worst cases, new hardware. Software security can implement complex, context-dependent policies; hardware security is limited to simple, predefined checks.

This means that RDMA security bugs tend to be more severe and harder to fix than traditional networking security bugs. A software vulnerability in a firewall can be patched and deployed in hours. A vulnerability in NIC memory key validation might require a firmware update across an entire fleet, with potential compatibility risks.

## Why RDMA Security Is an Evolving Area

RDMA security is under active development for several reasons:

1. **Expanding deployment scope.** As RDMA moves from isolated HPC clusters to multi-tenant cloud environments, the threat model changes dramatically.

2. **Hardware encryption offload.** NIC vendors are adding inline encryption capabilities that can protect RDMA traffic without software overhead. MACsec, IPsec offload, and proprietary encryption features are becoming available.

3. **Confidential computing integration.** Technologies like AMD SEV, Intel TDX, and ARM CCA create trusted execution environments. Integrating RDMA with these technologies -- ensuring that RDMA data paths remain within the trusted boundary -- is an active research area.

4. **New isolation mechanisms.** Memory Windows Type 2 provide fine-grained, revocable access control. Device Memory (DM) allows controlled access to NIC-attached memory. New features in each NIC generation improve the granularity and strength of hardware isolation.

5. **Standardization efforts.** The Ultra Ethernet Consortium and other standards bodies are working on next-generation transport protocols that incorporate security as a first-class design requirement rather than an afterthought.

The current state of RDMA security is adequate for its original use case -- trusted HPC clusters -- but requires careful supplementation for broader deployments. The remainder of this chapter provides the knowledge needed to understand and implement that supplementation.
