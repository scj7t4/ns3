/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * Copyright 2007 University of Washington
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation;
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include "ns3/log.h"
#include "ns3/ipv4.h"
#include "ns3/ipv4-address.h"
#include "ns3/ipv6-address.h"
#include "ns3/address-utils.h"
#include "ns3/nstime.h"
#include "ns3/inet-socket-address.h"
#include "ns3/inet6-socket-address.h"
#include "ns3/socket.h"
#include "ns3/udp-socket.h"
#include "ns3/simulator.h"
#include "ns3/socket-factory.h"
#include "ns3/packet.h"
#include "ns3/uinteger.h"
#include "ns3/ipv4-header.h"
#include "ns3/ethernet-header.h"

#include <boost/asio.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <boost/archive/iterators/base64_from_binary.hpp>
#include <boost/archive/iterators/binary_from_base64.hpp>
#include <boost/archive/iterators/transform_width.hpp>
#include <boost/foreach.hpp>

#include <sstream>
#include <stdexcept>

#include "ecn-announce.h"

namespace ns3 {

NS_LOG_COMPONENT_DEFINE ("EcnAnnounceApplication");

NS_OBJECT_ENSURE_REGISTERED (EcnAnnounce);

TypeId
EcnAnnounce::GetTypeId (void)
{
  static TypeId tid = TypeId ("ns3::EcnAnnounce")
    .SetParent<Application> ()
    .SetGroupName("Applications")
    .AddConstructor<EcnAnnounce> ()
    .AddAttribute ("Port", "Port on which we listen for incoming packets.",
                   UintegerValue (9),
                   MakeUintegerAccessor (&EcnAnnounce::m_port),
                   MakeUintegerChecker<uint16_t> ())
  ;
  return tid;
}

EcnAnnounce::EcnAnnounce ()
{
  NS_LOG_FUNCTION (this);
  m_started = false;
  m_spam_interval = 0.250;
  m_next_announcement = 0.0;
}

EcnAnnounce::~EcnAnnounce()
{
  NS_LOG_FUNCTION (this);
  m_socket = 0;
  m_socket6 = 0;
}

void
EcnAnnounce::DoDispose (void)
{
  NS_LOG_FUNCTION (this);
  Application::DoDispose ();
}

void
EcnAnnounce::CreateSockets(void)
{
  NS_LOG_FUNCTION (this);
  TypeId tid = TypeId::LookupByName ("ns3::UdpSocketFactory");
  m_socket = Socket::CreateSocket (GetNode (), tid);
  m_socket6 = Socket::CreateSocket (GetNode (), tid);
}

void
EcnAnnounce::StartApplication (void)
{
  NS_LOG_FUNCTION (this);
  m_started = true;
  InetSocketAddress local = InetSocketAddress (Ipv4Address::GetAny (), m_port);
  CreateSockets();
  m_socket->Bind (local);
  //m_socket->SetAttribute("IpMulticastTtl",UintegerValue(2));
  m_socket->SetAllowBroadcast(true);
  NS_LOG_INFO (Simulator::Now ().GetSeconds () << ": Node"<<GetNode()->GetId()<<" bound "<<
                   InetSocketAddress::ConvertFrom (local).GetIpv4 () << " port " <<
                   InetSocketAddress::ConvertFrom (local).GetPort ());
  if (addressUtils::IsMulticast (m_local))
  {
    Ptr<UdpSocket> udpSocket = DynamicCast<UdpSocket> (m_socket);
    if (udpSocket)
    {
      // equivalent to setsockopt (MCAST_JOIN_GROUP)
      udpSocket->MulticastJoinGroup (0, m_local);
    }
    else
    {
      NS_FATAL_ERROR ("Error: Failed to join multicast group");
    }
  }

  Inet6SocketAddress local6 = Inet6SocketAddress (Ipv6Address::GetAny (), m_port);
  m_socket6->Bind (local6);
  if (addressUtils::IsMulticast (local6))
  {
    Ptr<UdpSocket> udpSocket = DynamicCast<UdpSocket> (m_socket6);
    if (udpSocket)
    {
      // equivalent to setsockopt (MCAST_JOIN_GROUP)
      udpSocket->MulticastJoinGroup (0, local6);
    }
    else
    {
      NS_FATAL_ERROR ("Error: Failed to join multicast group");
    }
  }

  m_socket->SetRecvCallback (MakeCallback (&EcnAnnounce::HandleRead, this));
  m_socket6->SetRecvCallback (MakeCallback (&EcnAnnounce::HandleRead, this));
}

void
EcnAnnounce::StopApplication ()
{
  NS_LOG_FUNCTION (this);

  if (m_socket != 0)
    {
      m_socket->Close ();
      m_socket->SetRecvCallback (MakeNullCallback<void, Ptr<Socket> > ());
    }
  if (m_socket6 != 0)
    {
      m_socket6->Close ();
      m_socket6->SetRecvCallback (MakeNullCallback<void, Ptr<Socket> > ());
    }
}

void
EcnAnnounce::HandleRead (Ptr<Socket> socket)
{
  NS_LOG_FUNCTION (this << socket);
  Ptr<Packet> packet;
  Address from;
  while ((packet = socket->RecvFrom (from)))
  {
    boost::property_tree::ptree content;

    if (InetSocketAddress::IsMatchingType (from))
    {
      NS_LOG_INFO ("" << Simulator::Now ().GetSeconds () <<": Node"<<GetNode()->GetId()<<" received " <<
                   packet<<" of "<<packet->GetSize () << " bytes from " <<
                   InetSocketAddress::ConvertFrom (from).GetIpv4 () << " port " <<
                   InetSocketAddress::ConvertFrom (from).GetPort ());
                   
    }
    else if (Inet6SocketAddress::IsMatchingType (from))
    {
      NS_LOG_INFO ("" << Simulator::Now ().GetSeconds () <<": Node"<<GetNode()->GetId()<<" received " << packet->GetSize () << " bytes from " <<
                   Inet6SocketAddress::ConvertFrom (from).GetIpv6 () << " port " <<
                   Inet6SocketAddress::ConvertFrom (from).GetPort ());

    }

    boost::property_tree::ptree params;
    uint8_t* contents = new uint8_t[packet->GetSize()];
    packet->CopyData(contents, packet->GetSize());
    std::string packet_contents = std::string((char *)contents, packet->GetSize());
    std::cout<<packet_contents<<std::endl;
    delete [] contents;
    std::stringstream pstream(packet_contents);
    
    boost::property_tree::read_json(pstream, content);
    std::set<std::string> minions;
   
    try
    {
      std::string overlord = content.get<std::string>("leader");
      
      announce_destinations[overlord] = overlord;
      BOOST_FOREACH( boost::property_tree::ptree::value_type& peer, content.get_child("group"))
      {
          std::string minion = peer.second.get_value<std::string>();
          announce_destinations[minion] = overlord;
          NS_LOG_LOGIC("Added "<<minion<<" as member of "<<overlord);
          minions.insert(minion);
      }
      std::vector<std::string> clean;
      for(std::map<std::string, std::string>::iterator it=announce_destinations.begin(); it != announce_destinations.end(); it++)
      {
        if(it->second == overlord && minions.find(it->first) == minions.end() && it->second != it->first)
          clean.push_back(it->first);
      }
      for(std::vector<std::string>::iterator it=clean.begin(); it != clean.end(); it++)
      {
        announce_destinations.erase(*it);
        NS_LOG_LOGIC("Removed "<<*it<<" as member of "<<overlord);
      }
    }
    catch(...) { }
   
  }
}

bool EcnAnnounce::Announcement(Ptr<Packet> p, uint32_t mode)
{
  NS_LOG_FUNCTION (this);
  if(!m_started) return false;
  Ptr<Packet> dupe = p->Copy();
  EthernetHeader ehdr;
  Ipv4Header hdr;
  dupe->RemoveHeader(ehdr);
  dupe->RemoveHeader(hdr);
  Ipv4Address source = hdr.GetSource();
  boost::property_tree::ptree pt;
  pt.put("msg","ecn");
  pt.put("mode",mode==1?"hard":mode==2?"soft":"none");
  pt.put("origin",GetNode()->GetId());
  std::stringstream source_ip;
  source_ip<<source;
  std::stringstream json_buf;
  std::string json;
  boost::property_tree::write_json(json_buf, pt);
  json = json_buf.str();

  /// TODO: Support ipv6
  Ptr<Packet> packet = new Packet((uint8_t *) json.c_str(), json.size());
  uint32_t flags = 0;
  int result = 0;

  if(announce_destinations.find(source_ip.str()) == announce_destinations.end())
  {
    NS_LOG_LOGIC("Dropping message from "<<source_ip.str()<<" not participating");
    return false;
  }
  if(m_next_announcement <= Simulator::Now().GetSeconds())
  {
    m_next_announcement = Simulator::Now().GetSeconds() + m_spam_interval;
  }
  else
  {
    return true;
  }
  Address to = InetSocketAddress("224.1.1.1", 9);
  result = m_socket->SendTo(packet, flags, to);  
  if(result == -1)
  {
    std::cout<<"ECN ANNOUNCE FAILS"<<std::endl;
    exit(1);
  }
  if (InetSocketAddress::IsMatchingType (to))
  {
    NS_LOG_INFO ("" << Simulator::Now ().GetSeconds () << ": Node"<<GetNode()->GetId()<<" sent "<<packet<<
                 " of "<< packet->GetSize () << " bytes to " <<
                 InetSocketAddress::ConvertFrom (to).GetIpv4 () << " port " <<
                 InetSocketAddress::ConvertFrom (to).GetPort ());
  }
  else if (Inet6SocketAddress::IsMatchingType (to))
  {
    NS_LOG_INFO (""<<Simulator::Now ().GetSeconds ()<<": Node"<<GetNode()->GetId()<<" sent "<< packet->GetSize () << " bytes to " <<
                 Inet6SocketAddress::ConvertFrom (to).GetIpv6 () << " port " <<
                 Inet6SocketAddress::ConvertFrom (to).GetPort ());
  }
  return true;
}

} // Namespace ns3
