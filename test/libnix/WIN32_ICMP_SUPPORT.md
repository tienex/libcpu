# Windows ICMP (Internet Control Message Protocol) Support

Complete ICMP protocol implementation for Windows-hosted libnix, enabling network diagnostics like ping, traceroute, and path MTU discovery.

## Overview

This implementation provides two approaches to ICMP on Windows:

1. **Raw Sockets**: Full control over ICMP packets (requires Administrator privileges)
2. **Windows ICMP API**: Simplified ping using IcmpSendEcho (no admin required)

Both approaches are production-ready and compatible with Windows 2000+.

## Features

### Core Capabilities

- ✅ ICMP Echo Request/Reply (ping)
- ✅ ICMP Destination Unreachable
- ✅ ICMP Time Exceeded (for traceroute)
- ✅ IPv4 and IPv6 support
- ✅ TTL (Time to Live) control
- ✅ Custom payload support
- ✅ Checksum validation
- ✅ Round-trip time measurement
- ✅ Administrator privilege detection
- ✅ Timeout configuration

### ICMP Message Types Supported

| Type | Name | Code | Description |
|------|------|------|-------------|
| 0 | Echo Reply | 0 | Ping response |
| 3 | Destination Unreachable | 0-5 | Network/host/protocol/port unreachable |
| 4 | Source Quench | 0 | Congestion control (deprecated) |
| 5 | Redirect | 0-3 | Route redirection |
| 8 | Echo Request | 0 | Ping request |
| 9 | Router Advertisement | 0 | Router discovery |
| 10 | Router Solicitation | 0 | Router discovery |
| 11 | Time Exceeded | 0-1 | TTL exceeded / Fragment reassembly timeout |
| 12 | Parameter Problem | 0-2 | Bad IP header |
| 13 | Timestamp Request | 0 | Clock synchronization |
| 14 | Timestamp Reply | 0 | Clock synchronization response |
| 15 | Information Request | 0 | Network information (obsolete) |
| 16 | Information Reply | 0 | Network information response (obsolete) |
| 17 | Address Mask Request | 0 | Subnet mask query |
| 18 | Address Mask Reply | 0 | Subnet mask response |

## API Functions

### Raw Socket Approach

#### 1. Create ICMP Socket

```c
int nix_platform_win32_icmp_socket(int family);
```

**Parameters:**
- `family`: `AF_INET` for IPv4 or `AF_INET6` for IPv6

**Returns:**
- Socket descriptor on success
- -1 on error (errno set)

**Errors:**
- `EPERM`: Requires Administrator privileges
- `EAFNOSUPPORT`: Address family not supported
- `EPROTONOSUPPORT`: Protocol not supported

**Example:**
```c
/* Create ICMPv4 socket */
int sock = nix_platform_win32_icmp_socket(AF_INET);
if (sock < 0) {
    if (errno == EPERM) {
        fprintf(stderr, "Error: Administrator privileges required\n");
    }
    return -1;
}
```

#### 2. Send ICMP Echo Request

```c
ssize_t nix_platform_win32_icmp_echo_request(
    int sockfd,
    const struct sockaddr *dest_addr,
    socklen_t addrlen,
    uint16_t id,
    uint16_t sequence,
    const void *data,
    size_t data_len
);
```

**Parameters:**
- `sockfd`: ICMP socket descriptor
- `dest_addr`: Destination address (IPv4 or IPv6)
- `addrlen`: Size of address structure
- `id`: Identifier (typically process ID)
- `sequence`: Sequence number (incremented for each request)
- `data`: Optional payload data
- `data_len`: Payload length (typically 32-56 bytes)

**Returns:**
- Number of bytes sent on success
- -1 on error (errno set)

**Errors:**
- `EHOSTUNREACH`: Host unreachable
- `ENETUNREACH`: Network unreachable
- `EACCES`: Permission denied
- `EMSGSIZE`: Message too large

**Example:**
```c
struct sockaddr_in dest;
dest.sin_family = AF_INET;
dest.sin_addr.s_addr = inet_addr("8.8.8.8");

char payload[32] = "PING";
ssize_t sent = nix_platform_win32_icmp_echo_request(
    sock,
    (struct sockaddr *)&dest,
    sizeof(dest),
    getpid(),      /* ID */
    1,             /* Sequence */
    payload,
    sizeof(payload)
);
```

#### 3. Receive ICMP Echo Reply

```c
ssize_t nix_platform_win32_icmp_echo_reply(
    int sockfd,
    struct sockaddr *src_addr,
    socklen_t *addrlen,
    uint16_t *id,
    uint16_t *sequence,
    void *data,
    size_t data_len,
    uint8_t *ttl
);
```

**Parameters:**
- `sockfd`: ICMP socket descriptor
- `src_addr`: Source address buffer (filled on return)
- `addrlen`: Address buffer size / actual size (in/out)
- `id`: Identifier from reply (filled on return)
- `sequence`: Sequence number from reply (filled on return)
- `data`: Buffer for payload data
- `data_len`: Buffer size
- `ttl`: TTL value from reply (filled on return)

**Returns:**
- Payload length on success
- -1 on error (errno set)

**Errors:**
- `ETIMEDOUT`: Receive timeout
- `EHOSTUNREACH`: Destination unreachable
- `EBADMSG`: Checksum error
- `EPROTO`: Unexpected ICMP type

**Example:**
```c
struct sockaddr_in from;
socklen_t fromlen = sizeof(from);
uint16_t reply_id, reply_seq;
uint8_t ttl;
char buffer[1024];

ssize_t received = nix_platform_win32_icmp_echo_reply(
    sock,
    (struct sockaddr *)&from,
    &fromlen,
    &reply_id,
    &reply_seq,
    buffer,
    sizeof(buffer),
    &ttl
);

if (received >= 0) {
    printf("Reply from %s: bytes=%zd ttl=%d seq=%d\n",
           inet_ntoa(from.sin_addr), received, ttl, reply_seq);
}
```

### Simplified Ping Approach

#### 4. Simple Ping Function

```c
int nix_platform_win32_icmp_ping(
    const char *host,
    uint32_t timeout_ms,
    uint32_t *rtt_ms,
    uint8_t *ttl
);
```

**Parameters:**
- `host`: Hostname or IP address string
- `timeout_ms`: Timeout in milliseconds
- `rtt_ms`: Round-trip time in ms (filled on return)
- `ttl`: TTL value from reply (filled on return)

**Returns:**
- 0 on success
- -1 on error (errno set)

**Errors:**
- `ETIMEDOUT`: Request timed out
- `EHOSTUNREACH`: Host unreachable
- `EIO`: Other network error

**Example:**
```c
uint32_t rtt;
uint8_t ttl;

if (nix_platform_win32_icmp_ping("google.com", 5000, &rtt, &ttl) == 0) {
    printf("Ping successful: rtt=%ums ttl=%d\n", rtt, ttl);
} else {
    printf("Ping failed: %s\n", strerror(errno));
}
```

**Advantages:**
- ✅ No Administrator privileges required
- ✅ Hostname resolution included
- ✅ Simplified API
- ✅ Automatic retry logic

**Disadvantages:**
- ❌ Less control over ICMP packets
- ❌ Cannot receive non-echo ICMP messages
- ❌ Fixed packet format

### Socket Options

#### 5. Set TTL

```c
int nix_platform_win32_icmp_set_ttl(int sockfd, int ttl);
```

**Parameters:**
- `sockfd`: ICMP socket descriptor
- `ttl`: Time to Live (1-255)

**Example:**
```c
/* Set TTL to 64 hops */
nix_platform_win32_icmp_set_ttl(sock, 64);
```

#### 6. Get TTL

```c
int nix_platform_win32_icmp_get_ttl(int sockfd);
```

**Returns:**
- Current TTL value
- -1 on error

#### 7. Set Timeout

```c
int nix_platform_win32_icmp_set_timeout(int sockfd, uint32_t timeout_ms);
```

**Parameters:**
- `sockfd`: ICMP socket descriptor
- `timeout_ms`: Receive timeout in milliseconds

**Example:**
```c
/* Set 2-second timeout */
nix_platform_win32_icmp_set_timeout(sock, 2000);
```

### Utility Functions

#### 8. Get ICMP Type Name

```c
const char *nix_platform_win32_icmp_type_name(uint8_t type);
```

**Returns:** Human-readable ICMP type name

**Example:**
```c
printf("ICMP Type: %s\n", nix_platform_win32_icmp_type_name(8));
/* Output: "Echo Request" */
```

#### 9. Check Administrator Privileges

```c
int nix_platform_win32_icmp_check_admin(void);
```

**Returns:**
- 1 if running as Administrator
- 0 otherwise

**Example:**
```c
if (!nix_platform_win32_icmp_check_admin()) {
    fprintf(stderr, "Warning: Not running as Administrator\n");
    fprintf(stderr, "Raw ICMP sockets will not work\n");
}
```

## Usage Examples

### Example 1: Basic Ping

```c
#include "nix-platform-win32.h"
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <host>\n", argv[0]);
        return 1;
    }

    /* Use simplified ping API */
    uint32_t rtt;
    uint8_t ttl;

    printf("Pinging %s...\n", argv[1]);

    if (nix_platform_win32_icmp_ping(argv[1], 5000, &rtt, &ttl) == 0) {
        printf("Reply: rtt=%ums ttl=%d\n", rtt, ttl);
        return 0;
    } else {
        fprintf(stderr, "Ping failed: %s\n", strerror(errno));
        return 1;
    }
}
```

### Example 2: Advanced Ping with Raw Sockets

```c
#include "nix-platform-win32.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

int ping_host(const char *host, int count) {
    /* Check admin privileges */
    if (!nix_platform_win32_icmp_check_admin()) {
        fprintf(stderr, "Error: Administrator privileges required\n");
        return -1;
    }

    /* Create ICMP socket */
    int sock = nix_platform_win32_icmp_socket(AF_INET);
    if (sock < 0) {
        perror("socket");
        return -1;
    }

    /* Set socket options */
    nix_platform_win32_icmp_set_ttl(sock, 64);
    nix_platform_win32_icmp_set_timeout(sock, 5000);

    /* Resolve hostname */
    struct hostent *he = gethostbyname(host);
    if (he == NULL) {
        fprintf(stderr, "Cannot resolve %s\n", host);
        closesocket(sock);
        return -1;
    }

    struct sockaddr_in dest;
    memset(&dest, 0, sizeof(dest));
    dest.sin_family = AF_INET;
    memcpy(&dest.sin_addr, he->h_addr, he->h_length);

    printf("Pinging %s [%s] with 32 bytes of data:\n",
           host, inet_ntoa(dest.sin_addr));

    uint16_t id = getpid() & 0xFFFF;
    char payload[32] = "abcdefghijklmnopqrstuvwxyz012345";

    int success = 0;
    int total_rtt = 0;

    for (int seq = 0; seq < count; seq++) {
        /* Send echo request */
        clock_t start = clock();

        ssize_t sent = nix_platform_win32_icmp_echo_request(
            sock,
            (struct sockaddr *)&dest,
            sizeof(dest),
            id,
            seq,
            payload,
            sizeof(payload)
        );

        if (sent < 0) {
            fprintf(stderr, "Send failed: %s\n", strerror(errno));
            continue;
        }

        /* Receive echo reply */
        struct sockaddr_in from;
        socklen_t fromlen = sizeof(from);
        uint16_t reply_id, reply_seq;
        uint8_t ttl;
        char buffer[1024];

        ssize_t received = nix_platform_win32_icmp_echo_reply(
            sock,
            (struct sockaddr *)&from,
            &fromlen,
            &reply_id,
            &reply_seq,
            buffer,
            sizeof(buffer),
            &ttl
        );

        clock_t end = clock();
        int rtt = (int)((end - start) * 1000 / CLOCKS_PER_SEC);

        if (received >= 0 && reply_id == id && reply_seq == seq) {
            printf("Reply from %s: bytes=%zd time=%dms TTL=%d\n",
                   inet_ntoa(from.sin_addr), received, rtt, ttl);
            success++;
            total_rtt += rtt;
        } else if (errno == ETIMEDOUT) {
            printf("Request timed out.\n");
        } else {
            printf("Error: %s\n", strerror(errno));
        }

        Sleep(1000);  /* Wait 1 second between pings */
    }

    closesocket(sock);

    /* Print statistics */
    printf("\nPing statistics for %s:\n", inet_ntoa(dest.sin_addr));
    printf("    Packets: Sent = %d, Received = %d, Lost = %d (%.0f%% loss)\n",
           count, success, count - success,
           (count - success) * 100.0 / count);

    if (success > 0) {
        printf("Approximate round trip times in milli-seconds:\n");
        printf("    Average = %dms\n", total_rtt / success);
    }

    return (success > 0) ? 0 : -1;
}

int main(int argc, char *argv[]) {
    if (argc != 2 && argc != 3) {
        fprintf(stderr, "Usage: %s <host> [count]\n", argv[0]);
        return 1;
    }

    int count = (argc == 3) ? atoi(argv[2]) : 4;
    return ping_host(argv[1], count);
}
```

### Example 3: Traceroute Implementation

```c
#include "nix-platform-win32.h"
#include <stdio.h>
#include <stdlib.h>

int traceroute(const char *host, int max_hops) {
    if (!nix_platform_win32_icmp_check_admin()) {
        fprintf(stderr, "Error: Administrator privileges required\n");
        return -1;
    }

    int sock = nix_platform_win32_icmp_socket(AF_INET);
    if (sock < 0) {
        perror("socket");
        return -1;
    }

    /* Resolve destination */
    struct hostent *he = gethostbyname(host);
    if (he == NULL) {
        fprintf(stderr, "Cannot resolve %s\n", host);
        closesocket(sock);
        return -1;
    }

    struct sockaddr_in dest;
    memset(&dest, 0, sizeof(dest));
    dest.sin_family = AF_INET;
    memcpy(&dest.sin_addr, he->h_addr, he->h_length);

    printf("Tracing route to %s [%s]\n", host, inet_ntoa(dest.sin_addr));
    printf("over a maximum of %d hops:\n\n", max_hops);

    uint16_t id = getpid() & 0xFFFF;
    char payload[32] = "TRACE";

    for (int ttl = 1; ttl <= max_hops; ttl++) {
        printf("%3d  ", ttl);

        /* Set TTL */
        nix_platform_win32_icmp_set_ttl(sock, ttl);
        nix_platform_win32_icmp_set_timeout(sock, 2000);

        int attempts = 3;
        int reached = 0;

        for (int i = 0; i < attempts; i++) {
            clock_t start = clock();

            /* Send echo request */
            nix_platform_win32_icmp_echo_request(
                sock,
                (struct sockaddr *)&dest,
                sizeof(dest),
                id,
                ttl * 10 + i,
                payload,
                sizeof(payload)
            );

            /* Receive reply */
            struct sockaddr_in from;
            socklen_t fromlen = sizeof(from);
            uint16_t reply_id, reply_seq;
            uint8_t reply_ttl;
            char buffer[1024];

            ssize_t received = nix_platform_win32_icmp_echo_reply(
                sock,
                (struct sockaddr *)&from,
                &fromlen,
                &reply_id,
                &reply_seq,
                buffer,
                sizeof(buffer),
                &reply_ttl
            );

            clock_t end = clock();
            int rtt = (int)((end - start) * 1000 / CLOCKS_PER_SEC);

            if (received >= 0) {
                if (i == 0) {
                    printf("%-15s  ", inet_ntoa(from.sin_addr));
                }
                printf("%4dms  ", rtt);

                if (from.sin_addr.s_addr == dest.sin_addr.s_addr) {
                    reached = 1;
                }
            } else if (errno == ETIMEDOUT) {
                printf("  *  ");
            } else {
                printf(" (%s)  ", strerror(errno));
            }
        }

        printf("\n");

        if (reached) {
            printf("\nTrace complete.\n");
            break;
        }
    }

    closesocket(sock);
    return 0;
}

int main(int argc, char *argv[]) {
    if (argc != 2 && argc != 3) {
        fprintf(stderr, "Usage: %s <host> [max_hops]\n", argv[0]);
        return 1;
    }

    int max_hops = (argc == 3) ? atoi(argv[2]) : 30;
    return traceroute(argv[1], max_hops);
}
```

## Technical Details

### ICMP Packet Structure

```
0                   1                   2                   3
0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|     Type      |     Code      |          Checksum             |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|           Identifier          |        Sequence Number        |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                        Optional Data                          |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
```

### Checksum Calculation

The ICMP checksum is calculated using the RFC 1071 algorithm:

1. Sum all 16-bit words in the ICMP packet
2. Add any remaining byte (if odd length)
3. Fold 32-bit sum into 16 bits
4. Take one's complement

### Windows-Specific Considerations

#### Raw Sockets

- **Privileges**: Raw sockets require Administrator privileges on Windows 2000+
- **Firewall**: Windows Firewall may block ICMP by default
- **IP Header**: `IP_HDRINCL` socket option includes IP header in raw socket sends
- **Reception**: Windows includes IP header in received packets

#### IcmpSendEcho API

- **No Admin**: Works without Administrator privileges
- **Simplified**: Handles all packet construction internally
- **Limited**: Cannot receive non-echo ICMP messages
- **Blocking**: Always blocks until reply or timeout

### Error Handling

Comprehensive errno mapping:

| Windows Error | POSIX errno | Description |
|---------------|-------------|-------------|
| WSAEACCES | EPERM/EACCES | Permission denied (admin required) |
| WSAEHOSTUNREACH | EHOSTUNREACH | Host unreachable |
| WSAENETUNREACH | ENETUNREACH | Network unreachable |
| WSAETIMEDOUT | ETIMEDOUT | Operation timed out |
| IP_REQ_TIMED_OUT | ETIMEDOUT | ICMP request timed out |
| IP_DEST_HOST_UNREACHABLE | EHOSTUNREACH | Destination host unreachable |
| IP_TTL_EXPIRED_TRANSIT | ETIMEDOUT | TTL expired in transit |
| WSAEMSGSIZE | EMSGSIZE | Message too large |
| WSAECONNRESET | ECONNRESET | Connection reset |

## Performance

### Latency

Typical ping latency:
- Localhost: < 1ms
- LAN: 1-10ms
- Internet: 10-100ms
- Satellite: 500-700ms

### Throughput

ICMP is designed for control messages, not data transfer:
- Typical packet size: 32-64 bytes (payload)
- Total packet: 60-92 bytes (IP + ICMP + payload)
- Rate limit: Most networks limit ICMP to prevent flood attacks

## Security Considerations

1. **Administrator Privileges**: Raw sockets require elevated privileges
2. **Firewall Rules**: Ensure Windows Firewall allows ICMP
3. **Rate Limiting**: Implement rate limiting to prevent abuse
4. **Validation**: Always validate checksums and packet contents
5. **Source Address**: Verify source address matches expected host
6. **Buffer Overflow**: Use fixed-size buffers to prevent overflows

## Compatibility

| Windows Version | Raw Sockets | IcmpSendEcho | Status |
|----------------|-------------|--------------|--------|
| Windows NT 4.0 | ⚠️ Limited | ✅ Full | Supported |
| Windows 2000 | ✅ Full | ✅ Full | Supported |
| Windows XP | ✅ Full | ✅ Full | Supported |
| Windows Vista+ | ✅ Full | ✅ Full | Optimal |
| Windows 10/11 | ✅ Full | ✅ Full | Optimal |

## Limitations

1. **IPv6**: ICMPv6 support requires IPv6-enabled network
2. **Fragmentation**: Large payloads may be fragmented
3. **Filtering**: Some networks filter or rate-limit ICMP
4. **Privileges**: Raw sockets always require Administrator
5. **Async**: Current implementation is synchronous only

## Troubleshooting

### "Permission Denied" Error

**Cause**: Not running as Administrator

**Solution**:
```cmd
# Run as Administrator
runas /user:Administrator program.exe
```

### "Request Timed Out" Error

**Causes**:
- Host is down or unreachable
- Firewall blocking ICMP
- Network congestion
- Incorrect destination address

**Solutions**:
- Verify host is online
- Check firewall settings
- Increase timeout value
- Verify network connectivity

### Checksum Errors

**Cause**: Corrupted packets or implementation bug

**Solution**:
- Check network quality
- Verify packet construction
- Enable debug logging

## References

- [RFC 792](https://tools.ietf.org/html/rfc792) - ICMP Specification
- [RFC 1071](https://tools.ietf.org/html/rfc1071) - Internet Checksum
- [RFC 4443](https://tools.ietf.org/html/rfc4443) - ICMPv6 Specification
- [Windows IcmpSendEcho](https://docs.microsoft.com/en-us/windows/win32/api/icmpapi/nf-icmpapi-icmpsendecho)
- [Winsock Raw Sockets](https://docs.microsoft.com/en-us/windows/win32/winsock/tcp-ip-raw-sockets-2)

---

**Last Updated**: 2025
**Status**: Production Ready
**License**: Same as libnix
