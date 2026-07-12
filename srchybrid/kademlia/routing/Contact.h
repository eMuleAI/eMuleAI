/*
This file is part of eMule AI

Copyright (C)2003 Barry Dunne (https://www.emule-project.net)
Copyright (C)2026 eMule AI

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either
version 2 of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.


This work is based on the java implementation of the Kademlia protocol.
Kademlia: Peer-to-peer routing based on the XOR metric
Copyright (C) 2002  Petar Maymounkov [petar@post.harvard.edu]
http://kademlia.scs.cs.nyu.edu
*/

// Note To Mods //
/*
Please do not change anything here and release it.
There is going to be a new forum created just for the Kademlia side of the client.
If you feel there is an error or a way to improve something, please
post it in the forum first and let us look at it. If it is a real improvement,
it will be added to the official client. Changing something without knowing
what all it does, can cause great harm to the network if released in mass form.
Any mod that changes anything within the Kademlia side will not be allowed to advertise
their client on the eMule forum.
*/
#pragma once
#include "kademlia/utils/KadUDPKey.h"
#include "kademlia/utils/UInt128.h"
#include "eMuleAI/IPGeolocation.h"

namespace Kademlia
{
	class CContact
	{
		friend class CRoutingBin;
		friend class CRoutingZone;
		friend class CKadContactListCtrl;

	public:
		~CContact();
		CContact();
		CContact(const CUInt128 &uClientID, uint32 uIp, uint16 uUdpPort, uint16 uTcpPort, uint8 uVersion, const CKadUDPKey &cUDPKey, bool bIPVerified);
		CContact(const CUInt128 &uClientID, uint32 uIp, uint16 uUdpPort, uint16 uTcpPort, const CUInt128 &uTarget, uint8 uVersion, const CKadUDPKey &cUDPKey, bool bIPVerified);
		CContact(const CContact &k1)				{ Copy(k1); }
		CContact& operator=(const CContact &k1)		{ Copy(k1); return *this; }

		void	GetClientID(CUInt128 &uId) const		{ uId.SetValue(m_uClientID); }
		CUInt128 GetClientID() const					{ return m_uClientID; }
		void	GetClientID(CString &sId) const;
		void	SetClientID(const CUInt128 &uClientID);
		void	GetDistance(CUInt128 &uDistance) const	{ uDistance.SetValue(m_uDistance); }
		void	GetDistance(CString &sDistance) const;
		CUInt128 GetDistance() const					{ return m_uDistance; }
		uint32	GetIPAddress() const					{ return m_uIp; }
		uint32	GetNetIP() const						{ return m_uNetIp; }
		void	GetIPAddress(CString &sIp) const;
		void	SetIPAddress(uint32 uIp);
		uint16	GetTCPPort() const						{ return m_uTcpPort; }
		void	GetTCPPort(CString &sPort) const;
		void	SetTCPPort(uint16 uPort)				{ m_uTcpPort = uPort; }
		uint16	GetUDPPort() const						{ return m_uUdpPort; }
		void	GetUDPPort(CString &sPort) const;
		void	SetUDPPort(uint16 uPort)				{ m_uUdpPort = uPort; }
		byte	GetType() const							{ return m_byType; }
		void	UpdateType();
		void	CheckingType();
		bool	GetGuiRefs() const						{ return m_bGuiRefs; }
		void	SetGuiRefs(bool bRefs)					{ m_bGuiRefs = bRefs; }
		bool	InUse() const							{ return (m_uInUse > 0); }
		void	IncUse()								{ ++m_uInUse; }
		void	DecUse();
		uint8	GetVersion() const						{ return m_uVersion; }
		void	SetVersion(uint8 uVersion)				{ m_uVersion = uVersion; }
		time_t	GetCreatedTime() const					{ return m_tCreated; }
		time_t	GetExpireTime() const					{ return m_tExpires; }
		time_t	GetLastTypeSet() const					{ return m_tLastTypeSet; }
		time_t	GetLastSeen() const;
		void	Expire();

		bool	GetReceivedHelloPacket() const		{ return m_bReceivedHelloPacket; }
		void	SetReceivedHelloPacket()			{ m_bReceivedHelloPacket = true; }

		CKadUDPKey	GetUDPKey() const					{ return m_cUDPKey; }
		void	SetUDPKey(const CKadUDPKey &cUDPKey)	{ m_cUDPKey = cUDPKey; }
		bool	IsIpVerified() const					{ return m_bIPVerified; }
		void	SetIpVerified(bool bIPVerified)			{ m_bIPVerified = bIPVerified; }

		// bootstrapcontact functions only used for GUI
		bool	IsBootstrapContact() const			{ return m_bBootstrapContact; }
		void	SetBootstrapContact()				{ m_bBootstrapContact = true; }

		const CString GetGeolocationData() const;
		const int GetCountryFlagIndex() const;
		void	ResetIPGeolocation();
		GeolocationData_Struct m_structContactGeolocationData;
	private:
		void	InitContact(); // Common var initialization goes here
		void	Copy(const CContact &fromContact);

		CUInt128	m_uClientID;
		CUInt128	m_uDistance;
		CKadUDPKey	m_cUDPKey;
		time_t	m_tLastTypeSet;
		time_t	m_tExpires;
		time_t	m_tCreated;
		uint32	m_uInUse;
		uint32	m_uIp; //host byte order
		uint32	m_uNetIp; //network byte order
		uint16	m_uTcpPort;
		uint16	m_uUdpPort;
		uint8	m_uVersion;
		byte	m_byType;
		bool	m_bGuiRefs;
		bool	m_bIPVerified;
		bool	m_bReceivedHelloPacket;
		bool	m_bBootstrapContact;
	};
}