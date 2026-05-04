/*
 * Copyright (c) 2023
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
 *
 * Author: Zhaochen Zhang (zhaochen.zhang@outlook.com)
 */

#include "json-topology-helper.h"

#include "ns3/json-utils.h"

#include <fstream>
#include <iostream>
#include <map>
#include <time.h>

namespace ns3
{

NS_LOG_COMPONENT_DEFINE("JsonTopologyHelper");

namespace json_util
{

std::shared_ptr<DcbFcHelper>
ConstructFcHelper(const boost::json::object& fcConfig)
{
    std::shared_ptr<DcbFcHelper> fcHelper = std::make_shared<DcbFcHelper>();

    // Read and set the attributes of fcHelper
    JsonCallIfExistsString(fcConfig, "bufferSize", [fcHelper](std::string bufferSize) {
        fcHelper->SetBufferSize(QueueSize(bufferSize));
    });
    JsonCallIfExistsString(fcConfig, "bufferPerPort", [fcHelper](std::string bufferPerPort) {
        fcHelper->SetBufferPerPort(QueueSize(bufferPerPort));
    });
    JsonCallIfExistsInt<uint32_t>(
        fcConfig,
        "numQueuePerPort",
        [fcHelper](uint32_t numQueuePerPort) { fcHelper->SetNumQueuePerPort(numQueuePerPort); });
    JsonCallIfExistsFloat<double>(
        fcConfig,
        "bufferBandwidthRatio",
        [fcHelper](double bufferBandwidthRatio) { fcHelper->SetBufferBandwidthRatio(bufferBandwidthRatio); });
    JsonCallIfExistsInt<uint32_t>(
        fcConfig,
        "numLosslessQueue",
        [fcHelper](uint32_t numLosslessQueue) { fcHelper->SetNumLosslessQueue(numLosslessQueue); });

    // Read and set TypeId and Attributes of
    // TrafficControlLayer, flowControlPort, flowControlMmuQueue, outerQdisc, innerQdisc
    std::string tclType =
        JsonGetStringOrRaise(fcConfig, "trafficControlLayer", "trafficControlLayer is not found");
    std::unique_ptr<std::vector<ConfigEntry_t>> tclConfigVector = ConstructConfigVector(
        JsonGetObjectOrRaise(fcConfig,
                             "trafficControlLayerConfig",
                             "trafficControlLayertrafficControlLayerConfig is not found"));
    fcHelper->SetTrafficControlTypeId(tclType);
    fcHelper->SetTrafficControlAttributes(std::move(*tclConfigVector));

    std::string fcpType =
        JsonGetStringOrRaise(fcConfig, "flowControlPort", "flowControlPort is not found");
    std::unique_ptr<std::vector<ConfigEntry_t>> fcpConfigVector =
        ConstructConfigVector(JsonGetObjectOrRaise(fcConfig,
                                                   "flowControlPortConfig",
                                                   "flowControlPortConfig is not found"));
    fcHelper->SetFlowControlPortTypeId(fcpType);
    fcHelper->SetFlowControlPortAttributes(std::move(*fcpConfigVector));

    // flowControlMmuQueue is optional
    JsonCallIfExistsString(
        fcConfig,
        "flowControlMmuQueue",
        [fcConfig, fcHelper](std::string fcqType) {
            fcHelper->SetFlowControlMmuQueueTypeId(fcqType);
            std::unique_ptr<std::vector<ConfigEntry_t>> fcqConfigVector = ConstructConfigVector(
                JsonGetObjectOrRaise(fcConfig,
                                     "flowControlMmuQueueConfig",
                                     "flowControlMmuQueueConfig is not found"));
            fcHelper->SetFlowControlMmuQueueAttributes(std::move(*fcqConfigVector));
        });

    std::string oqdType =
        JsonGetStringOrRaise(fcConfig, "outerQueueDisc", "outerQueueDisc is not found");
    std::unique_ptr<std::vector<ConfigEntry_t>> oqdConfigVector =
        ConstructConfigVector(JsonGetObjectOrRaise(fcConfig,
                                                   "outerQueueDiscConfig",
                                                   "outerQueueDiscConfig is not found"));
    fcHelper->SetOuterQueueDiscTypeId(oqdType);
    fcHelper->SetOuterQueueDiscAttributes(std::move(*oqdConfigVector));

    std::string iqdType =
        JsonGetStringOrRaise(fcConfig, "innerQueueDisc", "innerQueueDisc is not found");
    std::unique_ptr<std::vector<ConfigEntry_t>> iqdConfigVector =
        ConstructConfigVector(JsonGetObjectOrRaise(fcConfig,
                                                   "innerQueueDiscConfig",
                                                   "innerQueueDiscConfig is not found"));
    fcHelper->SetInnerQueueDiscTypeId(iqdType);
    fcHelper->SetInnerQueueDiscAttributes(std::move(*iqdConfigVector));

    JsonCallIfExistsString(fcConfig, "priorityVec", [fcHelper](std::string priorityVec) {
        fcHelper->SetPriorities(ConvertRangeToVector(priorityVec.c_str(), fcHelper->GetNumQueuePerPort()));
    });
    JsonCallIfExistsString(fcConfig, "quantumVec", [fcHelper](std::string quantumVec) {
        fcHelper->SetQuantum(ConvertRangeToVector(quantumVec.c_str(), fcHelper->GetNumQueuePerPort()));
    });
    JsonCallIfExistsInt<uint32_t>(
        fcConfig,
        "maxCredit",
        [fcHelper](uint32_t maxCredit) { fcHelper->SetMaxCredit(maxCredit); });

    return fcHelper;
}

[[maybe_unused]] static void
InstallFlowControl(const boost::json::object& fcConfig, Ptr<DcTopology> topology)
{
    std::shared_ptr<DcbFcHelper> fcHelper = ConstructFcHelper(fcConfig);

    // Install the flow control protocols on the specified nodes
    std::string nodes = JsonGetStringOrRaise(fcConfig, "nodes", "nodes is not found");
    if (nodes == "host")
    {
        for (auto hostIter = topology->hosts_begin(); hostIter != topology->hosts_end(); hostIter++)
        {
            if (hostIter->type != DcTopology::TopoNode::NodeType::HOST)
            {
                NS_FATAL_ERROR("Node "
                               << topology->GetNodeIndex(hostIter->nodePtr)
                               << " is not a host and thus could not install an application.");
            }
            Ptr<Node> node = hostIter->nodePtr;
            fcHelper->Install(node);
        }
    }
    else if (nodes == "switch")
    {
        for (auto swIter = topology->switches_begin(); swIter != topology->switches_end(); swIter++)
        {
            if (swIter->type != DcTopology::TopoNode::NodeType::SWITCH)
            {
                NS_FATAL_ERROR("Node "
                               << topology->GetNodeIndex(swIter->nodePtr)
                               << " is not a switch and thus could not install an application.");
            }
            Ptr<Node> node = swIter->nodePtr;
            fcHelper->Install(node);

            // Switch node special config
            Ptr<TrafficControlLayer> tc = node->GetObject<TrafficControlLayer>();
            for (uint32_t i = 0; i < node->GetNDevices(); i++)
            {
                Ptr<SwitchNode> sw;
                if ((sw = DynamicCast<SwitchNode>(node)) == nullptr)
                {
                    NS_FATAL_ERROR("Node " << topology->GetNodeIndex(swIter->nodePtr)
                                           << " is not a switchNode.");
                }
                tc->RegisterProtocolHandler(MakeCallback(&SwitchNode::ReceivePacketAfterTc, sw),
                                            Ipv4L3Protocol::PROT_NUMBER,
                                            node->GetDevice(i));
            }
        }
    }
    else
    {
        // If the nodes are specified, convert the string to vector
        // Here we assume the nodes are specified in the format like "[1:3,5,7:]".
        std::vector<uint32_t> vNodes = ConvertRangeToVector(nodes.c_str(), topology->GetNNodes());

        for (uint32_t i = 0; i < vNodes.size(); ++i)
        {
            Ptr<Node> node = topology->GetNode(vNodes[i]).nodePtr;
            fcHelper->Install(node);
        }
    }
}

/***** Utilities about topology *****/
static void AssignAddress(const Ptr<Node> node, const Ptr<NetDevice> device);

static DcTopology::TopoNode
CreateOneHost()
{
    const Ptr<Node> host = CreateObject<Node>();

    DcbStackHelper hostStack;
    hostStack.InstallHostStack(host);

    return {.type = DcTopology::TopoNode::NodeType::HOST, .nodePtr = host};
}

static DcTopology::TopoNode
CreateOneSwitch()
{
    const Ptr<SwitchNode> sw = CreateObject<SwitchNode>();

    // Add protocol to switch
    DcbStackHelper hostStack;
    hostStack.InstallSwitchStack(sw);

    // Basic configurations
    // sw->SetEcmpSeed (m_ecmpSeed);
    return {.type = DcTopology::TopoNode::NodeType::SWITCH, .nodePtr = sw};
}

static Ptr<DcbNetDevice>
AddPortToNode(const Ptr<Node> node)
{
    // Create device for this port
    const Ptr<DcbNetDevice> dev = CreateObject<DcbNetDevice>();
    dev->SetAddress(Mac48Address::Allocate());

    // Create queue on the device
    ObjectFactory queueFactory;
    queueFactory.SetTypeId(DropTailQueue<Packet>::GetTypeId());
    Ptr<Queue<Packet>> queue = queueFactory.Create<Queue<Packet>>();
    // TODO: Why so high??
    // queue->SetMaxSize({QueueSizeUnit::PACKETS, std::numeric_limits<uint32_t>::max()});
    queue->SetMaxSize(QueueSize("10p"));
    dev->SetQueue(queue);

    node->AddDevice(dev);

    if (node->GetObject<TrafficControlLayer>())
        AssignAddress(node, dev);

    return dev;
}

struct LinkConfig
{
    DataRate rate;
    Time delay;
};

typedef std::map<std::string, LinkConfig> LinkConfigMap;

static std::unique_ptr<LinkConfigMap>
ConstructLinkConfig(const boost::json::array& linkConfigArray)
{
    std::unique_ptr<LinkConfigMap> linkConfigMap = std::make_unique<LinkConfigMap>();
    for (auto linkConfig : linkConfigArray)
    {
        std::string linkName =
            JsonGetStringOrRaise(linkConfig.as_object(), "links", "links is not found");
        std::string rate =
            JsonGetStringOrRaise(linkConfig.as_object(), "rate", "rate is not found");
        std::string delay =
            JsonGetStringOrRaise(linkConfig.as_object(), "delay", "delay is not found");
        linkConfigMap->insert({linkName, {DataRate(rate), Time(delay)}});
    }
    return linkConfigMap;
}

static void InstallLink(const LinkConfig& linkConfig,
                        std::optional<boost::json::object> linkFailureConfigObj,
                        Ptr<DcbNetDevice> dev1,
                        Ptr<DcbNetDevice> dev2)
{
    dev1->SetAttribute("DataRate", DataRateValue(linkConfig.rate));
    dev2->SetAttribute("DataRate", DataRateValue(linkConfig.rate));
    Ptr<Node> node1 = dev1->GetNode(), node2 = dev2->GetNode();
    // Simulator::ScheduleNow(MakeBoundCallback(f, node1));
    if (linkFailureConfigObj.has_value()){
        uint32_t srcNode = JsonGetInt64OrRaise((*linkFailureConfigObj), "SrcNode", "SrcNode is not found");
        uint32_t dstNode = JsonGetInt64OrRaise((*linkFailureConfigObj), "DstNode", "DstNode is not found");
        if ((node1->GetId() == srcNode && node2->GetId() == dstNode) || (node1->GetId() == dstNode && node2->GetId() == srcNode)){
            std::string startTimeStr = JsonGetStringOrRaise((*linkFailureConfigObj), "StartTime", "StartTime is not found");
            std::string lastingTimeStr = JsonGetStringOrRaise((*linkFailureConfigObj), "LastingTime", "LastingTime is not found");
            std::string failBandwidthStr = JsonGetStringOrRaise((*linkFailureConfigObj), "FailBandwidth", "FailBandwidth is not found");
            Time startTime = Time(startTimeStr);
            Time lastingTime = Time(lastingTimeStr);
            DataRate failBandwidth = DataRate(failBandwidthStr);
            Ptr<DcbNetDevice> srcDev = node1->GetId() == srcNode ? dev1 : dev2;
            std::cout<< "Link failure between node " << node1->GetId() << " and node " << node2->GetId() << " from " << startTime.GetNanoSeconds() << " to " << (startTime + lastingTime).GetNanoSeconds() << " with bandwidth " << failBandwidth << std::endl;
            Simulator::ScheduleWithContext(node1->GetId(), startTime, &DcbNetDevice::SetAttribute, srcDev, "DataRate", DataRateValue(failBandwidth));
            Simulator::ScheduleWithContext(node1->GetId(), startTime + lastingTime, &DcbNetDevice::SetAttribute, srcDev, "DataRate", DataRateValue(linkConfig.rate));
        }
    }
    Ptr<DcbChannel> channel = CreateObject<DcbChannel>();
    channel->SetAttribute("Delay", TimeValue(linkConfig.delay));

    dev1->Attach(channel);
    dev2->Attach(channel);
}

static void
AssignAddress(const Ptr<Node> node, const Ptr<NetDevice> device)
{
    Ptr<Ipv4> ipv4 = node->GetObject<Ipv4>();
    NS_ASSERT_MSG(ipv4,
                  "Ipv4AddressHelper::Assign(): NetDevice is associated"
                  " with a node without IPv4 stack installed -> fail "
                  "(maybe need to use DcbStackHelper?)");

    int32_t interface = ipv4->GetInterfaceForDevice(device);
    if (interface == -1)
    {
        interface = ipv4->AddInterface(device);
    }
    NS_ASSERT_MSG(interface >= 0,
                  "Ipv4AddressHelper::Assign(): "
                  "Interface index not found");

    Ipv4Address addr = Ipv4AddressGenerator::NextAddress("255.0.0.0");
    Ipv4InterfaceAddress ipv4Addr = Ipv4InterfaceAddress(addr, "255.0.0.0");
    ipv4->AddAddress(interface, ipv4Addr);
    ipv4->SetMetric(interface, 1);
    ipv4->SetUp(interface);
}

[[maybe_unused]] void
LogIpAddress(const Ptr<const DcTopology> topology)
{
    int ni = 0;
    for (const auto& node : *topology)
    {
        Ptr<Ipv4> ipv4 = node->GetObject<Ipv4>();
        const int nintf = ipv4->GetNInterfaces();
        for (int i = 0; i < nintf; i++)
        {
            const int naddr = ipv4->GetNAddresses(i);
            for (int j = 0; j < naddr; j++)
            {
                std::string name =
                    (node.type == DcTopology::TopoNode::NodeType::HOST) ? "host " : "switch ";
                NS_LOG_DEBUG(name << ni << " intf " << i << " addr " << j << " "
                                  << ipv4->GetAddress(i, j));
            }
        }
        ni++;
    }
}

[[maybe_unused]] static void
LogAllRoutes(const Ptr<const DcTopology> topology)
{
    int ni = 0;
    for (const auto& node : *topology)
    {
        Ptr<GlobalRouter> router = node->GetObject<GlobalRouter>();
        Ptr<Ipv4GlobalRouting> route = router->GetRoutingProtocol();
        std::string name =
            (node.type == DcTopology::TopoNode::NodeType::HOST) ? "host " : "switch ";
        if (!route)
        {
            NS_LOG_DEBUG(name << ni << " does not have global routing");
            ni++;
            continue;
        }
        const int n = route->GetNRoutes();
        for (int i = 0; i < n; i++)
        {
            Ipv4RoutingTableEntry entry = route->GetRoute(i);
            NS_LOG_DEBUG(name << ni << " " << entry);
        }
        ni++;
    }
}

[[maybe_unused]] static void
LogGlobalRouting(const Ptr<DcTopology> topology)
{
    for (DcTopology::SwitchIterator sw = topology->switches_begin(); sw != topology->switches_end();
         sw++)
    {
        Ptr<Ipv4ListRouting> lrouting =
            DynamicCast<Ipv4ListRouting>((*sw)->GetObject<Ipv4>()->GetRoutingProtocol());
        int16_t prio;
        Ptr<Ipv4GlobalRouting> glb =
            DynamicCast<Ipv4GlobalRouting>(lrouting->GetRoutingProtocol(0, prio));
        uint32_t n = glb->GetNRoutes();
        for (uint32_t i = 0; i < n; i++)
        {
            NS_LOG_DEBUG("global: " << *glb->GetRoute(i));
        }
        UintegerValue b;
        glb->GetAttribute("RandomEcmpRouting", b);
        NS_LOG_DEBUG("ecmp: " << b.Get());
    }
}

Ptr<DcTopology>
BuildTopology(boost::json::object& configObj)
{
    std::string topoFilename =
        configObj["inputFile"].get_object().find("topo")->value().as_string().c_str();
    std::ifstream topof;
    topof.open(topoFilename);
    Ipv4AddressGenerator::Init("10.0.0.0", "255.0.0.0", "0.0.0.1");

    // The first line
    uint32_t node_num, switch_num, link_num;
    topof >> node_num >> switch_num >> link_num;

    // Creat a topology to manage all nodes and links
    Ptr<DcTopology> topology = CreateObject<DcTopology>(node_num);

    // Read the idxes of switches and build the node type vector
    // In the node type vector, 0 means host, 1 means switch
    std::vector<uint32_t> node_type(node_num, 0);
    for (uint32_t i = 0; i < switch_num; i++)
    {
        uint32_t sid;
        topof >> sid;
        node_type[sid] = 1;
    }

    // Create nodes according to node type list
    NodeContainer n;
    // NS_LOG_INFO ("Create nodes.");
    for (uint32_t i = 0; i < node_num; i++)
    {
        if (node_type[i] == 0)
        {
            // Host
            DcTopology::TopoNode host = CreateOneHost();
            topology->InstallNode(i, std::move(host));
        }
        else
        {
            DcTopology::TopoNode host = CreateOneSwitch();
            topology->InstallNode(i, std::move(host));
        }
    }

    // Set the number of hosts in the GlobalValue
    // New a GlobalValue and set it, it will never be freeed
    new ns3::GlobalValue("NHosts",
                         "Number of hosts in the topology",
                         UintegerValue(topology->GetNHosts()),
                         MakeUintegerChecker<uint32_t>());

    Ipv4GlobalRoutingHelper globalRouting;
    // Ipv4ListRoutingHelper list;
    // list.Add (globalRouting, 1);

    // Explicitly create the channels required by the topology.
    // NS_LOG_INFO ("Create channels.");
    boost::json::object topoObj =
        JsonGetObjectOrRaise(configObj, "topologyConfig", "topologyConfig is not found");
    boost::json::array linkConfigObj =
        JsonGetArrayOrRaise(topoObj, "linkConfig", "linkConfig is not found");
    std::optional<boost::json::object> linkFailureConfigObj =
        topoObj.find("LinkFailureConfig") != topoObj.end()
            ? std::make_optional(topoObj["LinkFailureConfig"].as_object())
            : std::nullopt;
    std::unique_ptr<LinkConfigMap> linkConfigMap = ConstructLinkConfig(linkConfigObj);
    for (uint32_t i = 0; i < link_num; i++)
    {
        // type is used to build heterogeneous topology
        uint32_t src, dst;
        std::string type;
        topof >> src >> dst >> type;

        Ptr<Node> srcNode = topology->GetNode(src).nodePtr;
        Ptr<Node> dstNode = topology->GetNode(dst).nodePtr;
        topology->InstallLink(src, dst); // as metadata

        if (topology->GetNode(src).type != DcTopology::TopoNode::NodeType::SWITCH &&
            topology->GetNode(dst).type != DcTopology::TopoNode::NodeType::SWITCH)
        {
            NS_FATAL_ERROR("Do not allow link between two hosts");
        }

        // Add port to the nodes
        std::vector<Ptr<Node>> vNodes = {srcNode, dstNode};
        std::vector<Ptr<DcbNetDevice>> vDevs;
        for (auto node : vNodes)
        {
            if (topology->IsHost(node->GetId()))
            {
                Ptr<DcbNetDevice> dev = AddPortToNode(node);
                vDevs.push_back(dev);
            }
            else
            {
                Ptr<DcbNetDevice> dev = AddPortToNode(node);
                vDevs.push_back(dev);
            }
        }
        // Install link
        InstallLink(linkConfigMap->find(type)->second, linkFailureConfigObj, vDevs[0], vDevs[1]);
    }

    // Install flow control protocols after building the topology
    boost::json::array fcArray =
        JsonGetArrayOrRaise(topoObj, "flowControlConfig", "flowControlConfig is not found");
    for (auto fcConfig : fcArray)
    {
        InstallFlowControl(fcConfig.as_object(), topology);
    }

    // Calculate routing tables
    Ipv4GlobalRoutingHelper::PopulateRoutingTables();

    // p2p.EnablePcapAll ("pcap");
    LogIpAddress(topology);
    LogAllRoutes(topology);

    topof.close();

    // Calculate the propagation delay between each pair of hosts, and store it in the topology
    topology->CreateDelayMap();
    topology->LogDelayMap();


    return topology;
}

} // namespace json_util

} // namespace ns3
