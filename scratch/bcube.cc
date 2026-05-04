/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
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
 * Author: ChangLiu (liuchang_1307@163.com)
 */

#include "ns3/applications-module.h"
#include "ns3/core-module.h"
#include "ns3/dcb-module.h"
#include "ns3/error-model.h"
#include "ns3/global-route-manager.h"
#include "ns3/global-value.h"
#include "ns3/internet-module.h"
#include "ns3/ipv4-static-routing-helper.h"
#include "ns3/json-topology-helper.h"
#include "ns3/json-util-module.h"
#include "ns3/log.h"
#include "ns3/network-module.h"
#include "ns3/nstime.h"
#include "ns3/packet.h"
#include "ns3/point-to-point-helper.h"
#include "ns3/point-to-point-module.h"
#include "ns3/traffic-control-module.h"

#include <boost/json.hpp>
#include <fstream>
#include <iostream>
#include <map>
#include <time.h>

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("ScratchSimulator");

/***** Debug Utilities *****/

[[maybe_unused]] static void PhyTxBegin(Ptr<const Packet> pkt,
                                        std::pair<uint32_t, uint32_t> nodeAndPortId);
[[maybe_unused]] static void QdiscEnqueueWithId(Ptr<const QueueDiscItem> item,
                                                std::pair<uint32_t, uint32_t> nodeAndPortId,
                                                uint8_t prio);
[[maybe_unused]] static void PfcSent(std::pair<uint32_t, uint32_t> nodeAndPortId,
                                     uint32_t prio,
                                     bool isPause);
[[maybe_unused]] static void PfcReceived(std::pair<uint32_t, uint32_t> nodeAndPortId,
                                         uint32_t prio,
                                         bool isPause);

int
main(int argc, char* argv[])
{
    NS_LOG_UNCOND("ScratchSimulator for bcube");

    // Statistics about simulation running time
    clock_t begint, endt;
    begint = clock();

    std::string config_file;
    if (argc > 1)
    {
        // Read the configuration file
        config_file = std::string(argv[1]);
    }
    else
    {
        std::cout << "Error: require a configuration file\n";
        fflush(stdout);
        return 1;
    }

    LogComponentEnableAll(LOG_LEVEL_WARN);
    // LogComponentEnable ("PausableQueueDisc", LOG_LEVEL_INFO);
    // LogComponentEnable ("FifoQueueDiscEcn", LOG_LEVEL_INFO);
    // LogComponentEnable("ScratchSimulator", LOG_LEVEL_DEBUG);
    // LogComponentEnable("JsonTopologyHelper", LOG_LEVEL_DEBUG);
    // LogComponentEnable("DcTopology", LOG_LEVEL_DEBUG);

    // Read config in json from config file
    boost::json::object configObj = json_util::ReadConfig(config_file);

    /***** Automatical settings *****/
    // Automatically set default values using ns3 Config system
    json_util::SetDefault(configObj["defaultConfig"].get_object());
    // Automatically set global values using ns3 GlobalValue system
    json_util::SetGlobal(configObj["globalConfig"].get_object());
    // Set global seed for random generator
    json_util::SetRandomSeed(
        configObj["runtimeConfig"].get_object().find("seed")->value().as_int64());
    // Set the stop time of simulation
    json_util::SetStopTime(configObj);

    /***** Topology and application *****/
    // Build topology from topology file
    Ptr<DcTopology> topology = json_util::BuildTopology(configObj);

    // Install applications
    ApplicationContainer apps = json_util::InstallApplications(configObj, topology);

    // // Debug trace
    // Config::ConnectWithoutContext("/NodeList/*/DeviceList/*/PhyTxBeginWithId",
    //                               MakeCallback(&PhyTxBegin));
    // // Bind the callback to the pausable queue disc, no config system can be used
    // Config::MatchContainer devs = Config::LookupMatches("/NodeList/*/DeviceList/*");
    // for (auto& dev : devs)
    // {
    //     Ptr<DcbNetDevice> device = DynamicCast<DcbNetDevice>(dev);
    //     if (device == nullptr)
    //     {
    //         continue;
    //     }
    //     Ptr<PausableQueueDisc> qdisc = device->GetQueueDisc();
    //     if (qdisc != nullptr)
    //     {
    //         qdisc->TraceConnectWithoutContext("EnqueueWithId",
    //         MakeCallback(&QdiscEnqueueWithId));
    //     }
    // }
    // Bind the PFC traces
    Config::MatchContainer tcs = Config::LookupMatches("/NodeList/*/$ns3::DcbPPfcTrafficControl");
    for (auto& tc : tcs)
    {
        Ptr<DcbPPfcTrafficControl> trafficControl = DynamicCast<DcbPPfcTrafficControl>(tc);
        if (trafficControl == nullptr)
        {
            continue;
        }
        std::vector<DcbPPfcTrafficControl::PortInfo> ports = trafficControl->GetPorts();
        for (auto& port : ports)
        {
            Ptr<ns3::DcbPPfcPort> fc = DynamicCast<ns3::DcbPPfcPort>(port.GetFC());
            if (fc == nullptr)
                continue;
            port.GetFC()->TraceConnectWithoutContext("PfcSent", MakeCallback(&PfcSent));
            port.GetFC()->TraceConnectWithoutContext("PfcReceived", MakeCallback(&PfcReceived));
        }
    }

    tracer_extension::ConfigOutputDirectory("data");
    tracer_extension::ConfigTraceFCT(tracer_extension::Protocol::RoCEv2, "fct.csv");

    Simulator::Run();
    json_util::OutputStats(configObj, apps, topology, config_file);

    endt = clock();
    std::cout << "Total used time: " << (double)(endt - begint) / CLOCKS_PER_SEC << "s"
              << std::endl;
    Simulator::Destroy();
}

// For debug
enum debugType
{
    TRACK_PACKET,
    TRACK_PORT
};

const debugType debugT = TRACK_PACKET;
// Variables used for track a packet
const uint32_t dstAddr = 167772163;
const uint32_t dstQp = 100;
const uint32_t srcQp = 258;
const uint32_t psn = 43;
// Variables used for track a port's behavior
const Time debugFrom = NanoSeconds(115486788) + MicroSeconds(2);
const uint32_t nodeId = 10;
const uint32_t portId = 1;

void
PhyTxBegin(Ptr<const Packet> pkt, std::pair<uint32_t, uint32_t> nodeAndPortId)
{
    Ptr<Packet> copy = pkt->Copy();
    EthernetHeader ethH;
    copy->RemoveHeader(ethH);
    if (ethH.GetLengthType() == PfcFrame::PROT_NUMBER)
    {
        return;
    }

    if (debugT == TRACK_PORT)
    {
        if (nodeAndPortId.first == nodeId && nodeAndPortId.second == portId &&
            Simulator::Now() >= debugFrom)
        {
            NS_LOG_DEBUG("Packet send out from node " << nodeAndPortId.first << " port "
                                                      << nodeAndPortId.second << " at "
                                                      << Simulator::Now().GetNanoSeconds() << "ns");
        }
        return;
    }

    Ipv4Header ipv4Header;
    copy->RemoveHeader(ipv4Header);

    // Need to determine the protocol
    const uint8_t TCP_PROT_NUMBER = 6;  //!< TCP Protocol number
    const uint8_t UDP_PROT_NUMBER = 17; //!< UDP Protocol number
    if (ipv4Header.GetProtocol() == TCP_PROT_NUMBER)
    {
    }
    if (ipv4Header.GetProtocol() == UDP_PROT_NUMBER)
    {
        UdpHeader udpHeader;
        copy->RemoveHeader(udpHeader);
        if (udpHeader.GetDestinationPort() == RoCEv2L4Protocol::PROT_NUMBER)
        {
            RoCEv2Header rocev2Header;
            copy->RemoveHeader(rocev2Header);
            if (
                // ipv4Header.GetSource().Get() == srcAddr &&
                ipv4Header.GetDestination().Get() == dstAddr && rocev2Header.GetDestQP() == dstQp &&
                rocev2Header.GetSrcQP() == srcQp && rocev2Header.GetPSN() == psn)
            {
                NS_LOG_DEBUG("Packet send out from node "
                             << nodeAndPortId.first << " port " << nodeAndPortId.second << " at "
                             << Simulator::Now().GetNanoSeconds() << "ns");
            }
        }
    }
}

void
QdiscEnqueueWithId(Ptr<const QueueDiscItem> item,
                   std::pair<uint32_t, uint32_t> nodeAndPortId,
                   uint8_t prio)
{
    Ptr<const Ipv4QueueDiscItem> ipv4Item = DynamicCast<const Ipv4QueueDiscItem>(item);
    if (ipv4Item == nullptr)
    {
        return;
    }

    if (debugT == TRACK_PORT)
    {
        if (nodeAndPortId.first == nodeId && nodeAndPortId.second == portId &&
            Simulator::Now() >= debugFrom)
        {
            NS_LOG_DEBUG("Packet enqueue at node " << nodeAndPortId.first << " port "
                                                   << nodeAndPortId.second << " at "
                                                   << Simulator::Now().GetNanoSeconds() << "ns");
        }
        return;
    }

    Ptr<Packet> copy = ipv4Item->GetPacket()->Copy();
    Ipv4Header ipv4Header = ipv4Item->GetHeader();

    // Need to determine the protocol
    const uint8_t TCP_PROT_NUMBER = 6;  //!< TCP Protocol number
    const uint8_t UDP_PROT_NUMBER = 17; //!< UDP Protocol number
    if (ipv4Header.GetProtocol() == TCP_PROT_NUMBER)
    {
    }
    if (ipv4Header.GetProtocol() == UDP_PROT_NUMBER)
    {
        UdpHeader udpHeader;
        copy->RemoveHeader(udpHeader);
        if (udpHeader.GetDestinationPort() == RoCEv2L4Protocol::PROT_NUMBER)
        {
            RoCEv2Header rocev2Header;
            copy->RemoveHeader(rocev2Header);
            if (
                // ipv4Header.GetSource().Get() == srcAddr &&
                ipv4Header.GetDestination().Get() == dstAddr && rocev2Header.GetDestQP() == dstQp &&
                rocev2Header.GetSrcQP() == srcQp && rocev2Header.GetPSN() == psn)
            {
                NS_LOG_DEBUG("Packet enqueue at node "
                             << nodeAndPortId.first << " port " << nodeAndPortId.second << "'s "
                             << (uint8_t)prio << " priority "
                             << " at " << Simulator::Now().GetNanoSeconds() << "ns");
            }
        }
    }
}

void
PfcSent(std::pair<uint32_t, uint32_t> nodeAndPortId, uint32_t prio, bool isPause)
{
    std::string pfcType = isPause ? "PAUSE" : "RESUME";
    NS_LOG_DEBUG("PFC " << pfcType << " sent at node " << nodeAndPortId.first << " port "
                        << nodeAndPortId.second << "'s " << (uint32_t)prio << " priority "
                        << " at " << Simulator::Now().GetNanoSeconds() << "ns");
}

void
PfcReceived(std::pair<uint32_t, uint32_t> nodeAndPortId, uint32_t prio, bool isPause)
{
    std::string pfcType = isPause ? "PAUSE" : "RESUME";
    NS_LOG_DEBUG("PFC " << pfcType << " received at node " << nodeAndPortId.first << " port "
                        << nodeAndPortId.second << "'s " << (uint32_t)prio << " priority "
                        << " at " << Simulator::Now().GetNanoSeconds() << "ns");
}
