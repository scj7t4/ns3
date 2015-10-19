/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation;
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.    See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA    02111-1307    USA
 */

#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/bridge-module.h"
#include "ns3/applications-module.h"
#include "ns3/csma-module.h"
#include "ns3/ipv4-l3-protocol.h"
#include "ns3/gnuplot-helper.h"
#include "ns3/olsr-helper.h"
#include "ns3/ipv4-nix-vector-helper.h"

#include <fstream>

#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/json_parser.hpp>

using namespace ns3;

NS_LOG_COMPONENT_DEFINE ("FirstScriptExample");

static void CsmaRx (std::string c, Ptr<const Packet> p)
{
    std::cout<<"RAAAAAAAAWR "<<c<<std::endl;
    std::cout << *p << std::endl;
}

static void Ipv4L3Drop(std::string c, const Ipv4Header &h, Ptr<const Packet> p, Ipv4L3Protocol::DropReason r, Ptr<Ipv4> ipv4, uint32_t d)
{
    std::cout<<"RAAAAAAAAWR "<<c<<std::endl;
    std::cout << *p << std::endl;
}

void PopulateArpCache ()
{
    Ptr<ArpCache> arp = CreateObject<ArpCache> ();
    arp->SetAliveTimeout (Seconds(3600 * 24 * 365));
    for (NodeList::Iterator i = NodeList::Begin(); i != NodeList::End(); ++i)
    {
        Ptr<Ipv4L3Protocol> ip = (*i)->GetObject<Ipv4L3Protocol> ();
        if(ip == 0)
        {
            std::cerr<<"Skipped arp generation for Node "<<(*i)->GetId()<<std::endl;
            continue;
        }
        for(uint32_t j = 0; j != ip->GetNInterfaces(); j++)
        {
            Ptr<Ipv4Interface> ipIface = ip->GetInterface(j);
            NS_ASSERT(ipIface != 0);
            Ptr<NetDevice> device = ipIface->GetDevice();
            NS_ASSERT(device != 0);
            Mac48Address addr = Mac48Address::ConvertFrom(device->GetAddress ());
            for(uint32_t k = 0; k < ipIface->GetNAddresses (); k ++)
            {
                Ipv4Address ipAddr = ipIface->GetAddress (k).GetLocal();
                if(ipAddr == Ipv4Address::GetLoopback())
                    continue;
                ArpCache::Entry * entry = arp->Add(ipAddr);
                entry->MarkWaitReply(0);
                entry->MarkAlive(addr);
            }
        }
    }
    for (NodeList::Iterator i = NodeList::Begin(); i != NodeList::End(); ++i)
    {
        Ptr<Ipv4L3Protocol> ip = (*i)->GetObject<Ipv4L3Protocol> ();
        if(ip == 0)
        {
            std::cerr<<"Skipped arp distribution for Node "<<(*i)->GetId()<<std::endl;
            continue;
        }
        for(uint32_t j = 0; j != ip->GetNInterfaces(); j++)
        {
            Ptr<Ipv4Interface> ipIface = ip->GetInterface(j);
            ipIface->SetAttribute("ArpCache", PointerValue(arp));
        }
    }
}

boost::property_tree::ptree ids_in_container(const NodeContainer& cont)
{
    boost::property_tree::ptree arr;
    for(uint32_t i=0; i < cont.GetN(); i++)
    {
        boost::property_tree::ptree tmp;
        int id=cont.Get(i)->GetId();
        tmp.put("", id);
        arr.push_back(std::make_pair("",tmp));
    }
    return arr;
}

NetDeviceContainer DevicesAtNodeExcept(Ptr<Node> x, uint32_t skip)
{
    NetDeviceContainer result;
    for(uint32_t i=0; i < x->GetNDevices(); i++)
    {
        if(i == skip) continue;
        result.Add(x->GetDevice(i));
    }
    return result;
}

NodeContainer ShuffleContainer(NodeContainer inp)
{
    Ptr<UniformRandomVariable> rnd = CreateObject<UniformRandomVariable>();
    Ptr<Node> * nodes = new Ptr<Node>[inp.GetN()];
    for(uint32_t i=0; i < inp.GetN(); i++)
    {
        nodes[i] = inp.Get(i);
    }
    for(uint32_t i=inp.GetN()-1; i > 0; i--)
    {
        uint32_t j = rnd->GetInteger() % (i+1);
        Ptr<Node> tmp = nodes[i];
        nodes[i] = nodes[j];
        nodes[j] = tmp;
    }
    NodeContainer output;
    for(uint32_t i=0; i < inp.GetN(); i++)
    {
        output.Add(nodes[i]);
    }
    return output;
}

int
main (int argc, char *argv[])
{
    boost::property_tree::ptree sim_info;
    boost::property_tree::ptree scratch;
    Time::SetResolution (Time::NS);
    //LogComponentEnable ("RedQueueEcn", LOG_LEVEL_LOGIC);
    LogComponentEnable ("EcnAnnounceApplication", LOG_LEVEL_LOGIC);
    //LogComponentEnable("OnOffApplication", LOG_LEVEL_LOGIC);
    //LogComponentEnable ("UdpPyApplication", LOG_LEVEL_LOGIC);
    //LogComponentEnable ("UdpSocketImpl", LOG_LEVEL_LOGIC);

    const int NUM_NODES_N = 15;
    const int NUM_NODES_M = 15;

    const std::string ns3LB = "100Mbps";
    StringValue LINK_BANDWIDTH = StringValue(ns3LB);
    const uint64_t ns3LD = 500000;
    TimeValue LINK_DELAY = TimeValue(NanoSeconds(ns3LD));

    const bool ENABLE_TRAFFIC = true;
    const bool ENABLE_TROLLS = true;
    const bool USE_RED = true;

    const float GM_ST = 0.5;
    const float GM_ET = 300.0;
    const float TROLL_ST = 0.5;
    const float TROLL_ET = 300.0;
    const float TRAFFIC_ST = 80;
    const float TRAFFIC_ET = 300.0;

    const float RED_MIN_TH = 90;
    const float RED_MAX_TH = 200;
    const uint32_t RED_QUEUE_LIMIT = 500;
    const bool RED_GENTLE = true;
    const bool RED_WAIT = true;
    const float RED_QW = 0.002;

    const std::string JERK1_ON_TIME = "ns3::ConstantRandomVariable[Constant=5.0]";
    const std::string JERK1_OFF_TIME = "ns3::ConstantRandomVariable[Constant=0.0]";
    const std::string JERK1_DATA_RATE = "2.5Mbps";
    const bool JERK1_UDP = true;

    const std::string JERK2_ON_TIME = "ns3::ConstantRandomVariable[Constant=5.0]";
    const std::string JERK2_OFF_TIME = "ns3::ConstantRandomVariable[Constant=0.0]";
    const std::string JERK2_DATA_RATE = "2.5Mbps";
    const bool JERK2_UDP = true;

    sim_info.put("nodes_n", NUM_NODES_N);
    sim_info.put("nodes_m", NUM_NODES_M);
    sim_info.put("link_bandwidth", ns3LB);
    sim_info.put("link_delay", ns3LD);
    sim_info.put("enable_traffic", ENABLE_TRAFFIC);
    sim_info.put("enable_trolls", ENABLE_TROLLS);
    sim_info.put("gm_start_time", GM_ST);
    sim_info.put("gm_end_time", GM_ET);
    sim_info.put("troll_start_time", TROLL_ST);
    sim_info.put("troll_end_time", TROLL_ET);
    sim_info.put("traffic_start_time", TRAFFIC_ST);
    sim_info.put("traffic_end_time", TRAFFIC_ET);
    sim_info.put("red_min_th", RED_MIN_TH);
    sim_info.put("red_max_th", RED_MAX_TH);
    sim_info.put("red_queue_limit", RED_QUEUE_LIMIT);
    sim_info.put("red_gentle", RED_GENTLE);
    sim_info.put("red_wait", RED_WAIT);
    sim_info.put("red_qw", RED_QW);
    sim_info.put("use_red", USE_RED);
    sim_info.put("traffic1_on_time", JERK1_ON_TIME);
    sim_info.put("traffic1_off_time", JERK1_OFF_TIME);
    sim_info.put("traffic1_data_rate", JERK1_DATA_RATE);
    sim_info.put("traffic1_udp", JERK1_UDP);
    sim_info.put("traffic2_on_time", JERK2_ON_TIME);
    sim_info.put("traffic2_off_time", JERK2_OFF_TIME);
    sim_info.put("traffic2_data_rate", JERK2_DATA_RATE);
    sim_info.put("traffic2_udp", JERK2_UDP);

    NS_LOG_INFO ("Set RED params");
    Config::SetDefault ("ns3::RedQueueEcn::Mode", StringValue ("QUEUE_MODE_PACKETS"));
    Config::SetDefault ("ns3::RedQueueEcn::MeanPktSize", UintegerValue (100));
    Config::SetDefault ("ns3::RedQueueEcn::Wait", BooleanValue (RED_WAIT));
    Config::SetDefault ("ns3::RedQueueEcn::Gentle", BooleanValue (RED_GENTLE));
    Config::SetDefault ("ns3::RedQueueEcn::QW", DoubleValue (RED_QW));
    Config::SetDefault ("ns3::RedQueueEcn::MinTh", DoubleValue (RED_MIN_TH));
    Config::SetDefault ("ns3::RedQueueEcn::MaxTh", DoubleValue (RED_MAX_TH));
    Config::SetDefault ("ns3::RedQueueEcn::QueueLimit", UintegerValue (RED_QUEUE_LIMIT));
    Config::SetDefault ("ns3::RedQueueEcn::LinkBandwidth", LINK_BANDWIDTH);
    Config::SetDefault ("ns3::RedQueueEcn::LinkDelay", LINK_DELAY);

    NodeContainer allnodes;
    allnodes.Create(NUM_NODES_N+NUM_NODES_M);
    allnodes = ShuffleContainer(allnodes);

    NodeContainer nodes_n, nodes_m;
    for(uint32_t i=0; i < NUM_NODES_N; i++)
    {
        nodes_n.Add(allnodes.Get(i));

    }
    for(uint32_t i=NUM_NODES_N; i < NUM_NODES_N+NUM_NODES_M; i++)
    {
        nodes_m.Add(allnodes.Get(i));
    }
    sim_info.add_child("n_nodes", ids_in_container(nodes_n));
    sim_info.add_child("m_nodes", ids_in_container(nodes_m));

    Ptr<Node> router = CreateObject<Node>();
    sim_info.put("router", router->GetId());

    Ptr<Node> bridge1 = CreateObject<Node> ();
    Ptr<Node> bridge2 = CreateObject<Node> ();
    NodeContainer bridges(bridge1, bridge2);
    sim_info.put("n_bridge", bridge1->GetId());
    sim_info.put("m_bridge", bridge2->GetId());

    Ptr<Node> troll1 = CreateObject<Node> ();
    Ptr<Node> troll2 = CreateObject<Node> ();
    Ptr<Node> troll3 = CreateObject<Node> ();
    NodeContainer trolls(troll1, troll2, troll3);
    sim_info.put("n_troll", troll1->GetId());
    sim_info.put("m_troll", troll2->GetId());
    sim_info.put("router_troll", troll3->GetId());

    Ptr<Node> jerk1 = CreateObject<Node> ();
    Ptr<Node> jerk2 = CreateObject<Node> ();
    NodeContainer jerks(jerk1, jerk2);

    std::ofstream *tf1 = new std::ofstream("router_top_queue.dat");
    std::ofstream *tf2 = new std::ofstream("bridge_top_queue.dat");
    std::ofstream *bf1 = new std::ofstream("router_bottom_queue.dat");
    std::ofstream *bf2 = new std::ofstream("bridge_bottom_queue.dat");

    sim_info.put("n_jerk", jerk1->GetId());
    sim_info.put("m_jerk", jerk2->GetId());

    NS_LOG_INFO ("Build Topology");
    CsmaHelper csma;

    //csma.SetQueue("ns3::RedQueueEcn");
    csma.SetChannelAttribute ("DataRate", LINK_BANDWIDTH);
    csma.SetChannelAttribute ("Delay", LINK_DELAY);

    // Create the csma links, from each terminal to the bridge
    // This will create six network devices; we'll keep track separately
    // of the devices on and off the bridge respectively, for later configuration
    NetDeviceContainer topLanDevices;
    NetDeviceContainer topBridgeDevices;

    NodeContainer topLan(router,troll1);
    topLan.Add(nodes_n);
    topLan.Add(NodeContainer(jerk1));

    NetDeviceContainer link;
    for (uint32_t i = 0; i < topLan.GetN(); i++)
    {

        // install a csma channel between the ith toplan node and the bridge node
        link = csma.Install (NodeContainer(topLan.Get (i), bridge1));
        topLanDevices.Add (link.Get(0));
        topBridgeDevices.Add (link.Get(1));
        Ptr<CsmaNetDevice> rdev = link.Get(0)->GetObject<CsmaNetDevice>();
        Ptr<CsmaNetDevice> bdev = link.Get(1)->GetObject<CsmaNetDevice>();
        if(i == 0 && USE_RED)
        {
          Ptr<RedQueueEcn> q1 = CreateObject<RedQueueEcn>();
          Ptr<RedQueueEcn> q2 = CreateObject<RedQueueEcn>();
          q1->SetQueueLog(tf1);
          q2->SetQueueLog(tf2);
          // Link is connection between the bridge and the router
          rdev->SetQueue(q1);
          bdev->SetQueue(q2);
        }
    }


    // Now, Create the bridge netdevice, which will do the packet switching.    The
    // bridge lives on the node bridge1 and bridges together the topBridgeDevices
    // which are the three CSMA net devices on the node in the diagram above.
    //
    BridgeHelper bridge;
    bridge.Install (bridge1, topBridgeDevices);

    // Repeat for bottom bridged LAN
    NetDeviceContainer bottomLanDevices;
    NetDeviceContainer bottomBridgeDevices;
    NodeContainer bottomLan(router,troll2);
    bottomLan.Add(nodes_m);
    bottomLan.Add(NodeContainer(jerk2));

    if(!USE_RED)
    {
        std::cout<<"RED is not used on bridges or router"<<std::endl;
    }

    for (uint32_t i = 0; i < bottomLan.GetN(); i++)
    {
        // install a csma channel between the ith toplan node and the bridge node
        link = csma.Install (NodeContainer(bottomLan.Get (i), bridge2));
        bottomLanDevices.Add (link.Get(0));
        bottomBridgeDevices.Add (link.Get(1));
        Ptr<CsmaNetDevice> rdev = link.Get(0)->GetObject<CsmaNetDevice>();
        Ptr<CsmaNetDevice> bdev = link.Get(1)->GetObject<CsmaNetDevice>();
        if(i == 0)
        {
            // Link is connection between the bridge and the router
            if(USE_RED)
            {
              Ptr<RedQueueEcn> q1 = CreateObject<RedQueueEcn>();
              Ptr<RedQueueEcn> q2 = CreateObject<RedQueueEcn>();
              q1->SetQueueLog(bf1);
              q2->SetQueueLog(bf2);
              // Link is connection between the bridge and the router
              rdev->SetQueue(q1);
              bdev->SetQueue(q2);
            }
        }
    }
    bridge.Install (bridge2, bottomBridgeDevices);

    NetDeviceContainer t3 = csma.Install(NodeContainer(router,troll3));

    // Add internet stack to the router nodes
    NodeContainer routerNodes(router);
    routerNodes.Add(nodes_n);
    routerNodes.Add(nodes_m);
    routerNodes.Add(troll1);
    routerNodes.Add(troll2);
    routerNodes.Add(troll3);
    routerNodes.Add(jerk1);
    routerNodes.Add(jerk2);
    InternetStackHelper internet;
    //Ipv4NixVectorHelper nix;
    internet.Install(routerNodes);

    // We've got the "hardware" in place.    Now we need to add IP addresses.
    NS_LOG_INFO ("Assign IP Addresses.");
    Ipv4AddressHelper ipv4;
    ipv4.SetBase ("10.1.1.0", "255.255.255.0");
    ipv4.Assign (topLanDevices);
    ipv4.SetBase ("10.1.2.0", "255.255.255.0");
    ipv4.Assign (bottomLanDevices);
    ipv4.SetBase ("10.1.3.0", "255.255.255.0");
    ipv4.Assign (t3);


    //
    //
    // Create router nodes, initialize routing database and set up the routing
    // tables in the nodes.    We excuse the bridge nodes from having to serve as
    // routers, since they don't even have internet stacks on them.
    //
    /*
    std::cout<<"Making routes"<<std::endl;
    for(uint32_t i=0; i < bridge1->GetNDevices(); i++)
    {
        std::cout<<"Start "<<i<<std::endl;
        NetDeviceContainer bridgeifs = DevicesAtNodeExcept(bridge1,i);
        std::cout<<"bridgeifs "<<i<<std::endl;
        Ptr<NetDevice> devi = bridge1->GetDevice(i);
        std::cout<<"device "<<i<<" x "<<devi->GetIfIndex()<<std::endl;
        multicast.AddMulticastRoute(bridge1, Ipv4Address("0.0.0.0"), Ipv4Address("0.0.0.0"), devi, bridgeifs);
        std::cout<<"routed "<<i<<std::endl;
    }
    for(uint32_t i=0; i < bridge2->GetNDevices(); i++)
    {
        NetDeviceContainer bridgeifs = DevicesAtNodeExcept(bridge2,i);
        Ptr<NetDevice> devi = bridge2->GetDevice(i);
        multicast.AddMulticastRoute(bridge2, Ipv4Address("0.0.0.0"), Ipv4Address("0.0.0.0"), devi, bridgeifs);
    }
    multicast.AddMulticastRoute(router, Ipv4Address("0.0.0.0"), Ipv4Address("0.0.0.0"),
        router->GetDevice(0), NetDeviceContainer(router->GetDevice(1)));
    multicast.AddMulticastRoute(router, Ipv4Address("0.0.0.0"), Ipv4Address("0.0.0.0"),
        router->GetDevice(1), NetDeviceContainer(router->GetDevice(0)));
    */

    std::cout<<"Adding defaults"<<std::endl;
    Ipv4StaticRoutingHelper multicast;
    Ipv4GlobalRoutingHelper::PopulateRoutingTables ();


    for(uint32_t i=0; i < nodes_n.GetN(); i++)
    {
        multicast.SetDefaultMulticastRoute(nodes_n.Get(i), nodes_n.Get(i)->GetDevice(0));
        nodes_n.Get(i)->GetObject<Ipv4>()->GetRoutingProtocol()->PrintRoutingTable(Create<OutputStreamWrapper>(&std::cout));
    }
    for(uint32_t i=0; i < nodes_m.GetN(); i++)
    {
        multicast.SetDefaultMulticastRoute(nodes_m.Get(i), nodes_m.Get(i)->GetDevice(0));
    }
    multicast.SetDefaultMulticastRoute(troll1, troll1->GetDevice(0));
    multicast.SetDefaultMulticastRoute(troll2, troll2->GetDevice(0));
    multicast.SetDefaultMulticastRoute(troll3, troll3->GetDevice(0));
    //internet.SetRoutingHelper(multicast);


    //Ipv4ListRoutingHelper list;
    //list.Add(multicast,0);
    //list.Add(global,-10);
    //internet.SetRoutingHelper(list);

    ApplicationContainer gmApps;

    for (uint32_t i = 0; i < NUM_NODES_N; ++i)
    {
        Ptr<UdpPy> pyserv = CreateObject<UdpPy> ();
        Ptr<Node> node = nodes_n.Get(i);
        node->AddApplication(pyserv);
        gmApps.Add (pyserv);
        pyserv->CreateSockets();
    }

    for (uint32_t i = 0; i < NUM_NODES_M; ++i)
    {
        Ptr<UdpPy> pyserv = CreateObject<UdpPy> ();
        Ptr<Node> node = nodes_m.Get(i);
        node->AddApplication(pyserv);
        gmApps.Add (pyserv);
        pyserv->CreateSockets();
    }

    ApplicationContainer jerkApps;

    if(ENABLE_TRAFFIC)
    {
        Ptr<OnOffApplication> source1 = CreateObject<OnOffApplication>();
        Ptr<OnOffApplication> source2 = CreateObject<OnOffApplication>();
        Ptr<PacketSink> sink1 = CreateObject<PacketSink>();
        Ptr<PacketSink> sink2 = CreateObject<PacketSink>();
        jerk1->AddApplication(source1);
        jerk1->AddApplication(sink1);
        jerk2->AddApplication(source2);
        jerk2->AddApplication(sink2);
        jerkApps.Add(source1);
        jerkApps.Add(source2);
        jerkApps.Add(sink1);
        jerkApps.Add(sink2);

        InetSocketAddress d1 = InetSocketAddress(
                    jerk2->GetObject<Ipv4>()->GetAddress(1,0).GetLocal(), 22);
        InetSocketAddress d2 = InetSocketAddress(
                    jerk1->GetObject<Ipv4>()->GetAddress(1,0).GetLocal(), 22);

        std::cout<<d1.GetIpv4()<<":"<<d1.GetPort()<<std::endl;
        std::cout<<d2.GetIpv4()<<":"<<d2.GetPort()<<std::endl;

        source1->SetAttribute("Remote", AddressValue(d1));
        source2->SetAttribute("Remote", AddressValue(d2));
        if(!JERK1_UDP)
            source1->SetAttribute("Protocol", TypeIdValue (TcpSocketFactory::GetTypeId ()));
        if(!JERK2_UDP)
            source2->SetAttribute("Protocol", TypeIdValue (TcpSocketFactory::GetTypeId ()));
        source1->SetAttribute("OnTime",
            StringValue(JERK1_ON_TIME));
        source2->SetAttribute("OnTime",
            StringValue(JERK2_ON_TIME));
        source1->SetAttribute("OffTime",
            StringValue(JERK1_OFF_TIME));
        source2->SetAttribute("OffTime",
            StringValue(JERK2_OFF_TIME));
        source1->SetAttribute("DataRate", DataRateValue(DataRate(JERK1_DATA_RATE)));
        source2->SetAttribute("DataRate", DataRateValue(DataRate(JERK2_DATA_RATE)));
    }
    else
    {
        std::cout<<"EXTRA TRAFFIC IS DISABLED"<<std::endl;
    }

    ApplicationContainer announcers;
    if(ENABLE_TROLLS)
    {
        uint32_t connections = 0;
        NodeContainer infra(bridges);
        infra.Add(router);
        for (uint32_t i =0; i < trolls.GetN(); i++)
        {
            Ptr<EcnAnnounce> announcer = CreateObject<EcnAnnounce>();
            Ptr<Node> troll = trolls.Get(i);
            Ptr<Node> bridge = infra.Get(i);
            troll->AddApplication(announcer);
            announcers.Add(announcer);
            for(uint32_t j = 0; j < bridge->GetNDevices(); j++)
            {
                Ptr<CsmaNetDevice> dev = bridge->GetDevice(j)->GetObject<CsmaNetDevice>();
                if(dev)
                {
                    Ptr<RedQueueEcn> queue = dev->GetQueue()->GetObject<RedQueueEcn>();
                    if(queue)
                    {
                        queue->SetAnnounceCallback(MakeCallback(&EcnAnnounce::Announcement, announcer));
                        connections++;
                    }
                }
            }
        }
        std::cout<<"Enable callbacks...Done! (Made "<<announcers.GetN()<<" for "<<connections
                 <<" queues)"<<std::endl;
    }
    else
    {
        std::cout<<"ECN Notifications are DISABLED"<<std::endl;
        if(USE_RED)
            std::cout<<"RED will drop EXTRA TRAFFIC based on MAX_TH"<<std::endl;
    }



    jerkApps.Start(Seconds(TRAFFIC_ST));
    jerkApps.Stop(Seconds(TRAFFIC_ET));
    announcers.Start(Seconds(TROLL_ST));
    announcers.Stop(Seconds(TROLL_ET));
    gmApps.Start(Seconds(GM_ST));
    gmApps.Stop(Seconds(GM_ET));


    //
    // Also configure some tcpdump traces; each interface will be traced.
    // The output files will be named:
    //         csma-bridge-one-hop-<nodeId>-<interfaceId>.pcap
    // and can be read by the "tcpdump -r" command (use "-tt" option to
    // display timestamps correctly)
    //
    //csma.EnablePcapAll ("ns3", false);

    AsciiTraceHelper ascii;
    csma.EnableAsciiAll (ascii.CreateFileStream ("csma.tr"));

    
    //Config::Connect("/NodeList/*/DeviceList/*/$ns3::CsmaNetDevice/MacTxDrop",
    //    MakeCallback (&CsmaRx));
    //Config::Connect("/NodeList/*/DeviceList/*/$ns3::CsmaNetDevice/PhyTxDrop",
    //    MakeCallback (&CsmaRx));
    //Config::Connect("/NodeList/*/DeviceList/*/$ns3::CsmaNetDevice/PhyRxDrop",
    //    MakeCallback (&CsmaRx));
    //Config::Connect("/NodeList/*/$ns3::UdpL4Protocol/SocketList/*/$ns3::UdpSocketImpl/Drop",
    //    MakeCallback (&CsmaRx));
    //Config::Connect("/NodeList/*/$ns3::Ipv4L3Protocol/Drop",
    //    MakeCallback (&Ipv4L3Drop));
    //Config::Connect("/NodeList/*/$ns3::Ipv4L3Protocol/InterfaceList/*/ArpCache/Drop",
    //    MakeCallback (&CsmaRx));
    //Config::Connect("/NodeList/*/$ns3::ArpL3Protocol/Drop",
    //    MakeCallback (&CsmaRx));
    //Config::Connect("/NodeList/*/DeviceList/*/$ns3::CsmaNetDevice/TxQueue/Drop",
    //   MakeCallback (&CsmaRx));
    
    //Config::ConnectWithoutContext("$ns3::PacketSocket/FAKSE", MakeCallback(&PacketDrop));

    PopulateArpCache();

    Simulator::Run ();
    Simulator::Destroy ();
    std::ofstream fp_siminfo("simulationinfo.json", std::ofstream::out | std::ofstream::trunc);
    boost::property_tree::write_json(fp_siminfo, sim_info);
    fp_siminfo.close();
    tf1->close();
    tf2->close();
    bf1->close();
    bf2->close();
    delete tf1;
    delete tf2;
    delete bf1;
    delete bf2;
    return 0;
}
