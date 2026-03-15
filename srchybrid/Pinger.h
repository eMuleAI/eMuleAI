//This file is part of eMule AI
//Copyright (C)2002-2026 Merkur ( strEmail.Format("%s@%s", "devteam", "emule-project.net") / https://www.emule-project.net )
//Copyright (C)2026 eMule AI
//
//This program is free software; you can redistribute it and/or
//modify it under the terms of the GNU General Public License
//as published by the Free Software Foundation; either
//version 2 of the License, or (at your option) any later version.
//
//This program is distributed in the hope that it will be useful,
//but WITHOUT ANY WARRANTY; without even the implied warranty of
//MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//GNU General Public License for more details.
//
//You should have received a copy of the GNU General Public License
//along with this program; if not, write to the Free Software
//Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

/*---------------------------------------------------------------------
*
*  Some code in this file has been copied from a ping demo that was
*  created by Bob Quinn, 1997          http://www.sockets.com
*
* As some general documentation about how the ping is implemented,
* here is the description Bob Quinn wrote about the ping demo.
*
* Description:
*  Prototypes and typedefs Microsoft's ICMP.DLL functions & structs
*  for access to Internet Control Message Protocol.  This is capable
*  of doing "ping or "traceroute", although beware that Microsoft
*  discourages the use of these APIs.
*
* Some Background:
*
* The standard Berkeley Sockets SOCK_RAW socket type, is normally used
* to create ping (echo request/reply), and sometimes traceroute applications
* (the original traceroute application from Van Jacobson used UDP, rather
* than ICMP). Microsoft's WinSock version 2 implementations for NT4 and
* Windows 95 support raw sockets, but none of their WinSock version 1.1
* implementations (WFWG, NT3.x or standard Windows 95) did.
*
* Microsoft has their own API for an ICMP.DLL that their ping and tracert
* applications use (by the way, they are both non-GUI text-based console
* applications. This is a proprietary API, and all function calls that
* involve network functions operate in blocking mode. They still include
* it with WinSock 2 implementations.
*
* There is little documentation available (I first found it in the Win32
* SDK in \MSTOOLS\ICMP, and it exists on the MS Developers' Network
* CD-ROM now, also). Microsoft disclaims this API about as strongly as
* possible.  The README.TXT that accompanies it says:
*
* [DISCLAIMER]
*
* We have had requests in the past to expose the functions exported from
* icmp.dll. The files in this directory are provided for your convenience
* in building applications which make use of ICMPSendEcho(). Notice that
* the functions in icmp.dll are not considered part of the Win32 API and
* will not be supported in future releases. Once we have a more complete
* solution in the operating system, this DLL, and the functions it exports,
* will be dropped.
*
* [DOCUMENTATION]
*
* The ICMPSendEcho() function sends an ICMP echo request to the specified
* destination IP address and returns any replies received within the timeout
* specified. The API is synchronous, requiring the process to spawn a thread
* before calling the API to avoid blocking. An open IcmpHandle is required
* for the request to complete. IcmpCreateFile() and IcmpCloseHandle()
* functions are used to create and destroy the context handle.</P>
*/
#pragma once

#define DEFAULT_TTL 64

/* Note 1: The Reply Buffer will have an array of ICMP_ECHO_REPLY
* structures, followed by options and the data in ICMP echo reply
* datagram received. You must have room for at least one ICMP
* echo reply structure, plus 8 bytes for an ICMP header.
*/

/* Note 2: For the most part, you can refer to RFC 791 for details
* on how to fill in values for the IP option information structure.
*/

struct PingStatus
{
	float fDelay;
	uint32 destinationAddress;
	DWORD status;
	DWORD error;
	UCHAR ttl;
	bool bSuccess;
};

// UDPing - required constants and structures -->

// ICMP packet types
#define ICMP_T_ECHO_REPLY 0
#define ICMP_T_DEST_UNREACH 3
#define ICMP_T_ECHO_REQUEST 8
#define ICMP_T_TTL_EXPIRE 11

// Minimum ICMP packet size, in bytes
#define ICMP_MIN 8

// The following two structures need to be packed tightly, but unlike
// Borland C++, Microsoft C++ does not do this by default.
#ifdef _MSC_VER
#pragma pack(push, 1)
#endif

// The IP header
struct IPHeader
{
	BYTE h_len:4;		// Length of the header in dwords
	BYTE version:4;		// Version of IP
	BYTE tos;			// Type of service
	USHORT total_len;	// Length of the packet in dwords
	USHORT ident;		// unique identifier
	USHORT flags;		// Flags
	BYTE ttl;			// Time to live
	BYTE proto;			// Protocol number (TCP, UDP etc)
	USHORT checksum;	// IP checksum
	ULONG source_ip;
	ULONG dest_ip;
};

// ICMP header for DEST_UNREACH and TTL_EXPIRE replies
struct ICMPHeader
{
	BYTE type;			// ICMP packet type
	BYTE code;			// Type sub code
	USHORT checksum;
	BYTE unused[4];		// may be used for various data, we don't need it
	IPHeader hdrsent;	// original IP header
	union
	{
		BYTE data[8];	// data next to IP header (UDP header)
		struct
		{
			USHORT src_port;
			USHORT dest_port;
			USHORT length;
			USHORT checksum;
		} UDP;
	};
};

#ifdef _MSC_VER
#pragma pack(pop)
#endif

#define UDP_PORT 33434  // UDP/TCP traceroute port by iana.org - should not be filtered by routers/ISP

// UDPing - required constants and structures end <--


class Pinger
{
public:
	Pinger();
	~Pinger();
	Pinger(const Pinger&) = delete;
	Pinger& operator=(const Pinger&) = delete;

	PingStatus Ping(uint32 lAddr, uint32 ttl = DEFAULT_TTL, bool doLog = false, bool useUdp = false);

	static void PIcmpErr(LPCTSTR pszMsg, DWORD nICMPErr);

private:
	PingStatus PingICMP(uint32 lAddr, DWORD ttl, bool doLog);
	PingStatus PingUDP(uint32 lAddr, DWORD ttl, bool doLog);

	IP_OPTION_INFORMATION stIPInfo;
	HANDLE hICMP;

	SOCKET us;          // UDP socket to send requests
	SOCKET is;          // raw ICMP socket to catch responses

	bool udpStarted;
};