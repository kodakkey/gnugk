//////////////////////////////////////////////////////////////////
//
// gk_const.h	constants for gatekeeper ports etc.
//
// This work is published under the GNU Public License (GPL)
// see file COPYING for details.
// We also explicitely grant the right to link this code
// with the OpenH323 library.
//
//////////////////////////////////////////////////////////////////


#ifndef GK_CONST_H
#define GK_CONST_H "@(#) $Id$"

/* all values can be set in the config file, section [Gatekeeper::<InstanceName>]
 * these are just the defaults
 */

#define GK_DEF_UNICAST_RAS_PORT		1719
#define GK_DEF_MULTICAST_PORT		1718
#define GK_DEF_MULTICAST_GROUP		"224.0.1.41"

/* port used by gatekeeper for routed signaling: anything != 1720 so endpoint can be on same IP as GK */
#define GK_DEF_CALL_SIGNAL_PORT		1721

/* well known signal port */
#define GK_DEF_ENDPOINT_SIGNAL_PORT	1720

#define GK_DEF_STATUS_PORT		7000

#define GK_DEF_LISTEN_QUEUE_LENGTH	1024

extern const char *H225_ProtocolID;

#ifdef _WIN32
#ifndef _OpenH323_VERSION_H
#ifndef H323PLUS_LIB
#include <../../openh323/version.h> // get OpenH323 version
#else
#include <../../h323plus/version.h> // get h323plus version
#endif
#endif
#endif

///////////////////////////////////////
//OIDs
#define GnuGkOID "1.3.6.1.4.1.27938"

// Packetizer H460 Features
//+++++++++++++++++++++++++++++++++++++++++++++

///////////////////////////////////////////
// Presence
#define OID3 "1.3.6.1.4.1.17090.0.3"  // Presence

///////////////////////////////////////////
// NAT Type Detection

#define OID5 "1.3.6.1.4.1.17090.0.5"  // NAT Traversal support   
#define NATInstOID       7   // integer 8 Instruction on how NAT is to be Traversed

//////////////////////////////////////////
// Registration Priority and Pre-Emption

#define OID6  "1.3.6.1.4.1.17090.0.6"  // Registration priority & pre-emption
#define priorityOID  1   // integer 8 Priority number highest priority gets registration
#define preemptOID   2   // bool to instruct GK to preempt previous registration
#define priNotOID    3   // bool to notify EP registration RRJ (priority) UCF (higher Priority)
#define preNotOID    4   // bool to notify EP registration RRJ (can preempt) UCF (was preempted)

//////////////////////////////////////////
// P2Pnat Media

#define OID7 "1.3.6.1.4.1.17090.0.7"  // NAT Type detection support
#define localNATOID      1    // bool if endpoint is NATed
#define STUNServOID      2    // H225_TransportAddress of STUN Server 
#define NATTypeOID       3    // integer 8 Endpoint NAT Type
#define NATProxyOID      4    // Proxy for NAT support
#define NATMustProxyOID  5    // Media must proxy
#define remoteNATOID     6    // bool if endpoint has remote NAT support
#define NATAddressOID    7    // String of the remote party detected source address 

#define GK_DEF_STUN_PORT		3478

///////////////////////////////////////////
// Remote Vendor Information

#define OID9 "1.3.6.1.4.1.17090.0.9"  // Remote Vendor Information
#define VendorProdOID      1    // PASN_String of productID
#define VendorVerOID       2    // PASN_String of versionID
//+++++++++++++++++++++++++++++++++++++++++++++

#endif
