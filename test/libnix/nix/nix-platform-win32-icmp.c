/*
 * nix-platform-win32-icmp.c
 *
 * ICMP (Internet Control Message Protocol) support for Windows
 * Enables ping, traceroute, and other ICMP-based network diagnostics
 *
 * Requires: Windows 2000+ and Administrator privileges for raw sockets
 */

#include "nix-platform.h"

#if defined(NIX_HOST_WIN32)

#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <icmpapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "iphlpapi.lib")

/*
 * ========================================================================
 * ICMP PACKET STRUCTURES
 * ========================================================================
 */

/* ICMP Header (RFC 792) */
typedef struct icmp_header {
	uint8_t  type;       /* ICMP message type */
	uint8_t  code;       /* ICMP message code */
	uint16_t checksum;   /* Checksum of ICMP header + data */
	uint16_t id;         /* Identifier for echo request/reply */
	uint16_t sequence;   /* Sequence number */
} icmp_header_t;

/* ICMP Echo Request/Reply data */
typedef struct icmp_echo {
	icmp_header_t header;
	uint8_t data[1];  /* Variable length data */
} icmp_echo_t;

/* IPv4 Header (simplified) */
typedef struct ip_header {
	uint8_t  ver_ihl;        /* Version (4 bits) + IHL (4 bits) */
	uint8_t  tos;            /* Type of Service */
	uint16_t total_len;      /* Total packet length */
	uint16_t id;             /* Identification */
	uint16_t flags_offset;   /* Flags (3 bits) + Fragment Offset (13 bits) */
	uint8_t  ttl;            /* Time to Live */
	uint8_t  protocol;       /* Protocol (IPPROTO_ICMP = 1) */
	uint16_t checksum;       /* Header checksum */
	uint32_t src_addr;       /* Source IP address */
	uint32_t dst_addr;       /* Destination IP address */
} ip_header_t;

/*
 * ========================================================================
 * ICMP MESSAGE TYPES AND CODES (RFC 792, RFC 4443)
 * ========================================================================
 */

/* ICMPv4 Types */
#define ICMP_ECHO_REPLY            0
#define ICMP_DEST_UNREACH          3
#define ICMP_SOURCE_QUENCH         4
#define ICMP_REDIRECT              5
#define ICMP_ECHO_REQUEST          8
#define ICMP_ROUTER_ADVERT         9
#define ICMP_ROUTER_SOLICIT        10
#define ICMP_TIME_EXCEEDED         11
#define ICMP_PARAM_PROBLEM         12
#define ICMP_TIMESTAMP_REQUEST     13
#define ICMP_TIMESTAMP_REPLY       14
#define ICMP_INFO_REQUEST          15
#define ICMP_INFO_REPLY            16
#define ICMP_ADDRESS_REQUEST       17
#define ICMP_ADDRESS_REPLY         18

/* ICMP Destination Unreachable Codes */
#define ICMP_NET_UNREACH           0
#define ICMP_HOST_UNREACH          1
#define ICMP_PROT_UNREACH          2
#define ICMP_PORT_UNREACH          3
#define ICMP_FRAG_NEEDED           4
#define ICMP_SR_FAILED             5

/* ICMP Time Exceeded Codes */
#define ICMP_EXC_TTL               0
#define ICMP_EXC_FRAGTIME          1

/*
 * ========================================================================
 * ICMP CHECKSUM CALCULATION
 * ========================================================================
 */

/* Calculate RFC 1071 Internet Checksum */
static uint16_t
icmp_checksum(const void *data, size_t length)
{
	const uint16_t *buf = (const uint16_t *)data;
	uint32_t sum = 0;
	size_t words = length / 2;

	/* Sum all 16-bit words */
	for (size_t i = 0; i < words; i++) {
		sum += buf[i];
	}

	/* Add remaining byte if odd length */
	if (length & 1) {
		sum += ((const uint8_t *)data)[length - 1];
	}

	/* Fold 32-bit sum to 16 bits */
	while (sum >> 16) {
		sum = (sum & 0xFFFF) + (sum >> 16);
	}

	/* Return one's complement */
	return (uint16_t)~sum;
}

/*
 * ========================================================================
 * ICMP SOCKET CREATION
 * ========================================================================
 */

/* Create ICMP raw socket */
int
nix_platform_win32_icmp_socket(int family)
{
	/* family: AF_INET for ICMPv4, AF_INET6 for ICMPv6 */
	int protocol = (family == AF_INET) ? IPPROTO_ICMP : IPPROTO_ICMPV6;

	/* Create raw socket */
	SOCKET sock = socket(family, SOCK_RAW, protocol);
	if (sock == INVALID_SOCKET) {
		int error = WSAGetLastError();
		switch (error) {
		case WSAEACCES:
			/* Raw sockets require administrator privileges on Windows */
			nix_platform_set_errno(EPERM);
			break;
		case WSAEAFNOSUPPORT:
			nix_platform_set_errno(EAFNOSUPPORT);
			break;
		case WSAEPROTONOSUPPORT:
			nix_platform_set_errno(EPROTONOSUPPORT);
			break;
		default:
			nix_platform_set_errno(EINVAL);
		}
		return -1;
	}

	/* Set socket options for ICMP */

	/* Set TTL (Time to Live) */
	int ttl = 64;  /* Default TTL */
	if (family == AF_INET) {
		setsockopt(sock, IPPROTO_IP, IP_TTL, (const char *)&ttl, sizeof(ttl));
	} else {
		setsockopt(sock, IPPROTO_IPV6, IPV6_UNICAST_HOPS, (const char *)&ttl, sizeof(ttl));
	}

	/* Enable reception of IP header (for raw sockets) */
	if (family == AF_INET) {
		DWORD flag = 1;
		setsockopt(sock, IPPROTO_IP, IP_HDRINCL, (const char *)&flag, sizeof(flag));
	}

	/* Set receive timeout (5 seconds) */
	DWORD timeout = 5000;
	setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char *)&timeout, sizeof(timeout));

	return (int)sock;
}

/*
 * ========================================================================
 * ICMP ECHO REQUEST/REPLY (PING)
 * ========================================================================
 */

/* Send ICMP Echo Request */
ssize_t
nix_platform_win32_icmp_echo_request(int sockfd, const struct sockaddr *dest_addr,
									 socklen_t addrlen, uint16_t id, uint16_t seq,
									 const void *data, size_t data_len)
{
	if (dest_addr == NULL || addrlen < sizeof(struct sockaddr_in)) {
		nix_platform_set_errno(EINVAL);
		return -1;
	}

	/* Allocate packet buffer */
	size_t packet_len = sizeof(icmp_header_t) + data_len;
	uint8_t *packet = (uint8_t *)malloc(packet_len);
	if (packet == NULL) {
		nix_platform_set_errno(ENOMEM);
		return -1;
	}

	/* Fill ICMP header */
	icmp_header_t *icmp = (icmp_header_t *)packet;
	icmp->type = ICMP_ECHO_REQUEST;
	icmp->code = 0;
	icmp->checksum = 0;  /* Calculate later */
	icmp->id = htons(id);
	icmp->sequence = htons(seq);

	/* Copy data payload */
	if (data != NULL && data_len > 0) {
		memcpy(packet + sizeof(icmp_header_t), data, data_len);
	}

	/* Calculate checksum */
	icmp->checksum = icmp_checksum(packet, packet_len);

	/* Send packet */
	ssize_t sent = sendto((SOCKET)sockfd, (const char *)packet, (int)packet_len, 0,
						  dest_addr, (int)addrlen);

	free(packet);

	if (sent == SOCKET_ERROR) {
		int error = WSAGetLastError();
		switch (error) {
		case WSAEHOSTUNREACH:
			nix_platform_set_errno(EHOSTUNREACH);
			break;
		case WSAENETUNREACH:
			nix_platform_set_errno(ENETUNREACH);
			break;
		case WSAEACCES:
			nix_platform_set_errno(EACCES);
			break;
		case WSAEMSGSIZE:
			nix_platform_set_errno(EMSGSIZE);
			break;
		default:
			nix_platform_set_errno(EIO);
		}
		return -1;
	}

	return sent;
}

/* Receive ICMP Echo Reply */
ssize_t
nix_platform_win32_icmp_echo_reply(int sockfd, struct sockaddr *src_addr,
								   socklen_t *addrlen, uint16_t *id, uint16_t *seq,
								   void *data, size_t data_len, uint8_t *ttl)
{
	/* Receive buffer (IP header + ICMP packet) */
	uint8_t buffer[1500];  /* MTU size */
	struct sockaddr_in from;
	int from_len = sizeof(from);

	/* Receive packet */
	ssize_t received = recvfrom((SOCKET)sockfd, (char *)buffer, sizeof(buffer), 0,
								(struct sockaddr *)&from, &from_len);

	if (received == SOCKET_ERROR) {
		int error = WSAGetLastError();
		switch (error) {
		case WSAETIMEDOUT:
			nix_platform_set_errno(ETIMEDOUT);
			break;
		case WSAECONNRESET:
			nix_platform_set_errno(ECONNRESET);
			break;
		default:
			nix_platform_set_errno(EIO);
		}
		return -1;
	}

	if (received < (ssize_t)sizeof(ip_header_t) + (ssize_t)sizeof(icmp_header_t)) {
		nix_platform_set_errno(EINVAL);
		return -1;
	}

	/* Parse IP header */
	ip_header_t *ip = (ip_header_t *)buffer;
	size_t ip_hdr_len = (ip->ver_ihl & 0x0F) * 4;  /* IHL is in 32-bit words */

	if (received < (ssize_t)ip_hdr_len + (ssize_t)sizeof(icmp_header_t)) {
		nix_platform_set_errno(EINVAL);
		return -1;
	}

	/* Extract TTL */
	if (ttl != NULL) {
		*ttl = ip->ttl;
	}

	/* Parse ICMP header */
	icmp_header_t *icmp = (icmp_header_t *)(buffer + ip_hdr_len);

	/* Verify checksum */
	size_t icmp_len = received - ip_hdr_len;
	uint16_t recv_checksum = icmp->checksum;
	icmp->checksum = 0;
	uint16_t calc_checksum = icmp_checksum(icmp, icmp_len);

	if (recv_checksum != calc_checksum) {
		nix_platform_set_errno(EBADMSG);
		return -1;
	}

	/* Check if this is an Echo Reply */
	if (icmp->type != ICMP_ECHO_REPLY) {
		/* Handle other ICMP messages */
		if (icmp->type == ICMP_DEST_UNREACH) {
			nix_platform_set_errno(EHOSTUNREACH);
		} else if (icmp->type == ICMP_TIME_EXCEEDED) {
			nix_platform_set_errno(ETIMEDOUT);
		} else {
			nix_platform_set_errno(EPROTO);
		}
		return -1;
	}

	/* Extract ID and sequence number */
	if (id != NULL) {
		*id = ntohs(icmp->id);
	}
	if (seq != NULL) {
		*seq = ntohs(icmp->sequence);
	}

	/* Copy source address */
	if (src_addr != NULL && addrlen != NULL) {
		socklen_t copy_len = (*addrlen < from_len) ? *addrlen : from_len;
		memcpy(src_addr, &from, copy_len);
		*addrlen = from_len;
	}

	/* Copy data payload */
	size_t payload_len = icmp_len - sizeof(icmp_header_t);
	if (data != NULL && data_len > 0 && payload_len > 0) {
		size_t copy_len = (data_len < payload_len) ? data_len : payload_len;
		memcpy(data, (uint8_t *)icmp + sizeof(icmp_header_t), copy_len);
	}

	return (ssize_t)payload_len;
}

/*
 * ========================================================================
 * ICMP USING WINDOWS ICMP.DLL (ALTERNATIVE HIGH-LEVEL API)
 * ========================================================================
 */

/* Ping using IcmpSendEcho (simpler, no raw sockets needed) */
int
nix_platform_win32_icmp_ping(const char *host, uint32_t timeout_ms,
							 uint32_t *rtt_ms, uint8_t *ttl)
{
	/* Resolve hostname to IP address */
	struct addrinfo hints = {0};
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_RAW;
	hints.ai_protocol = IPPROTO_ICMP;

	struct addrinfo *result = NULL;
	if (getaddrinfo(host, NULL, &hints, &result) != 0) {
		nix_platform_set_errno(EHOSTUNREACH);
		return -1;
	}

	if (result == NULL || result->ai_addr == NULL) {
		nix_platform_set_errno(EHOSTUNREACH);
		return -1;
	}

	struct sockaddr_in *addr = (struct sockaddr_in *)result->ai_addr;
	uint32_t dest_ip = addr->sin_addr.s_addr;

	freeaddrinfo(result);

	/* Open ICMP handle */
	HANDLE hIcmp = IcmpCreateFile();
	if (hIcmp == INVALID_HANDLE_VALUE) {
		nix_platform_set_errno(EINVAL);
		return -1;
	}

	/* Prepare echo request */
	char send_data[32] = "PING";
	uint32_t reply_size = sizeof(ICMP_ECHO_REPLY) + sizeof(send_data);
	void *reply_buffer = malloc(reply_size);
	if (reply_buffer == NULL) {
		IcmpCloseHandle(hIcmp);
		nix_platform_set_errno(ENOMEM);
		return -1;
	}

	/* Send echo request */
	DWORD result_count = IcmpSendEcho(
		hIcmp,
		dest_ip,
		send_data,
		sizeof(send_data),
		NULL,  /* IP options */
		reply_buffer,
		reply_size,
		timeout_ms
	);

	if (result_count == 0) {
		DWORD error = GetLastError();
		free(reply_buffer);
		IcmpCloseHandle(hIcmp);

		switch (error) {
		case IP_REQ_TIMED_OUT:
			nix_platform_set_errno(ETIMEDOUT);
			break;
		case IP_DEST_HOST_UNREACHABLE:
		case IP_DEST_NET_UNREACHABLE:
			nix_platform_set_errno(EHOSTUNREACH);
			break;
		case IP_DEST_PROT_UNREACHABLE:
			nix_platform_set_errno(EPROTONOSUPPORT);
			break;
		case IP_TTL_EXPIRED_TRANSIT:
			nix_platform_set_errno(ETIMEDOUT);
			break;
		default:
			nix_platform_set_errno(EIO);
		}
		return -1;
	}

	/* Parse reply */
	ICMP_ECHO_REPLY *reply = (ICMP_ECHO_REPLY *)reply_buffer;

	if (rtt_ms != NULL) {
		*rtt_ms = reply->RoundTripTime;
	}

	if (ttl != NULL) {
		*ttl = reply->Options.Ttl;
	}

	int status = 0;
	if (reply->Status != IP_SUCCESS) {
		switch (reply->Status) {
		case IP_DEST_HOST_UNREACHABLE:
		case IP_DEST_NET_UNREACHABLE:
			nix_platform_set_errno(EHOSTUNREACH);
			status = -1;
			break;
		case IP_TTL_EXPIRED_TRANSIT:
			nix_platform_set_errno(ETIMEDOUT);
			status = -1;
			break;
		default:
			nix_platform_set_errno(EIO);
			status = -1;
		}
	}

	free(reply_buffer);
	IcmpCloseHandle(hIcmp);

	return status;
}

/*
 * ========================================================================
 * ICMP SOCKET OPTIONS
 * ========================================================================
 */

/* Set ICMP socket TTL */
int
nix_platform_win32_icmp_set_ttl(int sockfd, int ttl)
{
	if (ttl < 1 || ttl > 255) {
		nix_platform_set_errno(EINVAL);
		return -1;
	}

	/* Determine socket family */
	struct sockaddr_storage ss;
	int ss_len = sizeof(ss);
	if (getsockname((SOCKET)sockfd, (struct sockaddr *)&ss, &ss_len) == SOCKET_ERROR) {
		nix_platform_set_errno(EINVAL);
		return -1;
	}

	int level = (ss.ss_family == AF_INET) ? IPPROTO_IP : IPPROTO_IPV6;
	int option = (ss.ss_family == AF_INET) ? IP_TTL : IPV6_UNICAST_HOPS;

	if (setsockopt((SOCKET)sockfd, level, option, (const char *)&ttl, sizeof(ttl)) == SOCKET_ERROR) {
		nix_platform_set_errno(EINVAL);
		return -1;
	}

	return 0;
}

/* Get ICMP socket TTL */
int
nix_platform_win32_icmp_get_ttl(int sockfd)
{
	/* Determine socket family */
	struct sockaddr_storage ss;
	int ss_len = sizeof(ss);
	if (getsockname((SOCKET)sockfd, (struct sockaddr *)&ss, &ss_len) == SOCKET_ERROR) {
		nix_platform_set_errno(EINVAL);
		return -1;
	}

	int level = (ss.ss_family == AF_INET) ? IPPROTO_IP : IPPROTO_IPV6;
	int option = (ss.ss_family == AF_INET) ? IP_TTL : IPV6_UNICAST_HOPS;

	int ttl;
	int optlen = sizeof(ttl);

	if (getsockopt((SOCKET)sockfd, level, option, (char *)&ttl, &optlen) == SOCKET_ERROR) {
		nix_platform_set_errno(EINVAL);
		return -1;
	}

	return ttl;
}

/* Set ICMP socket timeout */
int
nix_platform_win32_icmp_set_timeout(int sockfd, uint32_t timeout_ms)
{
	DWORD timeout = timeout_ms;
	if (setsockopt((SOCKET)sockfd, SOL_SOCKET, SO_RCVTIMEO, (const char *)&timeout, sizeof(timeout)) == SOCKET_ERROR) {
		nix_platform_set_errno(EINVAL);
		return -1;
	}
	return 0;
}

/*
 * ========================================================================
 * ICMP UTILITIES
 * ========================================================================
 */

/* Get ICMP type name */
const char *
nix_platform_win32_icmp_type_name(uint8_t type)
{
	switch (type) {
	case ICMP_ECHO_REPLY:        return "Echo Reply";
	case ICMP_DEST_UNREACH:      return "Destination Unreachable";
	case ICMP_SOURCE_QUENCH:     return "Source Quench";
	case ICMP_REDIRECT:          return "Redirect";
	case ICMP_ECHO_REQUEST:      return "Echo Request";
	case ICMP_ROUTER_ADVERT:     return "Router Advertisement";
	case ICMP_ROUTER_SOLICIT:    return "Router Solicitation";
	case ICMP_TIME_EXCEEDED:     return "Time Exceeded";
	case ICMP_PARAM_PROBLEM:     return "Parameter Problem";
	case ICMP_TIMESTAMP_REQUEST: return "Timestamp Request";
	case ICMP_TIMESTAMP_REPLY:   return "Timestamp Reply";
	case ICMP_INFO_REQUEST:      return "Information Request";
	case ICMP_INFO_REPLY:        return "Information Reply";
	case ICMP_ADDRESS_REQUEST:   return "Address Mask Request";
	case ICMP_ADDRESS_REPLY:     return "Address Mask Reply";
	default:                     return "Unknown";
	}
}

/* Check if running with administrator privileges */
int
nix_platform_win32_icmp_check_admin(void)
{
	BOOL is_admin = FALSE;
	PSID admin_group = NULL;
	SID_IDENTIFIER_AUTHORITY nt_authority = SECURITY_NT_AUTHORITY;

	/* Create SID for BUILTIN\Administrators */
	if (AllocateAndInitializeSid(&nt_authority, 2,
								 SECURITY_BUILTIN_DOMAIN_RID,
								 DOMAIN_ALIAS_RID_ADMINS,
								 0, 0, 0, 0, 0, 0,
								 &admin_group)) {
		/* Check if current process token is member of Administrators group */
		CheckTokenMembership(NULL, admin_group, &is_admin);
		FreeSid(admin_group);
	}

	return is_admin ? 1 : 0;
}

#endif /* NIX_HOST_WIN32 */
