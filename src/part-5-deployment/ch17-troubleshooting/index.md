# Chapter 17: Troubleshooting

RDMA problems are notoriously difficult to diagnose. The very features that make RDMA fast -- kernel bypass, hardware-driven protocol processing, and zero-copy data movement -- also remove the software layers where traditional debugging tools operate. When a TCP connection fails, you can trace the packet through the kernel's networking stack, inspect it with tcpdump, and examine socket state with ss or netstat. When an RDMA operation fails, the NIC hardware returns a terse error code, and the diagnosis often requires reading hardware counters, understanding NIC firmware behavior, and reasoning about state machines that execute entirely in silicon.

This chapter provides a systematic approach to diagnosing RDMA problems. We begin with the diagnostic tools available in the RDMA ecosystem -- from basic device information utilities to performance testing tools and packet capture for RoCE. Many problems that seem application-specific are actually infrastructure issues that these tools can quickly identify.

We then examine hardware counters and monitoring, which are the primary source of visibility into RDMA operations. Unlike traditional networking where software processes every packet and can log at any point, RDMA operations execute in NIC hardware, and hardware counters are often the only evidence of what happened. Understanding which counters to watch, what they mean, and how to set up monitoring and alerting is essential for operating RDMA infrastructure in production.

The bulk of the chapter covers common failure modes and their diagnoses. RDMA error codes are specific but not always intuitive. A "transport retry counter exceeded" error could indicate anything from a misconfigured MTU to a failed network switch to a Priority Flow Control storm. We catalog the most common errors, their typical root causes, and the diagnostic steps to identify the specific issue.

Finally, we present a debugging methodology -- a systematic checklist that works from the physical layer up through the application, ensuring that no common cause is overlooked. This methodology also covers RDMA-specific debugging challenges such as fork safety, multi-threaded verbs interactions, and enabling debug output from rdma-core.

Whether you are bringing up a new RDMA deployment, diagnosing a production outage, or tracking down a subtle performance regression, the tools and techniques in this chapter will help you find the root cause efficiently.
