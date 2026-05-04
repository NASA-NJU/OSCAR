/*
 * Copyright (c) 2008 INRIA
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
 * Author: Pavinberg <pavin0702@gmail.com>
 */

#include "dcb-traffic-gen-application.h"

#include "dcb-net-device.h"
#include "rocev2-dcqcn.h"
#include "rocev2-l4-protocol.h"
#include "rocev2-socket.h"
#include "udp-based-socket.h"

#include "ns3/address.h"
#include "ns3/boolean.h"
#include "ns3/dcb-traffic-gen-application-helper.h"
#include "ns3/double.h"
#include "ns3/enum.h"
#include "ns3/fatal-error.h"
#include "ns3/global-value.h"
#include "ns3/integer.h"
#include "ns3/ipv4-address.h"
#include "ns3/ipv4.h"
#include "ns3/log-macros-enabled.h"
#include "ns3/loopback-net-device.h"
#include "ns3/node.h"
#include "ns3/packet-socket-address.h"
#include "ns3/pointer.h"
#include "ns3/ptr.h"
#include "ns3/simulator.h"
#include "ns3/socket.h"
#include "ns3/string.h"
#include "ns3/tcp-socket-base.h"
#include "ns3/tcp-socket-factory.h"
#include "ns3/tcp-tx-buffer.h"
#include "ns3/tracer-extension.h"
#include "ns3/type-id.h"
#include "ns3/udp-l4-protocol.h"
#include "ns3/udp-socket-factory.h"
#include "ns3/udp-socket.h"
#include "ns3/uinteger.h"

#include <cmath>
#include <unordered_set>

namespace ns3
{

NS_OBJECT_ENSURE_REGISTERED(FileRequestMetadata);

TypeId
FileRequestMetadata::GetTypeId()
{
    static TypeId tid = TypeId("ns3::FileRequestMetadata")
                            .SetParent<Object>()
                            .AddConstructor<FileRequestMetadata>();
    return tid;
}

NS_OBJECT_ENSURE_REGISTERED(RecoveryMetadata);

TypeId
RecoveryMetadata::GetTypeId()
{
    static TypeId tid =
        TypeId("ns3::RecoveryMetadata").SetParent<Object>().AddConstructor<RecoveryMetadata>();
    return tid;
}

TypeId
ParallelismMetadata::GetTypeId(void)
{
    static TypeId tid = TypeId("ns3::ParallelismMetadata")
                            .SetParent<Object>()
                            .AddConstructor<ParallelismMetadata>();
    return tid;
}

uint32_t
ParallelismMetadata::SelectMoeNode()
{
    double prob = m_moeRng->GetValue();
    uint32_t index = std::lower_bound(MoeCdf.begin(), MoeCdf.end(), prob) - MoeCdf.begin();
    return m_parallelismNodes[index % m_parallelismNodes.size()];
}

NS_OBJECT_ENSURE_REGISTERED(CoflowCdfMetadata);

TypeId
CoflowCdfMetadata::GetTypeId(void)
{
    static TypeId tid =
        TypeId("ns3::CoflowCdfMetadata").SetParent<Object>().AddConstructor<CoflowCdfMetadata>();
    return tid;
}

void
CoflowCdfMetadata::ReadingCoflowNums(std::string filename)
{
    // each line is <sorted-size maperNum reducerNum>
    std::ifstream file(filename);
    if (!file.is_open())
    {
        NS_FATAL_ERROR("Can not open file " << filename);
    }
    std::string line;
    while (std::getline(file, line))
    {
        std::istringstream iss(line);
        uint32_t size, mapperNum, reducerNum;
        iss >> size >> mapperNum >> reducerNum;
        m_mrSize.push_back(size);
        m_mrNums.push_back(std::make_pair(mapperNum, reducerNum));
    }
}

void
CoflowCdfMetadata::ReadingCoflowNumsCdf(std::string mapperFilename, std::string reducerFilename)
{
    std::unique_ptr<DcbTrafficGenApplication::TraceCdf> mapperCdf =
        DcbTrafficGenApplicationHelper::ConstructCdfFromFile(mapperFilename);
    std::unique_ptr<DcbTrafficGenApplication::TraceCdf> reducerCdf =
        DcbTrafficGenApplicationHelper::ConstructCdfFromFile(reducerFilename);
    m_coflowCdfMapperNumRng = CreateObject<EmpiricalRandomVariable>();
    m_coflowCdfMapperNumRng->SetAttribute("Interpolate", BooleanValue(false));
    double res = 0.;
    auto [ls, lp] = (*mapperCdf)[0];
    for (auto [sz, prob] : (*mapperCdf))
    {
        m_coflowCdfMapperNumRng->CDF(sz, prob);
        res += (sz + ls) / 2.0 * (prob - lp);
        ls = sz;
        lp = prob;
    }

    m_coflowCdfReducerNumRng = CreateObject<EmpiricalRandomVariable>();
    m_coflowCdfReducerNumRng->SetAttribute("Interpolate", BooleanValue(false));
    res = 0.;
    ls = 0;
    lp = 0;
    for (auto [sz, prob] : (*reducerCdf))
    {
        m_coflowCdfReducerNumRng->CDF(sz, prob);
        res += (sz + ls) / 2.0 * (prob - lp);
        ls = sz;
        lp = prob;
    }
}

NS_LOG_COMPONENT_DEFINE("DcbTrafficGenApplication");
NS_OBJECT_ENSURE_REGISTERED(DcbTrafficGenApplication);

// Define static variables
std::vector<std::vector<Time>> DcbTrafficGenApplication::m_ringIntervals;
bool DcbTrafficGenApplication::m_isRingInit = true;

std::vector<bool> DcbTrafficGenApplication::m_bgFlowFinished;

TypeId
DcbTrafficGenApplication::GetTypeId()
{
    static TypeId tid =
        TypeId("ns3::DcbTrafficGenApplication")
            .SetParent<DcbBaseApplication>()
            .SetGroupName("Dcb")
            .AddConstructor<DcbTrafficGenApplication>()
            // .AddAttribute ("Protocol",
            //                "The type of protocol to use. This should be "
            //                "a subclass of ns3::SocketFactory",
            //                TypeIdValue (UdpSocketFactory::GetTypeId ()),
            //                MakeTypeIdAccessor (&DcbTrafficGenApplication::m_socketTid),
            //                // This should check for SocketFactory as a parent
            //                MakeTypeIdChecker ())
            .AddAttribute("Interpolate",
                          "Enable interpolation from CDF",
                          BooleanValue(true),
                          MakeBooleanAccessor(&DcbTrafficGenApplication::m_interpolate),
                          MakeBooleanChecker())
            .AddAttribute("RecoveryNodeRatio",
                          "The ratio of recovery nodes",
                          DoubleValue(0.1),
                          MakeDoubleAccessor(&DcbTrafficGenApplication::m_recoveryNodeRatio),
                          MakeDoubleChecker<double>())
            .AddAttribute("MetadataGroupName",
                          "The name of the metadata group",
                          StringValue("MetadataGroup0"),
                          MakeStringAccessor(&DcbTrafficGenApplication::m_metadataGroupName),
                          MakeStringChecker())
            .AddAttribute("FileRequestNodesNum",
                          "The number of file request nodes",
                          UintegerValue(1),
                          MakeUintegerAccessor(&DcbTrafficGenApplication::m_fileRequestNodesNum),
                          MakeUintegerChecker<uint32_t>())
            .AddAttribute("RingGroupNodesNum",
                          "The number of ring group nodes",
                          UintegerValue(1),
                          MakeUintegerAccessor(&DcbTrafficGenApplication::m_ringGroupNodesNum),
                          MakeUintegerChecker<uint32_t>())
            .AddAttribute("ParallelismType",
                          "The type of parallelism",
                          EnumValue(ParallelismMetadata::ParallelismType::RESNET),
                          MakeEnumAccessor(&DcbTrafficGenApplication::m_parallelismType),
                          MakeEnumChecker(ParallelismMetadata::ParallelismType::RESNET,
                                          "ResNet",
                                          ParallelismMetadata::ParallelismType::VGG,
                                          "VGG",
                                          ParallelismMetadata::ParallelismType::MOE,
                                          "MOE"))
            .AddAttribute("ParallelismMaxPass",
                          "The maximum pass, for now just used for MOE",
                          UintegerValue(UINT32_MAX),
                          MakeUintegerAccessor(&DcbTrafficGenApplication::m_maxPass),
                          MakeUintegerChecker<uint32_t>())
            .AddAttribute("ParallelismComputeTime",
                          "The compute time of parallelism. MOE uses fixed time",
                          TimeValue(MicroSeconds(1000)),
                          MakeTimeAccessor(&DcbTrafficGenApplication::m_computeTime),
                          MakeTimeChecker())
            .AddAttribute("CoflowCdfMRNumsType",
                          "The type of coflow cdf mr nums",
                          EnumValue(DcbTrafficGenApplication::CoflowMRNumsType::BOUND),
                          MakeEnumAccessor(&DcbTrafficGenApplication::m_coflowCdfMRNumsType),
                          MakeEnumChecker(DcbTrafficGenApplication::CoflowMRNumsType::MRCDF,
                                          "Cdf",
                                          DcbTrafficGenApplication::CoflowMRNumsType::BOUND,
                                          "Bound"))
            .AddAttribute("CoflowCdfNumsFile",
                          "The file of coflow cdf nums, used for Bound type",
                          StringValue(""),
                          MakeStringAccessor(&DcbTrafficGenApplication::m_coflowCdfNumsFile),
                          MakeStringChecker())
            .AddAttribute(
                "CoflowCdfNumsMapperCdfFile",
                "The file of coflow cdf nums, used for Cdf type",
                StringValue(""),
                MakeStringAccessor(&DcbTrafficGenApplication::m_coflowCdfNumsMapperCdfFile),
                MakeStringChecker())
            .AddAttribute(
                "CoflowCdfNumsReducerCdfFile",
                "The file of coflow cdf nums, used for Cdf type",
                StringValue(""),
                MakeStringAccessor(&DcbTrafficGenApplication::m_coflowCdfNumsReducerCdfFile),
                MakeStringChecker())
            .AddAttribute(
                "IsPartofCdf",
                "If the application is part of the cdf. If so, start and end size is needed",
                BooleanValue(false),
                MakeBooleanAccessor(&DcbTrafficGenApplication::m_isPartofCdf),
                MakeBooleanChecker())
            .AddAttribute("PartofCdfType",
                          "The type of the part of cdf",
                          EnumValue(DcbTrafficGenApplication::PartofCdfType::SIZE),
                          MakeEnumAccessor(&DcbTrafficGenApplication::m_partofCdfType),
                          MakeEnumChecker(DcbTrafficGenApplication::PartofCdfType::SIZE,
                                          "Size",
                                          DcbTrafficGenApplication::PartofCdfType::PROB,
                                          "Prob",
                                          DcbTrafficGenApplication::PartofCdfType::BOTH,
                                          "Both"))
            .AddAttribute("PartofCdfPrio",
                          "The priority of the part of cdf",
                          UintegerValue(0),
                          MakeUintegerAccessor(&DcbTrafficGenApplication::m_partofCdfPrio),
                          MakeUintegerChecker<uint32_t>())
            .AddAttribute("PartofCdfTotalPrioNums",
                          "The total priority nums of the part of the CDF",
                          UintegerValue(0),
                          MakeUintegerAccessor(&DcbTrafficGenApplication::m_partofCdfTotalPrioNums),
                          MakeUintegerChecker<uint32_t>())
            .AddAttribute("TerminateTime",
                          "The time to terminate sockets' sending of SendTo node",
                          TimeValue(Seconds(0)),
                          MakeTimeAccessor(&DcbTrafficGenApplication::m_terminateTime),
                          MakeTimeChecker())
            .AddAttribute("TrafficPattern",
                          "The traffic pattern",
                          EnumValue(DcbTrafficGenApplication::TrafficPattern::CDF),
                          MakeEnumAccessor(&DcbTrafficGenApplication::m_trafficPattern),
                          MakeEnumChecker(DcbTrafficGenApplication::TrafficPattern::CDF,
                                          "CDF",
                                          DcbTrafficGenApplication::TrafficPattern::SEND_ONCE,
                                          "SendOnce",
                                          DcbTrafficGenApplication::TrafficPattern::SEND_TO,
                                          "SendTo",
                                          DcbTrafficGenApplication::TrafficPattern::FIXED_SIZE,
                                          "FixedSize",
                                          DcbTrafficGenApplication::TrafficPattern::RECOVERY,
                                          "Recovery",
                                          DcbTrafficGenApplication::TrafficPattern::FILE_REQUEST,
                                          "FileRequest",
                                          DcbTrafficGenApplication::TrafficPattern::RING,
                                          "Ring",
                                          DcbTrafficGenApplication::TrafficPattern::PARALLELISM,
                                          "Parallelism",
                                          DcbTrafficGenApplication::TrafficPattern::COFLOW_CDF,
                                          "CoflowCDF",
                                          DcbTrafficGenApplication::TrafficPattern::CHECKPOINT,
                                          "Checkpoint",
                                          DcbTrafficGenApplication::TrafficPattern::ALL2ALLV,
                                          "All2AllV"))
            .AddAttribute("IncastFlowInterval",
                          "The interval of the incast flow",
                          TimeValue(Seconds(0)),
                          MakeTimeAccessor(&DcbTrafficGenApplication::m_incastInterval),
                          MakeTimeChecker())
            .AddAttribute("TrafficSizeBytes",
                          "The size of the traffic, average size of CDF if it is use",
                          UintegerValue(0),
                          MakeUintegerAccessor(&DcbTrafficGenApplication::m_trafficSize),
                          MakeUintegerChecker<uint32_t>())
            .AddAttribute("TrafficLoad",
                          "The load of the traffic",
                          DoubleValue(1.0),
                          MakeDoubleAccessor(&DcbTrafficGenApplication::m_trafficLoad),
                          MakeDoubleChecker<double>())
            .AddAttribute("TrafficType",
                          "The type of traffic",
                          EnumValue(DcbTrafficGenApplication::TrafficType::NONE),
                          MakeEnumAccessor(&DcbTrafficGenApplication::m_trafficType),
                          MakeEnumChecker(DcbTrafficGenApplication::TrafficType::FOREGROUND,
                                          "Foreground",
                                          DcbTrafficGenApplication::TrafficType::BACKGROUND,
                                          "Background",
                                          DcbTrafficGenApplication::TrafficType::NONE,
                                          "None"))
            .AddAttribute("DestinationNode",
                          "The destination node index",
                          UintegerValue(0),
                          MakeUintegerAccessor(&DcbTrafficGenApplication::m_destNode),
                          MakeUintegerChecker<uint32_t>())
            .AddAttribute("DestFixed",
                          "If the dest is fixed",
                          BooleanValue(false),
                          MakeBooleanAccessor(&DcbTrafficGenApplication::m_isFixedDest),
                          MakeBooleanChecker())
            .AddAttribute("FixedFlowArrive",
                          "If the flow arrive interval is fixed",
                          BooleanValue(false),
                          MakeBooleanAccessor(&DcbTrafficGenApplication::m_isFixedFlowArrive),
                          MakeBooleanChecker())
            .AddAttribute("InferBatchSize",
                          "The batch size of infer",
                          UintegerValue(4000),
                          MakeUintegerAccessor(&DcbTrafficGenApplication::m_inferBatchSize),
                          MakeUintegerChecker<uint32_t>())
            .AddAttribute("FlowType",
                          "The type of flow",
                          StringValue(""),
                          MakeStringAccessor(&DcbTrafficGenApplication::SetFlowType),
                          MakeStringChecker());
    return tid;
}

DcbTrafficGenApplication::DcbTrafficGenApplication()
    : DcbBaseApplication(),
      m_fixedFlowArriveInterval(Time(0))
{
    NS_LOG_FUNCTION(this);

    m_stats = std::make_shared<Stats>(this);
}

DcbTrafficGenApplication::DcbTrafficGenApplication(Ptr<DcTopology> topology, uint32_t nodeIndex)
    : DcbBaseApplication(),
      m_fixedFlowArriveInterval(Time(0))
{
    NS_LOG_FUNCTION(this);

    m_stats = std::make_shared<Stats>(this);
}

void
DcbTrafficGenApplication::InitMembers()
{
    NS_LOG_FUNCTION(this);

    m_flowArriveTimeRng = CreateObject<ExponentialRandomVariable>();
    m_flowArriveTimeRng->SetAntithetic(true);

    if (!m_isFixedDest)
    {
        m_hostIndexRng = m_topology->CreateRamdomHostChooser();
    }
}

DcbTrafficGenApplication::~DcbTrafficGenApplication()
{
    NS_LOG_FUNCTION(this);
}

void
DcbTrafficGenApplication::CalcTrafficParameters()
{
    // Calculate the mean interval of the flow
    double flowMeanInterval =
        m_trafficSize * 8 / (m_socketLinkRate.GetBitRate() * m_trafficLoad) * 1e9; // ns

    switch (m_trafficPattern)
    {
    case TrafficPattern::CDF:
    case TrafficPattern::FIXED_SIZE:
        SetFlowMeanArriveInterval(flowMeanInterval);
        break;

    case TrafficPattern::RECOVERY:
        // In each file request event, the traffic generated is (traffic size *
        // m_topology->GetNHosts() * m_recoveryNodeRatio) And the generate process is only done at
        // one node, so the mean interval should be flowMeanInterval * m_recoveryNodeRatio
        SetFlowMeanArriveInterval(flowMeanInterval * m_recoveryNodeRatio);
        break;

    case TrafficPattern::FILE_REQUEST:
        // In each file request event, the traffic generated is traffic size * m_fileRequestNodesNum
        // And the generate process is only done at one node, so the mean interval should be
        // flowMeanInterval * m_fileRequestNodesNum / m_topology->GetNHosts()
        SetFlowMeanArriveInterval(flowMeanInterval * m_fileRequestNodesNum /
                                  m_topology->GetNHosts());
        break;
    case TrafficPattern::COFLOW_CDF:
        // std::clog << "Node" << m_nodeIndex << " " << m_stats->appFlowType << " "
        //           << "interval " << flowMeanInterval << " trafficSize" << m_trafficSize << "
        //           hosts"
        //           << m_topology->GetNHosts() << std::endl;
        SetFlowMeanArriveInterval(flowMeanInterval / m_topology->GetNHosts());
        break;

    case TrafficPattern::RING:
        // In each ring event, the traffic generated is traffic size * m_topology->GetNHosts()
        // And the generate process is only done at one node, so the mean interval should be
        // flowMeanInterval * m_topology->GetNHosts() / m_topology->GetNHosts()
        SetFlowMeanArriveInterval(flowMeanInterval);
        break;

    default:
        break;
    }

    // Calculate the traffic size according to start, stop time and load if not set
    if (m_trafficSize == 0)
    {
        NS_ASSERT_MSG(m_stopTime > m_startTime,
                      "Stop time should be meaningful if size is not set.");
        m_trafficSize = m_trafficLoad * (m_stopTime - m_startTime).GetSeconds() *
                        m_socketLinkRate.GetBitRate() / 8;
    }
}

void
DcbTrafficGenApplication::HandleRead(Ptr<Socket> socket)
{
    NS_LOG_FUNCTION(this << socket);
    Ptr<Packet> packet;
    Address from;
    // Address localAddress;
    while ((packet = socket->RecvFrom(from)))
    {
        if (InetSocketAddress::IsMatchingType(from))
        {
            NS_LOG_LOGIC("DcbTrafficGenApplication: At time "
                         << Simulator::Now().As(Time::S) << " client received " << packet->GetSize()
                         << " bytes from " << InetSocketAddress::ConvertFrom(from).GetIpv4()
                         << " port " << InetSocketAddress::ConvertFrom(from).GetPort());
        }
        else if (Inet6SocketAddress::IsMatchingType(from))
        {
            NS_LOG_LOGIC("DcbTrafficGenApplication: At time "
                         << Simulator::Now().As(Time::S) << " client received " << packet->GetSize()
                         << " bytes from " << Inet6SocketAddress::ConvertFrom(from).GetIpv6()
                         << " port " << Inet6SocketAddress::ConvertFrom(from).GetPort());
        }
        // socket->GetSockName (localAddress);
        // m_rxTrace (packet);
        // m_rxTraceWithAddresses (packet, from, localAddress);

        // Real time statistics, record them from RealTimeStatsTag
        ParallelismTag tag;
        if (packet->PeekPacketTag(tag))
        {
            // Construct the flow identifier
            InetSocketAddress inetFrom = InetSocketAddress::ConvertFrom(from);
            Ipv4Address srcAddr = inetFrom.GetIpv4(), dstAddr;
            uint32_t srcPort = inetFrom.GetPort(), dstPort = 0;
            if (m_protoGroup == ProtocolGroup::RoCEv2)
            {
                Ptr<RoCEv2Socket> roceSocket =
                    DynamicCast<RoCEv2Socket>(m_receiverSocket); // XXX Not a good way
                dstAddr = roceSocket->GetLocalAddress();
                dstPort = 100; // XXX Static for now
            }
            else if (m_protoGroup == ProtocolGroup::TCP)
            {
                Ptr<TcpSocketBase> tcpSocket =
                    DynamicCast<TcpSocketBase>(m_receiverSocket); // XXX Not a good way
                Address local;
                tcpSocket->GetSockName(local);

                // Get the address the socket is bound to
                Ptr<Ipv4> ipv4 = GetNode()->GetObject<Ipv4>();
                Ipv4Address addr =
                    ipv4->GetAddress(ipv4->GetInterfaceForDevice(GetOutboundNetDevice()), 0)
                        .GetLocal();
                NS_LOG_INFO("Receiver is bound to " << addr);

                dstAddr = addr;
                dstPort = InetSocketAddress::ConvertFrom(local).GetPort();
            }
            FlowIdentifier flowId(srcAddr, dstAddr, srcPort, dstPort);
            if (m_parallelismFlowRcvSize.find(flowId) == m_parallelismFlowRcvSize.end())
            {
                m_parallelismFlowRcvSize[flowId] = 0;
            }
            m_parallelismFlowRcvSize[flowId] += packet->GetSize();
            if (m_parallelismFlowRcvSize[flowId] >= m_trafficSize)
            {
                // std::clog << "At " << Simulator::Now() << ", pass " << m_currentPass
                //           << " completes."
                //           << " stop time: " << m_stopTime << std::endl;
                m_parallelismFlowRcvSize[flowId] = 0;
                if (m_parallelismType == ParallelismMetadata::ParallelismType::MOE)
                {
                    m_parallelismMetadata->m_curPassflowFinished++;
                    m_currentPass = m_parallelismMetadata->m_totalCurPass + 1;
                    if (m_parallelismMetadata->m_curPassflowFinished ==
                        m_parallelismMetadata->m_parallelismNodes.size())
                    {
                        // the last flow of the current pass
                        std::clog << "At " << Simulator::Now() << ", pass "
                                  << m_parallelismMetadata->m_totalCurPass
                                  << " completes with Node " << m_nodeIndex << std::endl;
                        m_parallelismMetadata->m_totalCurPass = m_currentPass;
                        m_parallelismMetadata->m_curPassflowFinished = 0;
                        ScheduleNextParallelismFlow();
                    }
                    else
                    {
                        std::clog << "At " << Simulator::Now() << ", Node " << m_nodeIndex
                                  << " finish flow and wait. curPassFlowFinished: "
                                  << m_parallelismMetadata->m_curPassflowFinished << std::endl;
                        // wait for the last flow of the current pass
                        MoePollingCheckPass();
                    }
                }
                else
                {
                    m_currentPass++;
                    ScheduleNextParallelismFlow();
                }
            }
        }
    }
}

uint32_t
DcbTrafficGenApplication::GenDestNodeIdFromDestCdf()
{
    double randomValue = m_destCdfRng->GetValue();
    auto it = std::lower_bound(m_destCdf.begin(), m_destCdf.end(), randomValue);
    return it - m_destCdf.begin(); // Get the index of the found element
}

void
DcbTrafficGenApplication::ConstructAll2AllVDestCdf()
{
    static std::vector<double> originalProb = {0.0020186727226848347,
                                        0.0063083522583901085,
                                        0.05677517032551098,
                                        0.03305576583396417,
                                        0.0035326772646984608,
                                        0.07645722937168811,
                                        0.17587686096391622,
                                        0.012869038607115822,
                                        0.22382033812768104,
                                        0.0055513499873832955,
                                        0.0035326772646984608,
                                        0.0047943477163764825,
                                        0.0070653545293969215,
                                        0.15972747918243754,
                                        0.20186727226848347,
                                        0.026747413575574062};
    double sum = 0.0;

    for (uint32_t i = 0; i < originalProb.size(); ++i)
    {
        for (uint32_t j = 0; j < 4; ++j)
        {
            sum += originalProb[i] / 4;
            m_destCdf.push_back(sum);
            // std::cout << sum << " ";
        }
    }
    std::cout<<"total size: "<<m_destCdf.size()<<std::endl;
    m_destCdf.back() = 1.0;
    m_destCdfRng = CreateObject<UniformRandomVariable>();
    m_destCdfRng->SetAttribute("Min", DoubleValue(0.0));
    m_destCdfRng->SetAttribute("Max", DoubleValue(1.0));
}

void
DcbTrafficGenApplication::GenerateTraffic()
{
    // Sanity check
    NS_ASSERT_MSG(
        m_trafficType != TrafficType::FOREGROUND ||
            (m_trafficType == TrafficType::FOREGROUND && m_trafficPattern == TrafficPattern::CDF),
        "If the traffic type is foreground, the traffic pattern should be CDF. ");

    // Used to access metadata
    PointerValue pv;

    switch (m_trafficPattern)
    {
    case TrafficPattern::CDF:
        if (m_trafficType == TrafficType::FOREGROUND)
        {
            // Schedule the next foreground flow
            Simulator::Schedule(GetNextFlowArriveInterval(),
                                &DcbTrafficGenApplication::ScheduleAForegroundFlow,
                                this);
        }
        else
        {
            // Schedule all flows in the beginning
            for (Time t = Simulator::Now() + GetNextFlowArriveInterval(); t < m_stopTime;
                 t += GetNextFlowArriveInterval())
            {
                // defer the ScheduleNextFlow to avoid rng pollute
                Simulator::Schedule(Time(0), &DcbTrafficGenApplication::ScheduleNextFlow, this, t);
                // ScheduleNextFlow(t);
            }
        }
        break;
    case ALL2ALLV:{
        ConstructAll2AllVDestCdf();
        for (uint32_t i = 0; i < m_inferBatchSize; i++)
        {
            std::unordered_set<uint32_t> nodes;
            while (nodes.size() < 9)
            {
                uint32_t destNode = 0;
                do
                {
                    destNode = GenDestNodeIdFromDestCdf();
                } while (destNode == m_nodeIndex || nodes.find(destNode) != nodes.end());
                nodes.insert(destNode);
                m_destCnter[destNode]++;
            }
        }
        while (!m_destCnter.empty())
        {
            ScheduleNextFlow(Simulator::Now());
        }
        break;
    }
    case TrafficPattern::SEND_ONCE:
        // Schedule only once
        ScheduleNextFlow(Simulator::Now());
        break;
    case TrafficPattern::SEND_TO:
        // Schedule only once, and set terminate time
        ScheduleNextFlow(Simulator::Now());
        ScheduleTerminate();
        break;
    case TrafficPattern::FIXED_SIZE:
        // Schedule all flows in the beginning
        for (Time t = Simulator::Now() + GetNextFlowArriveInterval(); t < m_stopTime;
             t += GetNextFlowArriveInterval())
        {
            ScheduleNextFlow(t);
        }
        break;
    case TrafficPattern::RECOVERY:
        if (!GlobalValue::GetValueByNameFailSafe(m_metadataGroupName, pv))
        {
            // new a metadata and a global value here
            m_recoveryMetadata = Create<RecoveryMetadata>();
            pv.Set(m_recoveryMetadata);
            new ns3::GlobalValue(m_metadataGroupName,
                                 m_metadataGroupName,
                                 pv,
                                 MakePointerChecker<RecoveryMetadata>());
            CreateRecovery();
        }
        else
        {
            m_recoveryMetadata = pv.Get<RecoveryMetadata>();
        }
        // traverse all recovery intervals
        for (uint32_t i = 0; i < m_recoveryMetadata->m_recoveryInterval.size(); i++)
        { // check if current node is in the current recovery nodesset
            if (m_recoveryMetadata->m_recoveryIntervalNodes[i].find(m_nodeIndex) !=
                m_recoveryMetadata->m_recoveryIntervalNodes[i].end())
            {
                ScheduleNextFlow(m_recoveryMetadata->m_recoveryInterval[i]);
            }
        }
        break;
    case TrafficPattern::FILE_REQUEST:
        m_fileRequestIndex = 0;
        if (!GlobalValue::GetValueByNameFailSafe(m_metadataGroupName, pv))
        {
            // new a metadata and a global value here
            m_fileRequestMetadata = Create<FileRequestMetadata>();
            pv.Set(m_fileRequestMetadata);
            new ns3::GlobalValue(m_metadataGroupName,
                                 m_metadataGroupName,
                                 pv,
                                 MakePointerChecker<FileRequestMetadata>());
            CreateFileRequest();
        }
        else
        {
            m_fileRequestMetadata = pv.Get<FileRequestMetadata>();
        }
        // traverse all file request intervals
        for (uint32_t i = 0; i < m_fileRequestMetadata->m_fileRequestInterval.size(); i++)
        { // check if current node is in the current file request nodesset
            if (m_fileRequestMetadata->m_fileRequestIntervalNodes[i].find(m_nodeIndex) !=
                m_fileRequestMetadata->m_fileRequestIntervalNodes[i].end())
            {
                ScheduleNextFlow(m_fileRequestMetadata->m_fileRequestInterval[i] + m_incastInterval*m_nodeIndex);
                // if (i == 0)
                // {
                // std::cout << "Node " << m_nodeIndex << " schedule flow at " << (m_fileRequestMetadata->m_fileRequestInterval[i] + m_incastInterval*m_nodeIndex).GetNanoSeconds() << std::endl;
                // std::cout << m_incastInterval.GetNanoSeconds() << std::endl;
                // }
            }
            m_fileRequestIndex++;
        }
        break;
    case TrafficPattern::COFLOW_CDF:
        m_coflowCdfIndex = 0;
        if (!GlobalValue::GetValueByNameFailSafe(m_metadataGroupName, pv))
        {
            // new a metadata and a global value here
            m_coflowCdfMetadata = Create<CoflowCdfMetadata>();
            pv.Set(m_coflowCdfMetadata);
            new ns3::GlobalValue(m_metadataGroupName,
                                 m_metadataGroupName,
                                 pv,
                                 MakePointerChecker<CoflowCdfMetadata>());
            CreateCoflowCdf();
        }
        else
        {
            m_coflowCdfMetadata = pv.Get<CoflowCdfMetadata>();
        }
        // traverse all coflow cdf intervals
        for (uint32_t i = 0; i < m_coflowCdfMetadata->m_coflowCdfInterval.size(); i++)
        { // check if current node is in the current coflow cdf nodesset
            if (m_coflowCdfMetadata->m_coflowCdfIntervalSrcNodes[i].find(m_nodeIndex) !=
                m_coflowCdfMetadata->m_coflowCdfIntervalSrcNodes[i].end())
            {
                ScheduleNextCoflow(i);
            }
            m_coflowCdfIndex++;
        }
        break;
    case TrafficPattern::RING:
        if (m_isRingInit)
        {
            CreateRing();
            m_isRingInit = false;
        }
        // Calculate the current application's group index using current index and group size
        m_ringGroupIndex = m_nodeIndex / m_ringGroupNodesNum;
        // Calculate the current application's destination node
        m_ringDestNode =
            (m_nodeIndex + 1) % m_ringGroupNodesNum + m_ringGroupIndex * m_ringGroupNodesNum;
        // Traverse curent group's intervals
        for (uint32_t i = 0; i < m_ringIntervals[m_ringGroupIndex].size(); i++)
        {
            ScheduleNextFlow(m_ringIntervals[m_ringGroupIndex][i]);
        }
        break;
    case TrafficPattern::CHECKPOINT:
        SchedulCheckpointFlow();
        break;
    case TrafficPattern::PARALLELISM:
        m_currentPass = 0;
        if (!GlobalValue::GetValueByNameFailSafe(m_metadataGroupName, pv))
        {
            // new a metadata and a global value here
            m_parallelismMetadata = Create<ParallelismMetadata>();
            pv.Set(m_parallelismMetadata);
            new ns3::GlobalValue(m_metadataGroupName,
                                 m_metadataGroupName,
                                 pv,
                                 MakePointerChecker<ParallelismMetadata>());
            if (m_parallelismType == ParallelismMetadata::ParallelismType::RESNET ||
                m_parallelismType == ParallelismMetadata::ParallelismType::VGG)
            {
                // for DATA parallelism, every node has all layers
                m_parallelismMetadata->m_parallelismNodes = std::vector<uint32_t>();
                const std::vector<ParallelismMetadata::WorkloadLayer>& layers =
                    ParallelismMetadata::m_parallelismWorkloads[m_parallelismType];
                m_parallelismMetadata->m_workloadLayers =
                    std::vector<ParallelismMetadata::WorkloadLayer>(layers);
            }
            else if (m_parallelismType == ParallelismMetadata::ParallelismType::MOE)
            {
                // create cdf
                double sum = 0;
                for (uint32_t i = 0; i < m_parallelismMetadata->MoeProbability.size(); i++)
                {
                    sum += m_parallelismMetadata->MoeProbability[i];
                    m_parallelismMetadata->MoeCdf.push_back(sum);
                }
                // create rng
                m_parallelismMetadata->m_moeRng = CreateObject<UniformRandomVariable>();
                m_parallelismMetadata->m_moeRng->SetAttribute("Min", DoubleValue(0));
                m_parallelismMetadata->m_moeRng->SetAttribute("Max", DoubleValue(1));
            }
            else
            {
                NS_LOG_ERROR("Parallelism type can not be recognized.");
            }
        }
        else
        {
            m_parallelismMetadata = pv.Get<ParallelismMetadata>();
        }
        m_parallelismMetadata->m_parallelismNodes.push_back(m_nodeIndex);
        m_parallelismIndexinMetaData = m_parallelismMetadata->m_parallelismNodes.size() - 1;
        if (m_parallelismType == ParallelismMetadata::ParallelismType::MOE)
        {
            Simulator::Schedule(Time(0),
                                &DcbTrafficGenApplication::ScheduleNextParallelismFlow,
                                this);
        }
        else
        {
            CreateParallelismFlow();
        }
        break;
    default:
        NS_FATAL_ERROR("Traffic pattern can not be recognized.");
        break;
    }

    if (m_protoGroup == ProtocolGroup::RoCEv2)
    {
        tracer_extension::RegisterTraceFCT(this);
    }
}

uint32_t
DcbTrafficGenApplication::GetDestinationNode() 
{
    NS_LOG_FUNCTION(this);

    if (!m_isFixedDest)
    {
        switch (m_trafficPattern)
        {
        case TrafficPattern::FILE_REQUEST:
            return m_fileRequestMetadata->m_fileRequestIntervalDest[m_fileRequestIndex];
        case TrafficPattern::RING:
            return m_ringDestNode;
        case TrafficPattern::ALL2ALLV:{
            auto it = m_destCnter.begin();
            return it->first;
        }
        default:
            // randomly send to a host
            uint32_t destNode;
            do
            {
                destNode = m_hostIndexRng->GetInteger();
            } while (destNode == m_nodeIndex);
            return destNode;
        }
    }
    else
    {
        return m_destNode;
    }
}

void
DcbTrafficGenApplication::ScheduleNextFlow(const Time& startTime)
{
    uint32_t destNode = 0;
    Ptr<Socket> socket;

    destNode = GetDestinationNode();
    socket = CreateNewSocket(destNode, m_flowPriority);

    uint64_t size = GetNextFlowSize();

    // XXX If use dest addr, the dest node is not set
    Flow* flow = new Flow(size, startTime, destNode, socket);
    SetFlowIdentifier(flow, socket);
    m_flows.emplace(socket, flow); // used when flow completes
    Simulator::Schedule(startTime - Simulator::Now(),
                        &DcbTrafficGenApplication::SendNextPacket,
                        this,
                        flow);
    // If socket is TCP, trace the unack sequence to check if the flow ends
    if (m_protoGroup == ProtocolGroup::TCP)
    {
        Ptr<TcpTxBuffer> tcpTxBuffer = DynamicCast<TcpSocketBase>(socket)->GetTxBuffer();
        tcpTxBuffer->TraceConnectWithoutContext(
            "UnackSequence",
            MakeBoundCallback(&DcbBaseApplication::TcpFlowEnds, flow));
    }

    // If the trafficType is BACKGROUND, we need to add a false to m_bgFlowFinished
    if (m_trafficType == TrafficType::BACKGROUND)
    {
        m_bgFlowFinished.push_back(false);
    }
}

void
DcbTrafficGenApplication::ScheduleTerminate()
{
    // get the socket from m_flows, which should only have one element
    Ptr<Socket> socket = m_flows.begin()->first;
    // Check if the socket is a RoCEv2Socket
    Ptr<RoCEv2Socket> roceSocket = DynamicCast<RoCEv2Socket>(socket);
    if (roceSocket == nullptr)
    {
        NS_FATAL_ERROR("The socket should be a RoCEv2Socket");
    }
    // Shedule the terminate event
    Simulator::Schedule(m_terminateTime - Simulator::Now(), &RoCEv2Socket::Terminate, roceSocket);
}

void
DcbTrafficGenApplication::CreateRecovery()
{
    // Schedule all flows in the beginning
    for (Time t = Simulator::Now(); t < m_stopTime; t += GetNextFlowArriveInterval())
    {
        m_recoveryMetadata->m_recoveryInterval.push_back(t);

        uint32_t nodeNum = m_topology->GetNHosts() * m_recoveryNodeRatio;
        m_recoveryMetadata->m_recoveryIntervalNodes.push_back(std::set<uint32_t>());

        // Select recovery node
        while (m_recoveryMetadata->m_recoveryIntervalNodes.back().size() < nodeNum)
        {
            uint32_t node = m_hostIndexRng->GetInteger();
            if (m_recoveryMetadata->m_recoveryIntervalNodes.back().find(node) ==
                m_recoveryMetadata->m_recoveryIntervalNodes.back().end())
            {
                m_recoveryMetadata->m_recoveryIntervalNodes.back().insert(node);
            }
        }
    }
    std::cout << m_recoveryMetadata->m_recoveryIntervalNodes.size() << std::endl;
}

void
DcbTrafficGenApplication::CreateFileRequest()
{
    // Schedule all flows in the beginning
    for (Time t = Simulator::Now() + GetNextFlowArriveInterval(); t < m_stopTime;
         t += GetNextFlowArriveInterval())
    {
        m_fileRequestMetadata->m_fileRequestInterval.push_back(t);

        m_fileRequestMetadata->m_fileRequestIntervalNodes.push_back(std::set<uint32_t>());
        // Select file request destination node
        uint32_t destNode = m_hostIndexRng->GetInteger();
        m_fileRequestMetadata->m_fileRequestIntervalDest.push_back(destNode);

        // Select file request node
        while (m_fileRequestMetadata->m_fileRequestIntervalNodes.back().size() <
               m_fileRequestNodesNum)
        {
            uint32_t node = m_hostIndexRng->GetInteger();
            if (m_fileRequestMetadata->m_fileRequestIntervalNodes.back().find(node) ==
                    m_fileRequestMetadata->m_fileRequestIntervalNodes.back().end() &&
                node != destNode)
            {
                m_fileRequestMetadata->m_fileRequestIntervalNodes.back().insert(node);
            }
        }
    }
}

void
DcbTrafficGenApplication::CreateCoflowCdf()
{
    if (m_coflowCdfMRNumsType == CoflowMRNumsType::MRCDF)
    {
        m_coflowCdfMetadata->ReadingCoflowNumsCdf(m_coflowCdfNumsMapperCdfFile,
                                                  m_coflowCdfNumsReducerCdfFile);
    }
    else if (m_coflowCdfMRNumsType == CoflowMRNumsType::BOUND)
    {
        m_coflowCdfMetadata->ReadingCoflowNums(m_coflowCdfNumsFile);
    }
    else
    {
        NS_FATAL_ERROR("CoflowMRNumsType can not be recognized.");
    }
    // Schedule all flows in the beginning
    for (Time t = Simulator::Now() + GetNextFlowArriveInterval(); t < m_stopTime;
         t += GetNextFlowArriveInterval())
    {
        m_coflowCdfMetadata->m_coflowCdfInterval.push_back(t);
        uint64_t trafficSize = GetNextFlowSize();
        uint32_t srcNums = 0, dstNums = 0;

        if (m_coflowCdfMRNumsType == CoflowMRNumsType::MRCDF)
        {
            srcNums = m_coflowCdfMetadata->m_coflowCdfMapperNumRng->GetInteger();
            dstNums = m_coflowCdfMetadata->m_coflowCdfReducerNumRng->GetInteger();
        }
        else if (m_coflowCdfMRNumsType == CoflowMRNumsType::BOUND)
        {
            uint32_t lower_bound_idx = std::lower_bound(m_coflowCdfMetadata->m_mrSize.begin(),
                                                        m_coflowCdfMetadata->m_mrSize.end(),
                                                        trafficSize) -
                                       m_coflowCdfMetadata->m_mrSize.begin();
            srcNums = m_coflowCdfMetadata->m_mrNums[lower_bound_idx].first;
            dstNums = m_coflowCdfMetadata->m_mrNums[lower_bound_idx].second;
        }

        // if trafficSize(byte) / (srcNums * dstNums) < 1e3 or > 1e9, we need to reselect
        while ((trafficSize / (srcNums * dstNums) < 1e3 && trafficSize >= 1e3) ||
               trafficSize / (srcNums * dstNums) > 1e8)
        {
            trafficSize = GetNextFlowSize();
            if (m_coflowCdfMRNumsType == CoflowMRNumsType::MRCDF)
            {
                srcNums = m_coflowCdfMetadata->m_coflowCdfMapperNumRng->GetInteger();
                dstNums = m_coflowCdfMetadata->m_coflowCdfReducerNumRng->GetInteger();
            }
            // std::clog << "Reselect traffic size: " << trafficSize << " srcNums: " << srcNums
            //           << " dstNums: " << dstNums << std::endl;
        }

        m_coflowCdfMetadata->m_coflowCdfIntervalSrcNodes.push_back(std::set<uint32_t>());
        m_coflowCdfMetadata->m_coflowCdfIntervalDstNodes.push_back(std::set<uint32_t>());
        // Randomly select src and dst nodes
        while (srcNums > 0)
        {
            uint32_t srcNode = m_hostIndexRng->GetInteger();
            if (m_coflowCdfMetadata->m_coflowCdfIntervalSrcNodes.back().find(srcNode) ==
                m_coflowCdfMetadata->m_coflowCdfIntervalSrcNodes.back().end())
            {
                m_coflowCdfMetadata->m_coflowCdfIntervalSrcNodes.back().insert(srcNode);
                srcNums--;
            }
        }
        while (dstNums > 0)
        {
            uint32_t dstNode = m_hostIndexRng->GetInteger();
            if (m_coflowCdfMetadata->m_coflowCdfIntervalDstNodes.back().find(dstNode) ==
                    m_coflowCdfMetadata->m_coflowCdfIntervalDstNodes.back().end() &&
                m_coflowCdfMetadata->m_coflowCdfIntervalSrcNodes.back().find(dstNode) ==
                    m_coflowCdfMetadata->m_coflowCdfIntervalSrcNodes.back().end())
            {
                m_coflowCdfMetadata->m_coflowCdfIntervalDstNodes.back().insert(dstNode);
                dstNums--;
            }
        }
    }
}

void
DcbTrafficGenApplication::ScheduleNextCoflow(uint32_t coflowIndex)
{
    // Send each dst node a flow, size = size[i] / (srcNums[i] * dstNums[i])
    for (auto dstNode : m_coflowCdfMetadata->m_coflowCdfIntervalDstNodes[coflowIndex])
    {
        Ptr<Socket> socket = CreateNewSocket(dstNode, m_flowPriority);
        uint64_t size = GetNextFlowSize() /
                        (m_coflowCdfMetadata->m_coflowCdfIntervalSrcNodes[coflowIndex].size() *
                         m_coflowCdfMetadata->m_coflowCdfIntervalDstNodes[coflowIndex].size());

        Flow* flow =
            new Flow(size, m_coflowCdfMetadata->m_coflowCdfInterval[coflowIndex], dstNode, socket);
        SetFlowIdentifier(flow, socket);
        m_flows.emplace(socket, flow);                                  // used when flow completes
        m_coflowCdfIndexMap.emplace(flow->flowIdentifier, coflowIndex); // used when flow completes
        Simulator::Schedule(m_coflowCdfMetadata->m_coflowCdfInterval[coflowIndex] -
                                Simulator::Now(),
                            &DcbTrafficGenApplication::SendNextPacket,
                            this,
                            flow);
        // If socket is TCP, trace the unack sequence to check if the flow ends
        if (m_protoGroup == ProtocolGroup::TCP)
        {
            Ptr<TcpTxBuffer> tcpTxBuffer = DynamicCast<TcpSocketBase>(socket)->GetTxBuffer();
            tcpTxBuffer->TraceConnectWithoutContext(
                "UnackSequence",
                MakeBoundCallback(&DcbBaseApplication::TcpFlowEnds, flow));
        }

        // If the trafficType is BACKGROUND, we need to add a false to m_bgFlowFinished
        if (m_trafficType == TrafficType::BACKGROUND)
        {
            m_bgFlowFinished.push_back(false);
        }
    }
}

void
DcbTrafficGenApplication::CreateRing()
{
    if (!m_isRingInit)
    {
        return;
    }
    uint32_t totalHosts = m_topology->GetNHosts();
    if (totalHosts % m_ringGroupNodesNum != 0)
    {
        NS_FATAL_ERROR(
            "The number of hosts should be a multiple of the number of ring group nodes");
    }
    uint32_t groupNum = totalHosts / m_ringGroupNodesNum;
    for (uint32_t i = 0; i < groupNum; i++)
    {
        m_ringIntervals.push_back(std::vector<Time>());
        for (Time t = Simulator::Now() + GetNextFlowArriveInterval(); t < m_stopTime;
             t += GetNextFlowArriveInterval())
        {
            m_ringIntervals[i].push_back(t);
        }
    }
}

void
DcbTrafficGenApplication::CreateParallelismFlow()
{
    if (m_parallelismType == ParallelismMetadata::ParallelismType::RESNET ||
        m_parallelismType == ParallelismMetadata::ParallelismType::VGG)
    {
        // DATA PARALLELISM
        m_computeTime = Time(0);
        m_trafficSize = 0;
        // Traverse all workload layers
        // for ResNet, computeTime += all layers' fwdcomp + last layer's wgcomp
        // trafficSize += all layers' wgcomm
        // And communicate Type shall be ALLREDUCE
        for (uint32_t i = 0; i < m_parallelismMetadata->m_workloadLayers.size(); i++)
        {
            m_computeTime += m_parallelismMetadata->m_workloadLayers[i].m_fwd_computeTime;
            m_trafficSize += m_parallelismMetadata->m_workloadLayers[i].m_wg_communicateSize;
        }
        m_computeTime += m_parallelismMetadata->m_workloadLayers.back().m_wg_computeTime;
        std::cout << "Compute time: " << m_computeTime.GetNanoSeconds() << std::endl;
        std::cout << "Traffic size: " << m_trafficSize * 8. / m_socketLinkRate.GetBitRate() * 1e9
                  << std::endl;
        Simulator::Schedule(Time(0), &DcbTrafficGenApplication::ScheduleNextParallelismFlow, this);
    }
    else
    {
        NS_LOG_ERROR("Parallelism type can not be recognized.");
    }
}

void
DcbTrafficGenApplication::ScheduleNextParallelismFlow()
{
    if (Simulator::Now() >= m_stopTime || m_currentPass > m_maxPass)
    {
        return;
    }
    ParallelismTag tag;
    tag.SetParallelismType(static_cast<uint32_t>(m_parallelismType));
    uint64_t trafficSize = UINT64_MAX;
    uint32_t destNode = UINT32_MAX;
    if (m_parallelismType == ParallelismMetadata::ParallelismType::RESNET ||
        m_parallelismType == ParallelismMetadata::ParallelismType::VGG)
    {
        uint32_t n = m_parallelismMetadata->m_parallelismNodes.size();
        trafficSize = 2 * (double(n - 1) / (double)n) * m_trafficSize;
        destNode = m_parallelismMetadata
                       ->m_parallelismNodes[(m_parallelismIndexinMetaData + 1) %
                                            m_parallelismMetadata->m_parallelismNodes.size()];
    }
    else if (m_parallelismType == ParallelismMetadata::ParallelismType::MOE)
    {
        trafficSize = m_trafficSize;
        do
        {
            destNode = m_parallelismMetadata->SelectMoeNode();
        } while (destNode == m_nodeIndex);
    }
    Ptr<Socket> socket;
    socket = CreateNewSocket(destNode, m_flowPriority);
    // std::clog << "At " << Simulator::Now() << ", Node " << m_nodeIndex << " pass " << m_currentPass
    //           << " starts to dest " << destNode << ", traffic size " << trafficSize << std::endl;

    // XXX If use dest addr, the dest node is not set
    Flow* flow = new Flow(trafficSize, Simulator::Now() + m_computeTime, destNode, socket);
    SetFlowIdentifier(flow, socket);
    m_flows.emplace(socket, flow); // used when flow completes
    if (m_currentPass == 0)
    {
        Simulator::Schedule(
            Time(0),
            &DcbTrafficGenApplication::SendNextPacketWithTags,
            this,
            flow,
            std::vector<std::shared_ptr<Tag>>{std::make_shared<ParallelismTag>(tag)});
    }
    else
    {
        Simulator::Schedule(
            m_computeTime,
            &DcbTrafficGenApplication::SendNextPacketWithTags,
            this,
            flow,
            std::vector<std::shared_ptr<Tag>>{std::make_shared<ParallelismTag>(tag)});
    }
    // If socket is TCP, trace the unack sequence to check if the flow ends
    if (m_protoGroup == ProtocolGroup::TCP)
    {
        Ptr<TcpTxBuffer> tcpTxBuffer = DynamicCast<TcpSocketBase>(socket)->GetTxBuffer();
        tcpTxBuffer->TraceConnectWithoutContext(
            "UnackSequence",
            MakeBoundCallback(&DcbBaseApplication::TcpFlowEnds, flow));
    }

    // If the trafficType is BACKGROUND, we need to add a false to m_bgFlowFinished
    if (m_trafficType == TrafficType::BACKGROUND)
    {
        m_bgFlowFinished.push_back(false);
    }
}

void
DcbTrafficGenApplication::MoePollingCheckPass()
{
    // std::clog << "At " << Simulator::Now() << ", Node " << m_nodeIndex << " check total pass "
    //           << m_parallelismMetadata->m_totalCurPass << " current pass " << m_currentPass
    //           << std::endl;
    if (m_parallelismMetadata->m_totalCurPass < m_currentPass)
    {
        // schedule next polling
        Simulator::Schedule(m_parallelismMetadata->m_pollingInterval,
                            &DcbTrafficGenApplication::MoePollingCheckPass,
                            this);
    }
    else
    {
        // m_currentPass = m_parallelismMetadata->m_totalCurPass;
        // schedule next pass
        ScheduleNextParallelismFlow();
    }
}

void
DcbTrafficGenApplication::SchedulCheckpointFlow()
{
    NS_LOG_FUNCTION(this);
    // Set the dest to the checkpoint node, ie, the neighbor of the node
    // the neighbor node is like (0 1) (2 3), we have our node index, so we can get the neighbor
    uint32_t destNode = m_nodeIndex % 2 == 0 ? m_nodeIndex + 1 : m_nodeIndex - 1;
    Ptr<Socket> socket = CreateNewSocket(destNode, m_flowPriority);

    ScheduleNextFlow(Simulator::Now());
}

void
DcbTrafficGenApplication::ScheduleAForegroundFlow()
{
    NS_LOG_FUNCTION(this);
    // Stop condition: all the background flows are finished, ie, m_bgFlowFinished is all true
    // Use std::all_of to check
    if (std::all_of(m_bgFlowFinished.begin(), m_bgFlowFinished.end(), [](bool b) { return b; }))
    {
        return;
    }

    ScheduleNextFlow(Simulator::Now());

    // Schedule the next foreground flow
    Simulator::Schedule(GetNextFlowArriveInterval(),
                        &DcbTrafficGenApplication::ScheduleAForegroundFlow,
                        this);
}

void
DcbTrafficGenApplication::SetFlowMeanArriveInterval(double interval)
{
    NS_LOG_FUNCTION(this << interval);
    if (m_isFixedFlowArrive)
    {
        m_fixedFlowArriveInterval = Time(NanoSeconds(interval));
        m_flowArriveTimeRng = nullptr;
    }
    else
    {
        m_fixedFlowArriveInterval = Time(0);
        // std::clog << "Node" << m_nodeIndex << " " << m_stats->appFlowType << " interval "
        //           << interval << std::endl;
        m_flowArriveTimeRng->SetAttribute("Mean", DoubleValue(interval)); // in ns
    }
}

void
DcbTrafficGenApplication::SetFlowCdf(const TraceCdf& cdf)
{
    NS_LOG_FUNCTION(this);

    m_flowSizeRng = CreateObject<EmpiricalRandomVariable>();
    // Enable interpolation from CDF
    m_flowSizeRng->SetAttribute("Interpolate", BooleanValue(m_interpolate));

    std::vector<std::pair<uint32_t, double>> newCdf;
    uint32_t partofCdfStartSize = 0, partofCdfEndSize = 0;
    if (m_isPartofCdf)
    {
        // 0. calculate the startsize and endsize according to type and prio
        if (m_partofCdfType == PartofCdfType::PROB)
        {
            double startProb = m_partofCdfPrio * 1.0 / m_partofCdfTotalPrioNums;
            double endProb = (m_partofCdfPrio + 1) * 1.0 / m_partofCdfTotalPrioNums;
            uint32_t os = 0;
            double op = 0.;

            bool start = true;

            for (auto [sz, prob] : cdf)
            {
                if (prob >= startProb && start)
                { // interpolation
                    partofCdfStartSize =
                        prob == startProb ? sz : (startProb - op) / (prob - op) * (sz - os) + os;
                    start = false;
                }
                if (prob >= endProb)
                { // interpolation
                    partofCdfEndSize =
                        prob == endProb ? sz : (endProb - op) / (prob - op) * (sz - os) + os;
                    break;
                }
                os = sz;
                op = prob;
            }
        }
        else if (m_partofCdfType == PartofCdfType::SIZE)
        {
            partofCdfStartSize = cdf.back().first / m_partofCdfTotalPrioNums * m_partofCdfPrio;
            partofCdfEndSize = cdf.back().first / m_partofCdfTotalPrioNums * (m_partofCdfPrio + 1);
        }
        else if (m_partofCdfType == PartofCdfType::BOTH)
        {
            double total = 0.;
            uint32_t os = 0;
            double op = 0.;
            for (auto [sz, prob] : cdf)
            {
                total += (sz + os) / 2.0 * (prob - op);
                os = sz;
                op = prob;
            }
            double both_start = total / m_partofCdfTotalPrioNums * m_partofCdfPrio;
            double both_end = total / m_partofCdfTotalPrioNums * (m_partofCdfPrio + 1);

            bool start = true;
            double cnt = 0.;
            os = 0;
            op = 0.;
            uint32_t ob = 0;
            for (auto [sz, prob] : cdf)
            {
                cnt += (sz + os) / 2.0 * (prob - op);
                if (cnt >= both_start && start)
                { // interpolation
                    partofCdfStartSize =
                        cnt == both_start ? sz : (both_start - ob) / (cnt - ob) * (sz - os) + os;
                    start = false;
                }
                if (cnt >= both_end)
                { // interpolation
                    partofCdfEndSize =
                        cnt == both_end ? sz : (both_end - ob) / (cnt - ob) * (sz - os) + os;
                    break;
                }
                os = sz;
                op = prob;
                ob = cnt;
            }
        }

        // 1. peek a new cdf
        uint32_t ls = partofCdfStartSize;
        double lp = 0.;
        bool init = false;
        for (auto [sz, prob] : cdf)
        {
            if (sz >= partofCdfStartSize)
            {
                if (newCdf.empty())
                {
                    // first element, add the start size
                    if (sz != partofCdfStartSize) // do the interpolation
                    {
                        lp = (prob - lp) / (sz - ls) * (partofCdfStartSize - ls) + lp;
                        ls = partofCdfStartSize;
                        newCdf.push_back({partofCdfStartSize, lp});
                        init = true;
                    }
                    else
                    {
                        newCdf.push_back({sz, prob});
                    }
                }
                else if (sz < partofCdfEndSize)
                {
                    newCdf.push_back({sz, prob});
                }
            }
            if (sz >= partofCdfEndSize)
            {
                if (sz != partofCdfEndSize) // do the interpolation
                {
                    // last element, add the end size
                    lp = (prob - lp) / (sz - ls) * (partofCdfEndSize - ls) + lp;
                    newCdf.push_back({partofCdfEndSize, lp});
                }
                else
                {
                    newCdf.push_back({sz, prob});
                }
                break;
            }
            if (init)
            {
                init = false;
                continue; // skip updating ls and lp
            }
            ls = sz;
            lp = prob;
        }
        // if (newCdf.begin()->first != 0)
        //     newCdf.insert(newCdf.begin(), {0, 0}); // insert {zero, zero} into the new cdf

        // 2. calculate the new traffic load
        m_trafficLoad *= 1.0 * (newCdf.back().second - newCdf.front().second) / cdf.back().second;

        // 3. normalize the new cdf
        for (auto& [sz, prob] : newCdf)
        {
            prob = (prob - newCdf.front().second) / newCdf.back().second;
        }

        // 4. set flowType for this flow
        m_stats->appFlowType = std::to_string(m_partofCdfPrio) + " prio";
    }
    else
    {
        newCdf = cdf;
    }

    // Set the CDF and calculate the mean
    double res = 0.;
    auto [ls, lp] = newCdf[0];
    for (auto [sz, prob] : newCdf)
    {
        m_flowSizeRng->CDF(sz, prob);
        res += (sz + ls) / 2.0 * (prob - lp);
        ls = sz;
        lp = prob;
    }
    if (m_trafficPattern == TrafficPattern::CDF || m_trafficPattern == TrafficPattern::COFLOW_CDF)
    {
        m_trafficSize = res;
    }

    // print
    // if (m_isPartofCdf)
    // {
    //     std::clog << "Node: " << m_nodeIndex << " prio: " << m_partofCdfPrio
    //               << " startSize: " << partofCdfStartSize << " endSize: " << partofCdfEndSize
    //               << std::endl;
    //     std::clog << "new cdf: " << std::endl;
    //     for (auto [sz, prob] : newCdf)
    //     {
    //         std::clog << sz << " " << prob << " ";
    //     }

    //     std::clog << std::endl
    //               << "new trafficLoad: " << m_trafficLoad << std::endl
    //               << "==============================\n";
    // }
}

inline Time
DcbTrafficGenApplication::GetNextFlowArriveInterval() const
{
    if (m_isFixedFlowArrive)
    {
        return m_fixedFlowArriveInterval;
    }
    else if (m_flowArriveTimeRng != nullptr)
    {
        Time t = Time(NanoSeconds(m_flowArriveTimeRng->GetInteger()));
        // std::clog << "Node" << m_nodeIndex << " " << m_stats->appFlowType << " interval "
        //           << t.GetNanoSeconds() << std::endl;
        return t;
    }
    else
    {
        NS_FATAL_ERROR("Flow arrival interval is not set");
    }
}

inline uint32_t
DcbTrafficGenApplication::GetNextFlowSize() 
{
    switch (m_trafficPattern)
    {
    case TrafficPattern::CDF:
    case TrafficPattern::COFLOW_CDF:
        return m_flowSizeRng->GetInteger();
    case TrafficPattern::ALL2ALLV:{
        auto it = m_destCnter.begin();
        uint32_t sz = it->second;
        m_destCnter.erase(it);
        return sz * 7000;
    }
    default:
        return m_trafficSize;
    }
}

void
DcbTrafficGenApplication::SetFlowType(std::string flowType)
{
    m_stats->appFlowType = flowType;
}

void
DcbTrafficGenApplication::FlowCompletes(Ptr<UdpBasedSocket> socket)
{
    // Call the base class method
    DcbBaseApplication::FlowCompletes(socket);

    // If the trafficType is BACKGROUND, we need to turn a false to true in m_bgFlowFinished
    if (m_trafficType == TrafficType::BACKGROUND)
    {
        // Find the first false
        for (uint32_t i = 0; i < m_bgFlowFinished.size(); i++)
        {
            if (!m_bgFlowFinished[i])
            {
                m_bgFlowFinished[i] = true;
                break;
            }
        }
    }
    Ptr<RoCEv2Socket> roceSocket = DynamicCast<RoCEv2Socket>(socket);
    if (roceSocket != nullptr)
    {
        Ipv4Address srcAddr = roceSocket->GetLocalAddress();
        Ipv4Address dstAddr = roceSocket->GetPeerAddress();
        uint32_t srcQP = roceSocket->GetSrcPort();
        uint32_t dstQP = roceSocket->GetDstPort();
        auto flowIdentifier = FlowIdentifier(srcAddr, dstAddr, srcQP, dstQP);
        if (m_trafficPattern == TrafficPattern::COFLOW_CDF)
        {
            m_flows[socket]->flowTag =
                m_stats->appFlowType + " " + std::to_string(m_coflowCdfIndexMap[flowIdentifier]);
            // m_stats->mFlowStats[flowIdentifier]->flowTag =
            //     m_stats->appFlowType + " " + std::to_string(m_coflowCdfIndexMap[flowIdentifier]);
        }
    }
}

DcbTrafficGenApplication::Stats::Stats(Ptr<DcbTrafficGenApplication> app)
    : DcbBaseApplication::Stats(app),
      m_app(app),
      isCollected(false)
{
}

std::shared_ptr<DcbBaseApplication::Stats>
DcbTrafficGenApplication::GetStats() const
{
    m_stats->CollectAndCheck(m_flows);
    if (m_stats->appFlowType.empty())
    {
        switch (m_trafficType)
        {
        case TrafficType::FOREGROUND:
            m_stats->appFlowType = "foreground";
            break;
        case TrafficType::BACKGROUND:
            m_stats->appFlowType = "background";
            break;
        default:
            m_stats->appFlowType = "none";
            break;
        }
    }
    return m_stats;
}

void
DcbTrafficGenApplication::Stats::CollectAndCheck(std::map<Ptr<Socket>, Flow*> flows)
{
    // Avoid collecting stats twice
    if (isCollected)
    {
        return;
    }
    isCollected = true;

    // Collect the statistics
    DcbBaseApplication::Stats::CollectAndCheck(flows);

    for (auto& [socket, flow] : flows)
    {
        // Collect the flow statistics
        mFlowStats[flow->flowIdentifier]->flowTag = flow->flowTag;
    }
}

TypeId
ParallelismTag::GetTypeId()
{
    static TypeId tid =
        TypeId("ns3::ParallelismTag").SetParent<Tag>().AddConstructor<ParallelismTag>();
    return tid;
}

TypeId
ParallelismTag::GetInstanceTypeId() const
{
    return GetTypeId();
}

uint32_t
ParallelismTag::GetSerializedSize() const
{
    return 4;
}

void
ParallelismTag::Serialize(TagBuffer buf) const
{
    buf.WriteU32(m_commType);
}

void
ParallelismTag::Deserialize(TagBuffer buf)
{
    m_commType = buf.ReadU32();
}

void
ParallelismTag::Print(std::ostream& os) const
{
    os << "ParallelismTag: " << m_commType;
}

void
ParallelismTag::SetParallelismType(uint32_t type)
{
    m_commType = type;
}

uint32_t
ParallelismTag::GetParallelismType() const
{
    return m_commType;
}

} // namespace ns3
