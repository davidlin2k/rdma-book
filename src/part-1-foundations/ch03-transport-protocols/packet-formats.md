# 3.4 Packet Formats

Understanding RDMA at the byte level is essential for debugging, performance analysis, and writing tools that interact with the wire protocol. This section dissects the headers used by InfiniBand and RoCE v2, byte by byte, with ASCII art diagrams that match the actual bit layout on the wire. All multi-byte fields are in network byte order (big-endian).

## InfiniBand Headers

An InfiniBand packet consists of a series of headers followed by the payload and integrity checks. The header chain varies by packet type, but the most common structure for an RDMA Write on a Reliable Connection within a single subnet is:

```
LRH (8B) + BTH (12B) + RETH (16B) + Payload + ICRC (4B) + VCRC (2B)
```

For inter-subnet traffic, a GRH is inserted between LRH and BTH:

```
LRH (8B) + GRH (40B) + BTH (12B) + RETH (16B) + Payload + ICRC (4B) + VCRC (2B)
```

### LRH: Local Route Header (8 bytes)

The LRH is the first header in every InfiniBand packet. It provides the information needed for switches to forward the packet within a subnet.

```
 0                   1                   2                   3
 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|  VL   |LVer |  SL   |  Rsv|LNH|       DLID (16 bits)        |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|  Rsv  |     Pkt Length      |         SLID (16 bits)        |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
```

| Field       | Bits  | Description |
|-------------|-------|-------------|
| **VL**      | 4     | Virtual Lane (0--14 for data, 15 for management) |
| **LVer**    | 4     | Link Version (must be 0) |
| **SL**      | 4     | Service Level (0--15), used for QoS classification |
| **Rsv**     | 2     | Reserved |
| **LNH**     | 2     | Link Next Header: `00` = raw, `01` = IPv6 (indicates GRH follows), `10` = BTH follows, `11` = GRH follows |
| **DLID**    | 16    | Destination Local Identifier |
| **Rsv**     | 5     | Reserved |
| **Pkt Length** | 11 | Packet length in 4-byte words (includes all headers from LRH through VCRC) |
| **SLID**    | 16    | Source Local Identifier |

<div class="note">

The LNH field tells the receiver what follows the LRH. For intra-subnet traffic without a GRH, LNH = `10` (BTH follows). For inter-subnet traffic or RoCE v1, LNH = `11` (GRH follows). Switches use only the DLID for forwarding; they do not look past the LRH.

</div>

### GRH: Global Route Header (40 bytes)

The GRH is identical in format to an IPv6 header. It is used for inter-subnet routing and is mandatory in RoCE v1 (where it carries the GID addressing information).

```
 0                   1                   2                   3
 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|IPVer=6| Traffic Class |           Flow Label (20 bits)        |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|       Payload Length          |  Next Header  |   Hop Limit   |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                                                               |
|                    Source GID (128 bits)                       |
|                                                               |
|                                                               |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                                                               |
|                  Destination GID (128 bits)                    |
|                                                               |
|                                                               |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
```

| Field              | Bits | Description |
|--------------------|------|-------------|
| **IP Version**     | 4    | Always 6 (IPv6 format) |
| **Traffic Class**  | 8    | QoS marking; maps to DSCP in RoCE v2 |
| **Flow Label**     | 20   | Used for flow identification; can provide ECMP entropy |
| **Payload Length** | 16   | Length of everything after the GRH, in bytes |
| **Next Header**    | 8    | Protocol of next header: 0x1B = IB BTH |
| **Hop Limit**      | 8    | Decremented by each router; packet discarded at 0 |
| **Source GID**     | 128  | Sender's Global Identifier |
| **Destination GID**| 128  | Receiver's Global Identifier |

### BTH: Base Transport Header (12 bytes)

The BTH is present in every InfiniBand transport packet. It identifies the transport service, the destination QP, and the packet sequence number.

```
 0                   1                   2                   3
 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|    Opcode     |S|M|Pad|  TVer |         P_Key (16 bits)       |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
| F | B | Rsv |             Dest QPN (24 bits)                  |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|A| Reserved  |              PSN (24 bits)                      |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
```

| Field       | Bits | Description |
|-------------|------|-------------|
| **Opcode**  | 8    | Operation code, encodes transport type + operation. Upper 3 bits = transport (RC=0x00, UC=0x20, RD=0x40, UD=0x60), lower 5 bits = operation (Send First=0x00, Send Last=0x02, RDMA Write First=0x06, etc.) |
| **SE**      | 1    | Solicited Event --- triggers a completion notification on the receiver |
| **M**       | 1    | Migration state (used for path migration, APM) |
| **Pad**     | 2    | Number of pad bytes added to payload to achieve 4-byte alignment (0--3) |
| **TVer**    | 4    | Transport Header Version (must be 0) |
| **P_Key**   | 16   | Partition Key --- receiver checks this against its P_Key table |
| **F**       | 1    | FECN (Forward Explicit Congestion Notification) |
| **B**       | 1    | BECN (Backward Explicit Congestion Notification) |
| **Rsv**     | 6    | Reserved |
| **Dest QPN**| 24   | Destination Queue Pair Number (identifies the target QP) |
| **A**       | 1    | Acknowledge Request --- asks receiver to send an ACK |
| **Rsv**     | 7    | Reserved |
| **PSN**     | 24   | Packet Sequence Number (0 to 2^24 - 1, wraps around) |

The opcode byte is densely packed with information. Some common opcodes:

| Opcode | Transport | Operation |
|--------|-----------|-----------|
| 0x00   | RC        | Send First |
| 0x02   | RC        | Send Last |
| 0x04   | RC        | Send Only |
| 0x06   | RC        | RDMA Write First |
| 0x08   | RC        | RDMA Write Last |
| 0x0A   | RC        | RDMA Write Only |
| 0x0C   | RC        | RDMA Read Request |
| 0x10   | RC        | RDMA Read Response First |
| 0x11   | RC        | Acknowledge |
| 0x14   | RC        | Atomic Acknowledge |

<div class="note">

The 24-bit PSN limits the sequence space to 16 million packets. For a 200 Gbps link sending 4 KB packets, this wraps every ~2.6 seconds. The transport protocol must handle wrap-around correctly, and the retransmission window must never span more than half the sequence space (8 million packets) to avoid ambiguity.

</div>

## Extended Transport Headers

Following the BTH, certain operations require additional headers:

### RETH: RDMA Extended Transport Header (16 bytes)

Present in RDMA Write First/Only and RDMA Read Request packets. Specifies the remote memory location.

```
 0                   1                   2                   3
 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                 Virtual Address (64 bits)                      |
|                                                               |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                      R_Key (32 bits)                          |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                   DMA Length (32 bits)                         |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
```

| Field              | Bits | Description |
|--------------------|------|-------------|
| **Virtual Address** | 64  | Remote virtual address where data is read from or written to |
| **R_Key**          | 32   | Remote Key that authorizes access to the memory region |
| **DMA Length**     | 32   | Total length of the RDMA operation in bytes (max 2^31 - 1) |

### AETH: ACK Extended Transport Header (4 bytes)

Present in Acknowledge, RDMA Read Response First/Last/Only, and Atomic Acknowledge packets.

```
 0                   1                   2                   3
 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|Syndrome |          MSN (24 bits)                              |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
```

| Field        | Bits | Description |
|--------------|------|-------------|
| **Syndrome** | 8    | ACK type: `00` = ACK, `01` = RNR NAK (receiver not ready), `10` = reserved, `11` = NAK (error). Lower 5 bits encode error codes or RNR timer values. |
| **MSN**      | 24   | Message Sequence Number --- counts completed messages, not packets |

### AtomicETH: Atomic Extended Transport Header (28 bytes)

Present in Compare-and-Swap and Fetch-and-Add request packets.

```
 0                   1                   2                   3
 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                 Virtual Address (64 bits)                      |
|                                                               |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                      R_Key (32 bits)                          |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                    Swap / Add Data (64 bits)                   |
|                                                               |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                   Compare Data (64 bits)                       |
|                  (unused for Fetch-and-Add)                    |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
```

For Fetch-and-Add, the "Swap/Add Data" field contains the value to add, and the "Compare Data" field is ignored. For Compare-and-Swap, both fields are used.

### ImmDt: Immediate Data (4 bytes)

An optional 4-byte field that can accompany Send and RDMA Write operations. The immediate data is delivered to the receiver as part of the completion event, enabling the sender to pass a small amount of out-of-band information without consuming a receive buffer (in the case of RDMA Write with Immediate).

```
 0                   1                   2                   3
 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                   Immediate Data (32 bits)                     |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
```

## RoCE v2 Headers

RoCE v2 replaces the LRH (and GRH) with standard Ethernet, IP, and UDP headers. Everything from the BTH onward is identical to native InfiniBand.

### Ethernet Header (14 bytes, or 18 with VLAN)

```
 0                   1                   2                   3
 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|              Destination MAC Address (48 bits)                |
|                               +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                               |       Source MAC Address      |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+                               |
|                (48 bits)      |    EtherType (0x0800 IPv4,    |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+     0x86DD IPv6, or           |
                                      0x8100 VLAN)
```

When a VLAN tag is present (EtherType 0x8100), 4 additional bytes are inserted:

```
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
| TPID (0x8100) |PCP|D|  VID (12 bits)   |  Inner EtherType    |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
```

The PCP (Priority Code Point, 3 bits) field is used for PFC priority marking. For RoCE, the PCP value determines which priority class the frame belongs to and whether it receives lossless treatment.

### IPv4 Header (20 bytes, no options)

```
 0                   1                   2                   3
 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|Ver= 4 |IHL= 5 |DSCP     |ECN|       Total Length             |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|        Identification         |Flags|  Fragment Offset        |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|     TTL       |Protocol=17(UDP)|     Header Checksum          |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                     Source IP Address                          |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                  Destination IP Address                        |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
```

Key fields for RoCE:

- **DSCP** (6 bits): Differentiated Services Code Point, used for QoS classification at L3. Switches map DSCP to traffic classes and forwarding behaviors.
- **ECN** (2 bits): Explicit Congestion Notification. RoCE packets are sent with ECN = `01` or `10` (ECN-capable), and switches set ECN = `11` (Congestion Experienced) when queues exceed thresholds.
- **Don't Fragment (DF)**: Must be set. RoCE packets must not be fragmented because the RNIC cannot reassemble IP fragments.
- **Protocol**: Always 17 (UDP).

### UDP Header (8 bytes)

```
 0                   1                   2                   3
 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|        Source Port (entropy)  |   Dest Port = 4791 (0x12B7)   |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|           Length              |         Checksum              |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
```

- **Source Port**: Chosen by the sender to provide ECMP entropy. Typically computed as a hash of source QPN, destination QPN, and optionally other fields. Range is usually 0xC000--0xFFFF.
- **Destination Port**: Always 4791 (IANA-assigned for RoCE v2).
- **Checksum**: Typically set to 0 (disabled) for RoCE v2, because the ICRC provides stronger integrity checking. Some implementations support UDP checksum for IPv6, where the UDP checksum is mandatory per RFC.

### BTH in RoCE v2

The BTH in RoCE v2 is bit-for-bit identical to the InfiniBand BTH described above. The same opcodes, the same fields, the same semantics. This is intentional --- it means the RNIC's transport engine can process the BTH identically regardless of whether the packet arrived over InfiniBand or Ethernet.

## Integrity: ICRC and VCRC

### ICRC: Invariant CRC (4 bytes)

The ICRC is a 32-bit CRC that covers all headers and payload that are **invariant** across the network --- that is, fields that are not modified by switches or routers. Specifically:

- **Covered**: BTH, all extended headers, payload, pad bytes.
- **Covered with substituted values**: Certain LRH/GRH fields are replaced with known constants before CRC computation (because routers may modify them). In RoCE v2, the IP header's TTL, DSCP, ECN, and Flow Label are substituted.
- **Not covered**: Ethernet FCS (which provides link-level integrity) and VCRC.

The ICRC provides end-to-end integrity from source RNIC to destination RNIC, surviving any modifications that switches or routers make to L2/L3 headers.

### VCRC: Variant CRC (2 bytes, InfiniBand only)

The VCRC is a 16-bit CRC that covers the entire packet including the LRH (which is "variant" because switches modify the VL field). The VCRC is checked and regenerated at each hop. VCRC exists only in native InfiniBand --- RoCE packets use the Ethernet FCS instead.

## Header Overhead Comparison

The total header overhead per protocol determines the efficiency for small messages and the maximum achievable message rate:

| Header Component | InfiniBand (intra-subnet) | InfiniBand (inter-subnet) | RoCE v1 | RoCE v2 (IPv4) | RoCE v2 (IPv6) |
|-----------------|--------------------------|--------------------------|---------|----------------|----------------|
| L2 Header       | LRH: 8B                 | LRH: 8B                 | Eth: 14B (+4B VLAN) | Eth: 14B (+4B VLAN) | Eth: 14B (+4B VLAN) |
| L3 Header       | ---                      | GRH: 40B                | GRH: 40B | IPv4: 20B     | IPv6: 40B      |
| L4 Header       | ---                      | ---                      | ---     | UDP: 8B        | UDP: 8B        |
| BTH             | 12B                      | 12B                     | 12B     | 12B            | 12B            |
| ICRC            | 4B                       | 4B                      | 4B      | 4B             | 4B             |
| VCRC/FCS        | 2B                       | 2B                      | 4B (FCS)| 4B (FCS)       | 4B (FCS)       |
| **Total (no ext. hdr)** | **26B**           | **66B**                 | **74B** | **62B**        | **82B**        |
| + RETH (Write)  | **42B**                  | **82B**                 | **90B** | **78B**        | **98B**        |

<div class="tip">

For small-message workloads (e.g., key-value stores sending 64-byte values), header overhead is a significant fraction of the total packet. With RoCE v2 over IPv4, a 64-byte payload results in a 78 + 64 = 142 byte packet on the wire (including RETH for an RDMA Write), meaning 55% of the bandwidth is consumed by headers. Switching from RDMA Write (which requires RETH) to Send (which does not) saves 16 bytes per packet. Using InfiniBand intra-subnet (26 + 64 = 90 bytes) reduces the overhead to 29%.

</div>

## Putting It All Together: A Complete RoCE v2 RDMA Write Packet

Here is the complete byte layout of a RoCE v2 RDMA Write Only packet carrying 64 bytes of payload, with IPv4 and no VLAN tag:

```
Bytes 0-13:    Ethernet Header (14B)
  [0-5]       Destination MAC
  [6-11]      Source MAC
  [12-13]     EtherType = 0x0800 (IPv4)

Bytes 14-33:   IPv4 Header (20B)
  [14]        Version=4, IHL=5
  [15]        DSCP + ECN
  [16-17]     Total Length = 124 (20+8+12+16+64+4)
  [18-19]     Identification
  [20-21]     Flags (DF=1) + Fragment Offset = 0
  [22]        TTL
  [23]        Protocol = 17 (UDP)
  [24-25]     Header Checksum
  [26-29]     Source IP
  [30-33]     Destination IP

Bytes 34-41:   UDP Header (8B)
  [34-35]     Source Port (entropy, e.g., 0xC1A3)
  [36-37]     Destination Port = 0x12B7 (4791)
  [38-39]     Length = 104 (8+12+16+64+4)
  [40-41]     Checksum = 0x0000

Bytes 42-53:   BTH (12B)
  [42]        Opcode = 0x0A (RC RDMA Write Only)
  [43]        SE=0, M=0, Pad=0, TVer=0
  [44-45]     P_Key
  [46-48]     Destination QPN (24 bits, with 8 reserved/flag bits)
  [49-51]     A=1, PSN (24 bits, with 8 reserved bits)

Bytes 54-69:   RETH (16B)
  [54-61]     Virtual Address (64 bits)
  [62-65]     R_Key (32 bits)
  [66-69]     DMA Length = 64 (32 bits)

Bytes 70-133:  Payload (64B)
  [70-133]    Application data

Bytes 134-137: ICRC (4B)
  [134-137]   Invariant CRC-32

Bytes 138-141: Ethernet FCS (4B)
  [138-141]   Frame Check Sequence (CRC-32)
```

Total frame size: 142 bytes (not counting preamble, SFD, and inter-frame gap, which add 20 bytes of Ethernet overhead on the wire).

For a 4 KB RDMA Write, the payload increases to 4096 bytes, and the total frame is 4178 bytes. With jumbo frames (MTU 9000), this fits in a single frame. With standard 1500-byte MTU, it must be segmented into three RDMA Write First/Middle/Last packets by the RNIC (note: this is RDMA-level segmentation, not IP fragmentation --- the DF bit is always set).
