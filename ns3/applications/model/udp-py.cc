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

#include <boost/asio.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <boost/archive/iterators/base64_from_binary.hpp>
#include <boost/archive/iterators/binary_from_base64.hpp>
#include <boost/archive/iterators/transform_width.hpp>

#include <sstream>
#include <stdexcept>

#include "udp-py.h"

namespace ns3 {

NS_LOG_COMPONENT_DEFINE ("UdpPyApplication");

NS_OBJECT_ENSURE_REGISTERED (UdpPy);

static unsigned int rpc_id_counter = 0;

const std::string base64_padding[] = {"", "==","="};
std::string base64_encode(const std::string& s) {
  namespace bai = boost::archive::iterators;

  std::stringstream os;

  // convert binary values to base64 characters
  typedef bai::base64_from_binary
  // retrieve 6 bit integers from a sequence of 8 bit bytes
  <bai::transform_width<const char *, 6, 8> > base64_enc; // compose all the above operations in to a new iterator

  std::copy(base64_enc(s.c_str()), base64_enc(s.c_str() + s.size()),
            std::ostream_iterator<char>(os));

  os << base64_padding[s.size() % 3];
  return os.str();
}

std::string base64_decode(const std::string& s) {
  namespace bai = boost::archive::iterators;

  std::stringstream os;

  typedef bai::transform_width<bai::binary_from_base64<const char *>, 8, 6> base64_dec;

  unsigned int size = s.size();

  // Remove the padding characters, cf. https://svn.boost.org/trac/boost/ticket/5629
  if (size && s[size - 1] == '=') {
    --size;
    if (size && s[size - 1] == '=') --size;
  }
  if (size == 0) return std::string();

  std::copy(base64_dec(s.data()), base64_dec(s.data() + size),
            std::ostream_iterator<char>(os));

  return os.str();
}

TypeId
UdpPy::GetTypeId (void)
{
  static TypeId tid = TypeId ("ns3::UdpPy")
    .SetParent<Application> ()
    .SetGroupName("Applications")
    .AddConstructor<UdpPy> ()
    .AddAttribute ("Port", "Port on which we listen for incoming packets.",
                   UintegerValue (9),
                   MakeUintegerAccessor (&UdpPy::m_port),
                   MakeUintegerChecker<uint16_t> ())
  ;
  return tid;
}

UdpPy::UdpPy ()
{
  NS_LOG_FUNCTION (this);

}

UdpPy::~UdpPy()
{
  NS_LOG_FUNCTION (this);
  m_socket = 0;
  m_socket6 = 0;
}

void
UdpPy::DoDispose (void)
{
  NS_LOG_FUNCTION (this);
  Application::DoDispose ();
}

void
UdpPy::CreateSockets(void)
{
  NS_LOG_FUNCTION (this);
  TypeId tid = TypeId::LookupByName ("ns3::UdpSocketFactory");
  m_socket = Socket::CreateSocket (GetNode (), tid);
  m_socket6 = Socket::CreateSocket (GetNode (), tid);
}

void
UdpPy::StartApplication (void)
{
  NS_LOG_FUNCTION (this);

  InetSocketAddress local = InetSocketAddress (Ipv4Address::GetAny (), m_port);
  m_socket->Bind (local);
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

  m_socket->SetRecvCallback (MakeCallback (&UdpPy::HandleRead, this));
  m_socket6->SetRecvCallback (MakeCallback (&UdpPy::HandleRead, this));

    namespace pt = boost::property_tree;
    pt::ptree event;
    pt::ptree child;
    event.put("method","start");

    Ptr<Ipv4> ipv4 = GetNode()->GetObject<Ipv4>();

    for(uint32_t i=0; i < ipv4->GetNInterfaces(); i++)
    {
        pt::ptree device;
        pt::ptree addresses;
        for(uint32_t j=0; j < ipv4->GetNAddresses(i); j++)
        {
            pt::ptree tmp;
            tmp.put("", ipv4->GetAddress(i,j));
            addresses.push_back(std::make_pair("",tmp));
        }
        device.put("mtu", ipv4->GetMtu(i));
        device.put("up", ipv4->IsUp(i));
        device.add_child("addresses", addresses);
        child.push_back(std::make_pair("ipv4",device));
    }
    pt::ptree cont;
    cont.add_child("ipv4",child);
    /// TODO: ipv6
    event.add_child("params", child);

    boost::property_tree::ptree response = MakeRPCRequest(event);

    HandleRPCResponse(response, m_socket);
}

void
UdpPy::StopApplication ()
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
UdpPy::HandleRead (Ptr<Socket> socket)
{
  NS_LOG_FUNCTION (this << socket);
  Ptr<Packet> packet;
  Address from;
  while ((packet = socket->RecvFrom (from)))
  {
    boost::property_tree::ptree outgoing;
    outgoing.put("method", "recv");

    boost::property_tree::ptree pinfo;
    pinfo.put("packet_size", packet->GetSize());

    if (InetSocketAddress::IsMatchingType (from))
    {
      NS_LOG_INFO ("" << Simulator::Now ().GetSeconds () <<": Node"<<GetNode()->GetId()<<" received " <<
                   packet<<" of "<<packet->GetSize () << " bytes from " <<
                   InetSocketAddress::ConvertFrom (from).GetIpv4 () << " port " <<
                   InetSocketAddress::ConvertFrom (from).GetPort ());
                   
      pinfo.put("sender_ipv4", InetSocketAddress::ConvertFrom (from).GetIpv4 ());
      pinfo.put("sender_port", InetSocketAddress::ConvertFrom (from).GetPort ());
    }
    else if (Inet6SocketAddress::IsMatchingType (from))
    {
      NS_LOG_INFO ("" << Simulator::Now ().GetSeconds () <<": Node"<<GetNode()->GetId()<<" received " << packet->GetSize () << " bytes from " <<
                   Inet6SocketAddress::ConvertFrom (from).GetIpv6 () << " port " <<
                   Inet6SocketAddress::ConvertFrom (from).GetPort ());
      pinfo.put("sender_ipv6", Inet6SocketAddress::ConvertFrom (from).GetIpv6 ());
      pinfo.put("sender_port", Inet6SocketAddress::ConvertFrom (from).GetPort ());
    }

    boost::property_tree::ptree params;
    uint8_t* contents = new uint8_t[packet->GetSize()];
    packet->CopyData(contents, packet->GetSize());
    std::string packet_contents = std::string((char *)contents, packet->GetSize());
    pinfo.put("packet", base64_encode(packet_contents));
    delete [] contents;

    outgoing.add_child("params", pinfo);

    boost::property_tree::ptree response = MakeRPCRequest(outgoing);

    //packet->RemoveAllPacketTags ();
    //packet->RemoveAllByteTags ();

    HandleRPCResponse(response, socket);
  }
}

boost::property_tree::ptree UdpPy::MakeRPCRequest(boost::property_tree::ptree rpc_call)
{
    using boost::asio::ip::tcp;
    using namespace std;
    namespace pt = boost::property_tree;
    boost::asio::io_service io_service;

    rpc_call.put("id", rpc_id_counter++);
    rpc_call.put("node_id", GetNode()->GetId());
    rpc_call.put("simulation_time", Simulator::Now().GetSeconds());

    std::string host = "127.0.0.1";
    std::string path = "/";
    std::string port = "5000";

    // Get a list of endpoints corresponding to the server name.
    tcp::resolver resolver(io_service);
    tcp::resolver::query query(host, port);
    tcp::resolver::iterator endpoint_iterator = resolver.resolve(query);
    //boost::asio::ip::tcp::endpoint ep(boost::asio::ip::tcp::v4(), 5000);


    // Try each endpoint until we successfully establish a connection.
    tcp::socket socket(io_service);
    boost::asio::connect(socket, endpoint_iterator);

    // Convert the ptree to json
    stringstream call_buf;
    string call;
    pt::write_json(call_buf, rpc_call);
    call = call_buf.str();

    // Form the request. We specify the "Connection: close" header so that the
    // server will close the socket after transmitting the response. This will
    // allow us to treat all data up until the EOF as the content.
    boost::asio::streambuf request;
    std::ostream request_stream(&request);
    request_stream << "PUT " << path << " HTTP/1.0\r\n";
    request_stream << "Host: " << host << "\r\n";
    request_stream << "Accept: */*\r\n";
    request_stream << "Connection: close\r\n";
    request_stream << "Content-type: application/json\r\n";
    request_stream << "Content-length: "<<call.size()<<"\r\n";
    request_stream << "\r\n";
    // Attach the json to the stream
    request_stream << call;

    // Send the request.
    boost::asio::write(socket, request);

    // Read the response status line. The response streambuf will automatically
    // grow to accommodate the entire line. The growth may be limited by passing
    // a maximum size to the streambuf constructor.
    boost::asio::streambuf response;
    boost::asio::read_until(socket, response, "\r\n");

    // Check that response is OK.
    std::istream response_stream(&response);
    std::string http_version;
    response_stream >> http_version;
    unsigned int status_code;
    response_stream >> status_code;
    std::string status_message;
    std::getline(response_stream, status_message);
    if (!response_stream || http_version.substr(0, 5) != "HTTP/")
    {
      std::cerr << "ERROR: invalid response from RPC server."<<std::endl;
      throw runtime_error("RPC -- Invalid response.");
    }
    if (status_code != 200)
    {
      std::cerr << "ERROR: Response returned with status code " << status_code << std::endl;
      throw runtime_error("RPC -- Invalid status code.");
    }

    // Read the response headers, which are terminated by a blank line.
    boost::asio::read_until(socket, response, "\r\n\r\n");

    // Process the response headers.
    std::string header;
    while (std::getline(response_stream, header) && header != "\r");

    stringstream ss;
    // Write whatever content we already have to output.
    if (response.size() > 0)
      ss << &response;

    // Read until EOF, writing data to output as we go.
    boost::system::error_code error;
    while (boost::asio::read(socket, response,
          boost::asio::transfer_at_least(1), error))
      ss << &response;
    if (error != boost::asio::error::eof)
      throw boost::system::system_error(error);

    // Now ss is full of yummy json (Hopefully!)
    pt::ptree rpc_response;
    pt::read_json(ss, rpc_response);
    //std::cout<<ss.str()<<std::endl;
    return rpc_response;
}

int UdpPy::HandleRPCResponse(boost::property_tree::ptree response, Ptr<Socket> socket)
{
  namespace pt = boost::property_tree;
  // The response is an id, an error message, and a list of commands
  int id = response.get<int>("id");
  std::string error = response.get<std::string>("error");
  if(error != "null")
  {
    std::cerr<<"ERROR: "<<error<<std::endl;
    throw std::runtime_error("RPC -- RPC response signaled error. Halt simulation.");
  }

  boost::property_tree::ptree commands = response.get_child("commands");
  for(pt::ptree::iterator it=commands.begin(); it != commands.end(); it++)
  {
    boost::property_tree::ptree command = it->second;
    std::string action = command.get<std::string>("action");
    if(action == "send")
    {
      /// TODO: Support ipv6
      std::string host = command.get<std::string>("destination_ipv4");
      uint16_t port = command.get<uint16_t>("destination_port");
      uint32_t flags = command.get<uint32_t>("flags",0);
      Ipv4Address ipv4addr(host.c_str());
      Address to = InetSocketAddress(ipv4addr,port);
      std::string contents = base64_decode(command.get<std::string>("packet"));
      Ptr<Packet> packet = new Packet((uint8_t *) contents.c_str(), contents.size());

      socket->SendTo(packet, flags, to);
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
    }
    else if(action == "schedule")
    {
        /// TODO: Don't assume ms
        std::string whenstr = command.get<std::string>("when");
        double when = 0;
        uint32_t eventid = command.get<uint32_t>("eventid");
        bool absolute = command.get<bool>("when_is_absolute",false);
        EventId event;

        if(whenstr == "destroyed")
        {
            event = Simulator::ScheduleDestroy(&UdpPy::DoEvent, this, eventid);
            NS_LOG_INFO (""<<Simulator::Now ().GetSeconds ()<<": Node"<<GetNode()->GetId()<<" scheduled event "<<eventid<<" for ON DESTROY");
        }
        else if(whenstr == "now")
        {
            event = Simulator::ScheduleNow(&UdpPy::DoEvent, this, eventid);
            NS_LOG_INFO (""<<Simulator::Now ().GetSeconds ()<<": Node"<<GetNode()->GetId()<<" scheduled event "<<eventid<<" for NOW");
        }
        else
        {
            when = command.get<double>("when");
            Time dt = MilliSeconds(when);
            std::string note = "FROM NOW";
            if(absolute)
            {
                dt -= Now();
                note = "(ABSOLUTE)";
            }
            NS_LOG_INFO (""<<Simulator::Now ().GetSeconds ()<<": Node"<<GetNode()->GetId()<<" scheduled event "<<eventid<<" for +"<<dt.GetMilliSeconds()<<"ms "<<note);
            event = Simulator::Schedule(dt, &UdpPy::DoEvent, this, eventid);
        }
        m_eventtable[eventid] = event;
    }
    else if(action == "cancel")
    {
        uint32_t eventid = command.get<uint32_t>("id");
        m_eventtable[eventid].Cancel();
        NS_LOG_INFO (""<<Simulator::Now ().GetSeconds ()<<": Node"<<GetNode()->GetId()<<" cancelled event "<<eventid);
    }
  }
}

void UdpPy::DoEvent(uint32_t eventid)
{
    NS_LOG_INFO (""<<Simulator::Now ().GetSeconds ()<<": Node"<<GetNode()->GetId()<<" fires event "<<eventid);
    namespace pt = boost::property_tree;
    pt::ptree event;
    pt::ptree child;
    event.put("method","event");
    child.put("eventid", eventid);
    event.add_child("params", child);

    boost::property_tree::ptree response = MakeRPCRequest(event);

    //packet->RemoveAllPacketTags ();
    //packet->RemoveAllByteTags ();

    HandleRPCResponse(response, m_socket);
}

} // Namespace ns3
