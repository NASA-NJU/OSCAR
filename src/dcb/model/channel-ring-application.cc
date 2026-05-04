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
 */

#include "channel-ring-application.h"

#include "dcb-net-device.h"
#include "rocev2-dcqcn.h"
#include "rocev2-l4-protocol.h"
#include "rocev2-socket.h"
#include "udp-based-socket.h"

#include "ns3/boolean.h"
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
#include "ns3/ptr.h"
#include "ns3/simulator.h"
#include "ns3/socket.h"
#include "ns3/string.h"
#include "ns3/tcp-socket-factory.h"
#include "ns3/type-id.h"
#include "ns3/udp-l4-protocol.h"
#include "ns3/udp-socket-factory.h"
#include "ns3/udp-socket.h"
#include "ns3/uinteger.h"

#include <bitset>
#include <cmath>

namespace ns3
{

NS_LOG_COMPONENT_DEFINE("ChannelRingApplication");
NS_OBJECT_ENSURE_REGISTERED(ChannelRingApplication);

std::map<uint8_t, std::vector<uint8_t>> ChannelRingApplication::m_channelSeqences;

TypeId
ChannelRingApplication::GetTypeId()
{
    static TypeId tid =
        TypeId("ns3::ChannelRingApplication")
            .SetParent<DcbBaseApplication>()
            .SetGroupName("Dcb")
            .AddConstructor<ChannelRingApplication>()
            .AddAttribute("TrafficPattern",
                          "The traffic pattern",
                          EnumValue(ChannelRingApplication::TrafficPattern::CHANNEL_RING),
                          MakeEnumAccessor(&ChannelRingApplication::m_trafficPattern),
                          MakeEnumChecker(ChannelRingApplication::TrafficPattern::CHANNEL_RING,
                                          "ChannelRing",
                                          ChannelRingApplication::TrafficPattern::CHANNEL_RING_OPT,
                                          "ChannelRingOpt"))
            .AddAttribute("TrafficSizeBytes",
                          "The size of the traffic, average size of CDF if it is use",
                          UintegerValue(0),
                          MakeUintegerAccessor(&ChannelRingApplication::m_trafficSize),
                          MakeUintegerChecker<uint32_t>());
    return tid;
}

ChannelRingApplication::ChannelRingApplication()
    : DcbBaseApplication()
{
    NS_LOG_FUNCTION(this);

    m_stats = std::make_shared<Stats>(this);
}

ChannelRingApplication::ChannelRingApplication(Ptr<DcTopology> topology, uint32_t nodeIndex)
    : DcbBaseApplication()
{
    NS_LOG_FUNCTION(this);

    m_stats = std::make_shared<Stats>(this);
}

void
ChannelRingApplication::InitMembers()
{
    NS_LOG_FUNCTION(this);
}

ChannelRingApplication::~ChannelRingApplication()
{
    NS_LOG_FUNCTION(this);
}

void
ChannelRingApplication::CalcTrafficParameters()
{
    CreateChannelRing();
}

void
ChannelRingApplication::GenerateTraffic()
{
    // Sanity check

    switch (m_trafficPattern)
    {
    case TrafficPattern::CHANNEL_RING:
    case TrafficPattern::CHANNEL_RING_OPT:
        for (uint8_t i = 0; i < m_numChannels; i++)
        {
            ChannelRingTag tag(i, static_cast<uint8_t>(m_nodeIndex));
            tag.SetPassed(m_nodeIndex);
            // tag.SetDest(m_channelNextNode[i]);
            tag.SetDest(m_nodeIndex);
            ScheduleNextFlow(tag);
        }
        break;
    default:
        NS_FATAL_ERROR("Traffic pattern can not be recognized.");
        break;
    }
}

void
ChannelRingApplication::ScheduleNextFlow(ChannelRingTag tag)
{
    Ptr<Socket> socket;

    uint8_t channel = tag.GetChannel();
    // Set the dest to the next node
    uint8_t nextNode = m_channelNextNode[channel];
    uint32_t destNode = 0;
    switch (m_trafficPattern)
    {
    case TrafficPattern::CHANNEL_RING:
        if (m_nodeIndex == tag.GetDest())
        {
            // The dest node is local node, forward to the next node in the channel
            tag.SetDest(nextNode);

            // Check if the dest node is in the same server and set the dest node
            if (nextNode / m_numChannels != m_nodeIndex / m_numChannels)
            {
                // Not in the same server, forward to the node whose id equals to the channel in the
                // local server
                destNode = (m_nodeIndex / m_numChannels) * m_numChannels + channel;
            }
            else
            {
                destNode = nextNode;
            }
        }
        else
        {
            // The dest node is not local node, just forward to the dest node in the tag
            destNode = tag.GetDest();
        }
        break;
    case TrafficPattern::CHANNEL_RING_OPT:
        destNode = nextNode;
        tag.SetDest(nextNode);
        break;
    default:
        NS_FATAL_ERROR("Traffic pattern can not be recognized.");
        break;
    }

    socket = CreateNewSocket(destNode);

    if (m_nodeIndex == 4 && destNode == 13)
    {
        NS_LOG_DEBUG("Create a new flow from " << m_nodeIndex << " to " << destNode);
    }

    uint64_t size = m_trafficSize;

    // XXX If use dest addr, the dest node is not set
    Flow* flow = new Flow(size, Simulator::Now(), destNode, socket, tag);
    SetFlowIdentifier(flow, socket);
    m_flows.emplace(socket, flow); // used when flow completes
    Simulator::Schedule(NanoSeconds(0),
                        &ChannelRingApplication::SendNextPacketWithTags,
                        this,
                        flow,
                        std::vector<std::shared_ptr<Tag>>{std::make_shared<ChannelRingTag>(tag)});
}

void
ChannelRingApplication::GenerateChannelRingSequence()
{
    // Create the channel sequences
    for (uint8_t i = 0; i < m_numChannels; i++)
    {
        std::vector<uint8_t> seq;
        // for each server, there is m_numChannels nodes
        for (uint8_t j = 0; j < m_numChunks / m_numChannels; j++)
        {
            // for each channel, the sequence begins from i
            for (uint8_t k = i; k < i + m_numChannels; k++)
            {
                seq.push_back(j * m_numChannels + (k % m_numChannels));
            }
        }
        m_channelSeqences[i] = seq;
    }

    // Log the sequences
    for (uint8_t i = 0; i < m_numChannels; i++)
    {
        std::stringstream ss;
        ss << "Channel " << static_cast<uint32_t>(i) << " sequence: ";
        for (auto node : m_channelSeqences[i])
        {
            ss << static_cast<uint32_t>(node) << " ";
        }
        NS_LOG_DEBUG(ss.str());
    }
}

void
ChannelRingApplication::GenerateChannelRingOptSequence()
{
    // Create the channel sequences in pair
    for (uint8_t i = 0; i < m_numChannels / 2; i++)
    {
        std::vector<uint8_t> seq0;
        std::vector<uint8_t> seq1;

        // Basic sequences
        std::vector<uint8_t> basicSeq0;
        std::vector<uint8_t> basicSeq1;
        uint8_t start = i * 2;
        uint8_t end = i * 2 + 1;
        // Construct the first sequence
        basicSeq0.push_back(start);
        for (uint8_t nodeIdx = 0; nodeIdx < m_numChannels; nodeIdx++)
        {
            if (nodeIdx != start && nodeIdx != end)
            {
                basicSeq0.push_back(nodeIdx);
            }
        }
        basicSeq0.push_back(end);
        // The second sequence is the reverse of the first sequence
        basicSeq1 = basicSeq0;
        std::reverse(basicSeq1.begin(), basicSeq1.end());

        // for each server, there is m_numChannels nodes
        // the sequence of each server is interleaved with the basic sequence plus j * m_numChannels
        for (uint8_t j = 0; j < m_numChunks / m_numChannels; j++)
        {
            if (j % 2 == 0)
            {
                for (uint8_t k = 0; k < m_numChannels; k++)
                {
                    seq0.push_back(j * m_numChannels + basicSeq0[k]);
                    seq1.push_back(j * m_numChannels + basicSeq1[k]);
                }
            }
            else
            {
                for (uint8_t k = 0; k < m_numChannels; k++)
                {
                    seq0.push_back(j * m_numChannels + basicSeq1[k]);
                    seq1.push_back(j * m_numChannels + basicSeq0[k]);
                }
            }
        }
        m_channelSeqences[i * 2] = seq0;
        m_channelSeqences[i * 2 + 1] = seq1;
    }

    // Log the sequences
    for (uint8_t i = 0; i < m_numChannels; i++)
    {
        std::stringstream ss;
        ss << "Channel " << static_cast<uint32_t>(i) << " sequence: ";
        for (auto node : m_channelSeqences[i])
        {
            ss << static_cast<uint32_t>(node) << " ";
        }
        NS_LOG_DEBUG(ss.str());
    }
}

void
ChannelRingApplication::CreateChannelRing()
{
    // If m_channelSeqences is empty, then we need to create it
    if (m_channelSeqences.empty())
    {
        switch (m_trafficPattern)
        {
        case TrafficPattern::CHANNEL_RING:
            GenerateChannelRingSequence();
            break;
        case TrafficPattern::CHANNEL_RING_OPT:
            GenerateChannelRingOptSequence();
            break;
        default:
            NS_FATAL_ERROR("Traffic pattern can not be recognized.");
            break;
        }
    }

    // Set the previous node and the next node according to the sequence
    uint8_t local = m_nodeIndex;
    for (uint8_t i = 0; i < m_numChannels; i++)
    {
        // find local in the sequence and set the previous and next node
        auto localIt = std::find(m_channelSeqences[i].begin(), m_channelSeqences[i].end(), local);
        if (localIt != m_channelSeqences[i].end())
        {
            m_channelPrevNode[i] = localIt == m_channelSeqences[i].begin()
                                       ? m_channelSeqences[i].back()
                                       : *(localIt - 1);
            m_channelNextNode[i] = localIt == m_channelSeqences[i].end() - 1
                                       ? m_channelSeqences[i].front()
                                       : *(localIt + 1);
        }
        else
        {
            NS_FATAL_ERROR("Local node not found in the sequence");
        }
    }
}

void
ChannelRingApplication::HandleRead(Ptr<Socket> socket)
{
    NS_LOG_FUNCTION(this << socket);
    Ptr<Packet> packet;
    Address from;
    // Address localAddress;
    while ((packet = socket->RecvFrom(from)))
    {
        // Try to peek the ChannelRingTag
        ChannelRingTag tag;
        if (!packet->PeekPacketTag(tag))
        {
            NS_FATAL_ERROR("ChannelRingTag not found");
        }

        // Check if the chunk is passed the local node
        if (tag.IsPassed(m_nodeIndex))
        {
            // It is ok
        }

        // Record the received chunk
        m_recvedChannelRingChunks[tag] += packet->GetSize();

        if (m_recvedChannelRingChunks[tag] == m_trafficSize)
        {
            // If the chunk is passed all the nodes, then forward it

            // Get the from address
            // InetSocketAddress inetFrom = InetSocketAddress::ConvertFrom(from);
            // Ipv4Address fromAddr4 = inetFrom.GetIpv4();
            // uint32_t fromNode = m_topology->GetNodeIdxFormIp(fromAddr4);
            // if (fromNode == m_channelPrevNode[tag.GetChannel()])
            // {
            //     // The chunk is passed the local node
            //     tag.SetPassed(m_nodeIndex);
            // }
            if (tag.GetDest() == m_nodeIndex)
            {
                tag.SetPassed(m_nodeIndex);
                if (tag.AllPassed())
                {
                    std::stringstream ss;
                    tag.Print(ss);
                    // Get the from address
                    InetSocketAddress inetFrom = InetSocketAddress::ConvertFrom(from);
                    Ipv4Address fromAddr4 = inetFrom.GetIpv4();
                    uint32_t fromNode = m_topology->GetNodeIdxFormIp(fromAddr4);
                    NS_LOG_DEBUG("Finish a chunk whit tag: " << ss.str() << " at " << m_nodeIndex
                                                             << " from " << fromNode);
                    return;
                }
            }
            // Forward the chunk
            ScheduleNextFlow(tag);
        }
        else
        {
            std::stringstream ss;
            tag.Print(ss);
            // Get the from address
            InetSocketAddress inetFrom = InetSocketAddress::ConvertFrom(from);
            Ipv4Address fromAddr4 = inetFrom.GetIpv4();
            uint32_t fromNode = m_topology->GetNodeIdxFormIp(fromAddr4);
            NS_LOG_DEBUG("Tag: " << ss.str() << " at " << m_nodeIndex << " from " << fromNode);
        }
    }
}

ChannelRingApplication::Stats::Stats(Ptr<ChannelRingApplication> app)
    : DcbBaseApplication::Stats(app),
      isCollected(false)
{
}

std::shared_ptr<DcbBaseApplication::Stats>
ChannelRingApplication::GetStats() const
{
    std::dynamic_pointer_cast<Stats>(m_stats)->CollectAndCheck(m_flows);
    return m_stats;
}

void
ChannelRingApplication::Stats::CollectAndCheck(
    std::map<Ptr<Socket>, DcbBaseApplication::Flow*> flows)
{
    // Avoid collecting stats twice
    if (isCollected)
    {
        return;
    }
    isCollected = true;

    // Collect the statistics
    // Call the base class's CollectAndCheck
    DcbBaseApplication::Stats::CollectAndCheck(flows);
}

NS_OBJECT_ENSURE_REGISTERED(ChannelRingTag);

TypeId
ChannelRingTag::GetTypeId()
{
    static TypeId tid = TypeId("ns3::ChannelRingTag")
                            .SetParent<Tag>()
                            .SetGroupName("Dcb")
                            .AddConstructor<ChannelRingTag>();
    return tid;
}

TypeId
ChannelRingTag::GetInstanceTypeId() const
{
    return GetTypeId();
}

uint32_t
ChannelRingTag::GetSerializedSize() const
{
    NS_LOG_FUNCTION(this);
    return 1 + 1 + 1 + 4;
}

void
ChannelRingTag::Serialize(TagBuffer buf) const
{
    NS_LOG_FUNCTION(this << &buf);
    buf.WriteU8(m_channel);
    buf.WriteU8(m_chunk);
    buf.WriteU8(m_dest);
    buf.WriteU32(m_passed);
}

void
ChannelRingTag::Deserialize(TagBuffer buf)
{
    NS_LOG_FUNCTION(this << &buf);
    m_channel = buf.ReadU8();
    m_chunk = buf.ReadU8();
    m_dest = buf.ReadU8();
    m_passed = buf.ReadU32();
}

void
ChannelRingTag::Print(std::ostream& os) const
{
    NS_LOG_FUNCTION(this << &os);
    // Convert m_passed to onehot encoding string
    std::bitset<32> onehot(m_passed);

    os << "ChannelRingTag: channel=" << static_cast<uint32_t>(m_channel)
       << ", chunk=" << static_cast<uint32_t>(m_chunk) << ", dest=" << static_cast<uint32_t>(m_dest)
       << ", passed=" << onehot;
}

ChannelRingTag::ChannelRingTag()
    : Tag()
{
    NS_LOG_FUNCTION(this);
}

ChannelRingTag::ChannelRingTag(uint8_t channel, uint8_t chunk)
    : Tag(),
      m_channel(channel),
      m_chunk(chunk),
      m_dest(0xff),
      m_passed(0)
{
    NS_LOG_FUNCTION(this << channel << chunk);
}

uint8_t
ChannelRingTag::GetChannel() const
{
    return m_channel;
}

uint8_t
ChannelRingTag::GetChunk() const
{
    return m_chunk;
}

void
ChannelRingTag::SetDest(uint8_t dest)
{
    m_dest = dest;
}

uint8_t
ChannelRingTag::GetDest() const
{
    return m_dest;
}

void
ChannelRingTag::SetPassed(uint8_t passedNode)
{
    // Set the passed node to onehot encoded m_passed
    m_passed |= 1 << passedNode;
}

bool
ChannelRingTag::IsPassed(uint8_t node) const
{
    return m_passed & (1 << node);
}

bool
ChannelRingTag::AllPassed() const
{
    return m_passed == 0xffffffff;
}

bool
ChannelRingTag::operator<(const ChannelRingTag& other) const
{
    return m_channel < other.m_channel ||
           (m_channel == other.m_channel && m_chunk < other.m_chunk) ||
           (m_channel == other.m_channel && m_chunk == other.m_chunk && m_dest < other.m_dest) ||
           (m_channel == other.m_channel && m_chunk == other.m_chunk && m_dest == other.m_dest &&
            m_passed < other.m_passed);
}

} // namespace ns3
