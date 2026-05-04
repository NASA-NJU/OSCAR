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
 * Author: ChangLiu <liuchang_1307@163.com>
 */

#include "dcb-ppfc-traffic-control.h"

#include "dcb-flow-control-port.h"
#include "dcb-pfc-port.h"
#include "dcb-ppfc-mmu-queue.h"
#include "dcb-ppfc-port.h"
#include "dcb-traffic-control.h"
#include "pausable-queue-disc.h"
#include "ppfc-queue-disc-item.h"
#include "udp-based-l4-protocol.h"

#include "ns3/address.h"
#include "ns3/boolean.h"
#include "ns3/callback.h"
#include "ns3/ethernet-header.h"
#include "ns3/fatal-error.h"
#include "ns3/ipv4-header.h"
#include "ns3/ipv4-queue-disc-item.h"
#include "ns3/log-macros-enabled.h"
#include "ns3/log.h"
#include "ns3/net-device-queue-interface.h"
#include "ns3/nstime.h"
#include "ns3/pfc-frame.h"
#include "ns3/rocev2-header.h"
#include "ns3/simulator.h"
#include "ns3/socket.h"
#include "ns3/trace-source-accessor.h"
#include "ns3/type-id.h"
#include "ns3/udp-header.h"

#include <cmath>

namespace ns3
{

NS_LOG_COMPONENT_DEFINE("DcbPPfcTrafficControl");

NS_OBJECT_ENSURE_REGISTERED(DcbPPfcTrafficControl);

TypeId
DcbPPfcTrafficControl::GetTypeId(void)
{
    static TypeId tid = TypeId("ns3::DcbPPfcTrafficControl")
                            .SetParent<DcbTrafficControl>()
                            .SetGroupName("Dcb")
                            .AddConstructor<DcbPPfcTrafficControl>();
    return tid;
}

TypeId
DcbPPfcTrafficControl::GetInstanceTypeId(void) const
{
    return GetTypeId();
}

DcbPPfcTrafficControl::DcbPPfcTrafficControl()
    : DcbTrafficControl()
{
    NS_LOG_FUNCTION(this);
}

DcbPPfcTrafficControl::~DcbPPfcTrafficControl()
{
    NS_LOG_FUNCTION(this);
}

void
DcbPPfcTrafficControl::RegisterDeviceNumber(const uint32_t num)
{
    NS_LOG_FUNCTION(this << num);
    m_buffer.RegisterPortNumber(num);
}

void
DcbPPfcTrafficControl::Send(Ptr<NetDevice> device, Ptr<QueueDiscItem> item)
{
    NS_LOG_FUNCTION(this << device << item);

    NS_LOG_DEBUG("Send packet to device " << device << " protocol number " << item->GetProtocol());

    Ptr<Packet> pkt = item->GetPacket()->Copy();
    const Ipv4Header& ipHdr = DynamicCast<Ipv4QueueDiscItem>(item)->GetHeader();
    // Get inDev's priority and index from tag
    DeviceIndexTag devTag;
    CoSTag cosTag;

    uint32_t inPortIndex;
    uint8_t inQueuePriority;
    if (pkt->PeekPacketTag(cosTag))
    {
        inQueuePriority = cosTag.GetCoS();
    }
    else
    {
        inQueuePriority = Socket::IpTos2Priority(ipHdr.GetTos());
    }

    // Get outDev's priority and index from tag
    uint32_t outPortIndex = device->GetIfIndex();
    uint32_t outQueuePriority = inQueuePriority;

    // get the index of queue that the item should be in outPort.
    Ptr<Node> currNode = GetNode();

    // get the next hop node
    Ptr<Node> nxtNode = nullptr;
    Ptr<Channel> channel = device->GetChannel();
    Ptr<NetDevice> nxtDev;
    for (uint32_t i = 0; i < channel->GetNDevices(); i++)
    {
        nxtDev = channel->GetDevice(i);
        if (nxtDev != device)
        {
            nxtNode = nxtDev->GetNode();
            break;
        }
    }
    if (nxtNode == nullptr)
    {
        NS_LOG_ERROR("can't find the node linked to current node!");
    }
    // get the index of queue that the packet should be put in this node
    if (m_topology->IsHost(currNode->GetId()) == true)
    {
        outQueuePriority =
            m_topology->GetHostQueueIdx(currNode, device, nxtNode, pkt, ipHdr, inQueuePriority);
    }
    else if (m_topology->IsSwitch(currNode->GetId()) == true)
    {
        outQueuePriority =
            m_topology->GetSwitchQueueIdx(nxtNode, nxtDev, pkt, ipHdr, inQueuePriority);
    }

    // Check enqueue admission
    bool success;
    if (pkt->PeekPacketTag(devTag))
    {
        inPortIndex = devTag.GetIndex();
        success = m_buffer.InPacketProcess(inPortIndex,
                                           inQueuePriority,
                                           outPortIndex,
                                           outQueuePriority,
                                           pkt->GetSize());
    }
    else
    {
        success = m_buffer.InPacketProcess(outPortIndex, outQueuePriority, pkt->GetSize());
    }

    if (!success)
    {
        m_bufferOverflowTrace(pkt);
        return;
    }

    Ptr<Ipv4QueueDiscItem> ipv4Item = DynamicCast<Ipv4QueueDiscItem>(item);
    Ptr<PPfcQueueDiscItem> ppfcItem = Create<PPfcQueueDiscItem>(ipv4Item->GetPacket(),
                                                                ipv4Item->GetAddress(),
                                                                ipv4Item->GetProtocol(),
                                                                ipv4Item->GetHeader(),
                                                                outQueuePriority);

    // after enqueue the packet, do per-port-fc check on egress port.
    EnqueueEgressProcess(device->GetIfIndex(), outQueuePriority);

    TrafficControlLayer::Send(device, ppfcItem);
}

void
DcbPPfcTrafficControl::EgressProcess(uint32_t outPort, uint32_t qIdx, Ptr<Packet> packet)
{
    NS_LOG_FUNCTION(this << outPort << qIdx << packet);
    DeviceIndexTag devTag; // egress will remove the tag
    CoSTag cosTag;
    uint32_t inPortIndex;
    uint8_t inQueuePriority;
    bool hasDevTag = false;
    bool hasCosTag = false;
    if (packet->RemovePacketTag(devTag))
    {
        inPortIndex = devTag.GetIndex();
        hasDevTag = true;
    }
    if (packet->RemovePacketTag(cosTag))
    {
        inQueuePriority = cosTag.GetCoS() & 0x0f;
        hasCosTag = true;
    }

    // liuchangTODO: be careful with the priority (if affected by the ppfc rechoose the queue)
    if (hasDevTag && hasCosTag)
    {
        m_buffer.OutPacketProcess(inPortIndex, inQueuePriority, outPort, qIdx, packet->GetSize());
    }
    else
    {
        m_buffer.OutPacketProcess(outPort, qIdx, packet->GetSize());
    }

    // when a packet dequeue from egress port, do check if this port uncongested and should send
    // resume to upstreams.
    DequeueEgressProcess(outPort, qIdx);
}

void
DcbPPfcTrafficControl::InstallFCToPort(
    uint32_t portIdx,
    Ptr<DcbFlowControlPort> fc,
    std::vector<ns3::Ptr<ns3::DcbFlowControlMmuQueue>> fcMmuQueues)
{
    NS_LOG_FUNCTION(this << portIdx);
    m_buffer.GetPort(portIdx).SetFC(fc, fcMmuQueues);
    m_buffer.SetPortFcMmuBufferCallback(portIdx);
}

void
DcbPPfcTrafficControl::EnqueueEgressProcess(uint32_t port, uint32_t qIdx)
{
    NS_LOG_FUNCTION(this << port << qIdx);
    if (m_topology->IsHost(GetNode()->GetId()))
    {
        return;
    }
    PortInfo& info = m_buffer.GetPort(port);
    if (info.FcEnabled())
    {
        uint32_t cate = GetCategory(port, qIdx);
        Ptr<DcbPPfcPort> fcPort = DynamicCast<DcbPPfcPort>(info.GetFC());
        if (cate != DcTopology::SwicthFCCateory::Highest &&
            fcPort->CheckShouldSendPause(cate) == true)
        {
            fcPort->SetUpstreamPaused(cate, true);
            uint32_t pauseQIdx = port - 1;
            // liuchangTODO: all ports' (except this port) fc sendPFCPause to upstreams.
            for (uint32_t i = 1; i < m_buffer.GetPorts().size(); i++)
            {
                if (i == port)
                {
                    continue;
                }
                PortInfo& portInfo = m_buffer.GetPort(i);
                Ptr<DcbPPfcPort> fc = DynamicCast<DcbPPfcPort>(portInfo.GetFC());
                fc->DoSendPause(cate, pauseQIdx, Address(), 0);
            }
        }
    }
}

void
DcbPPfcTrafficControl::DequeueEgressProcess(uint32_t port, uint32_t qIdx)
{
    NS_LOG_FUNCTION(this << port << qIdx);
    if (m_topology->IsHost(GetNode()->GetId()))
    {
        return;
    }
    PortInfo& info = m_buffer.GetPort(port);
    if (info.FcEnabled())
    {
        uint32_t cate = GetCategory(port, qIdx);
        Ptr<DcbPPfcPort> fcPort = DynamicCast<DcbPPfcPort>(info.GetFC());
        if (cate != DcTopology::SwicthFCCateory::Highest &&
            fcPort->CheckShouldSendResume(cate) == true)
        {
            fcPort->SetUpstreamPaused(cate, false);
            uint32_t reseumeQIdx = port - 1;
            // all ports' (except this port) fc sendPFCPause to upstreams.
            for (uint32_t i = 1; i < m_buffer.GetPorts().size(); i++)
            {
                if (i == port)
                {
                    continue;
                }
                PortInfo& portInfo = m_buffer.GetPort(i);
                Ptr<DcbPPfcPort> fc = DynamicCast<DcbPPfcPort>(portInfo.GetFC());
                fc->DoSendPause(cate, reseumeQIdx, Address(), 1);
            }
        }
    }
}

uint32_t
DcbPPfcTrafficControl::GetCategory(uint32_t portIdx, uint32_t qIdx)
{
    NS_LOG_FUNCTION(this << portIdx << qIdx);
    Ptr<Node> currNode = GetNode();
    uint32_t swPortNum = m_topology->switches_begin()->nodePtr->GetNDevices();
    NS_ASSERT_MSG(m_topology->IsSwitch(currNode->GetId()), "ONLY SWITCH NEED TO CALL THIS FUN!");

    if (qIdx <= swPortNum - 2)
    {
        return DcTopology::SwicthFCCateory::NeedRelay;
    }
    else if (qIdx == swPortNum - 1)
    {
        return DcTopology::SwicthFCCateory::NxtToDst;
    }
    else if (qIdx == swPortNum)
    {
        return DcTopology::SwicthFCCateory::Highest;
    }
    else
    {
        NS_ASSERT_MSG(false, "Wrong category in switch!");
        // suppress warning
        return UINT32_MAX;
    }
}

void
DcbPPfcTrafficControl::SetTopology(Ptr<DcTopology> topo)
{
    NS_LOG_FUNCTION(this << topo);
    m_topology = topo;
}

Ptr<DcTopology>
DcbPPfcTrafficControl::GetTopology() const
{
    NS_LOG_FUNCTION(this);
    return m_topology;
}

uint32_t
DcbPPfcTrafficControl::GetEgressPriority(const uint32_t devIdx,
                                         const Ipv4Address& srcIp,
                                         const Ipv4Address& dstIp,
                                         const uint32_t srcPort,
                                         const uint32_t dstPort,
                                         const uint32_t prio)
{
    NS_LOG_FUNCTION(this);
    Ptr<Node> currNode = GetNode();
    NS_ASSERT(
        m_topology->IsHost(currNode->GetId())); // only called by rocev2-socket in host for now
    Ptr<Packet> pkt = Create<Packet>();

    RoCEv2Header rocev2Header;
    rocev2Header.SetSrcQP(srcPort);
    rocev2Header.SetDestQP(dstPort);
    UdpHeader udpHeader;
    udpHeader.SetSourcePort(UdpBasedL4Protocol::PROT_NUMBER);
    udpHeader.SetDestinationPort(UdpBasedL4Protocol::PROT_NUMBER);
    pkt->AddHeader(rocev2Header);
    pkt->AddHeader(udpHeader);

    Ipv4Header ipv4Header;
    ipv4Header.SetSource(srcIp);
    ipv4Header.SetDestination(dstIp);
    ipv4Header.SetProtocol(UdpL4Protocol::PROT_NUMBER);
    Ptr<NetDevice> currDev = currNode->GetDevice(devIdx);
    Ptr<Channel> channel = currDev->GetChannel();
    Ptr<NetDevice> nxtDev = nullptr;
    for (uint32_t i = 0; i < channel->GetNDevices(); i++)
    {
        if (channel->GetDevice(i) != currDev)
        {
            nxtDev = channel->GetDevice(i);
        }
    }
    NS_ASSERT(nxtDev != nullptr);
    Ptr<Node> nxtNode = nxtDev->GetNode();
    uint32_t outQueuePriority =
        m_topology->GetHostQueueIdx(currNode, currDev, nxtNode, pkt, ipv4Header, prio);
    return outQueuePriority;
}

void
DcbPPfcTrafficControl::RelayPFC(uint32_t devIdx,
                                uint32_t cate,
                                uint32_t pauseQIdx,
                                const Address& from,
                                uint32_t type)
{
    NS_LOG_FUNCTION(this << devIdx << cate << pauseQIdx << from << type);
    NS_LOG_DEBUG("Relay PFC to upstream switch");
    NS_ASSERT(cate == DcTopology::SwicthFCCateory::NxtToDst); // only this category pfc need to be
                                                              // realyed to upstream.
    PortInfo& portInfo = m_buffer.GetPort(devIdx);
    Ptr<DcbPPfcPort> fcPort = DynamicCast<DcbPPfcPort>(portInfo.GetFC());
    fcPort->DoSendRelayPause(cate, pauseQIdx, from, type);
}

int
DcbPPfcTrafficControl::CompareEgressQueueLength(uint32_t portIdx, uint32_t cate, uint32_t th)
{
    NS_LOG_FUNCTION(this << portIdx << cate << th);
    Ptr<Node> currNode = GetNode();
    uint32_t swPortNum = m_topology->switches_begin()->nodePtr->GetNDevices();
    NS_ASSERT_MSG(m_topology->IsSwitch(currNode->GetId()), "ONLY SWITCH NEED TO CALL THIS FUN!");
    const auto& port = m_buffer.GetPort(portIdx);
    // get the queue(s) length of cate
    uint32_t qLenSum = 0;
    if (cate == DcTopology::SwicthFCCateory::NeedRelay)
    {
        for (uint32_t i = 0; i <= swPortNum - 2; i++)
        {
            Ptr<DcbPPfcMmuQueue> mmuQ = DynamicCast<DcbPPfcMmuQueue>(port.GetFCMmuQueue(i));
            qLenSum += mmuQ->GetQueueLength();
        }
    }
    else if (cate == DcTopology::SwicthFCCateory::NxtToDst)
    {
        Ptr<DcbPPfcMmuQueue> mmuQ = DynamicCast<DcbPPfcMmuQueue>(port.GetFCMmuQueue(swPortNum - 1));
        qLenSum = mmuQ->GetQueueLength();
    }
    else if (cate == DcTopology::SwicthFCCateory::Highest)
    {
        NS_ASSERT_MSG(false, "should not to check the highest priority Q!");
    }
    else
    {
        NS_ASSERT_MSG(false, "wrong category of this check!");
    }

    if (qLenSum < th)
    {
        return -1;
    }
    else if (qLenSum == th)
    {
        return 0;
    }
    else
    {
        return 1;
    }
}

DcbPPfcTrafficControl::PortInfo::PortInfo()
    : m_fcEnabled(false),
      m_fc(nullptr)
{
}

DcbPPfcTrafficControl::Buffer::Buffer()
    : m_totalSize(32 * 1024 * 1024)
{
}

void
DcbPPfcTrafficControl::Buffer::SetBufferSpace(uint32_t bytes)
{
    NS_LOG_FUNCTION(this << bytes);

    m_totalSize = bytes;
}

void
DcbPPfcTrafficControl::Buffer::RegisterPortNumber(const uint32_t num)
{
    NS_LOG_FUNCTION(this << num);

    m_ports.resize(num);
}

bool
DcbPPfcTrafficControl::Buffer::InPacketProcess(uint32_t inPortIndex,
                                               uint32_t inQueuePriority,
                                               uint32_t outPortIndex,
                                               uint32_t outQueuePriority,
                                               uint32_t packetSize)
{
    Ptr<DcbFlowControlMmuQueue> inQueue = m_ports[inPortIndex].GetFCMmuQueue(inQueuePriority);
    Ptr<DcbFlowControlMmuQueue> outQueue = m_ports[outPortIndex].GetFCMmuQueue(outQueuePriority);
    bool success =
        inQueue->CheckIngressAdmission(packetSize) & outQueue->CheckEgressAdmission(packetSize);
    if (success)
    {
        inQueue->IngressIncrement(packetSize);
        outQueue->EgressIncrement(packetSize);
        return true;
    }
    NS_LOG_DEBUG("Buffer overflow, packet drop.");
    return false; // buffer overflow
}

bool
DcbPPfcTrafficControl::Buffer::InPacketProcess(uint32_t outPortIndex,
                                               uint32_t outQueuePriority,
                                               uint32_t packetSize)
{
    Ptr<DcbFlowControlMmuQueue> outQueue = m_ports[outPortIndex].GetFCMmuQueue(outQueuePriority);
    bool success = outQueue->CheckEgressAdmission(packetSize);
    if (success)
    {
        outQueue->EgressIncrement(packetSize);
        return true;
    }
    NS_LOG_DEBUG("Buffer overflow, packet drop.");
    return false; // buffer overflow
}

void
DcbPPfcTrafficControl::Buffer::OutPacketProcess(uint32_t inPortIndex,
                                                uint32_t inQueuePriority,
                                                uint32_t outPortIndex,
                                                uint32_t outQueuePriority,
                                                uint32_t packetSize)
{
    Ptr<DcbFlowControlMmuQueue> inQueue = m_ports[inPortIndex].GetFCMmuQueue(inQueuePriority);
    Ptr<DcbFlowControlMmuQueue> outQueue = m_ports[outPortIndex].GetFCMmuQueue(outQueuePriority);
    inQueue->IngressDecrement(packetSize);
    outQueue->EgressDecrement(packetSize);
}

void
DcbPPfcTrafficControl::Buffer::OutPacketProcess(uint32_t outPortIndex,
                                                uint32_t outQueuePriority,
                                                uint32_t packetSize)
{
    Ptr<DcbFlowControlMmuQueue> outQueue = m_ports[outPortIndex].GetFCMmuQueue(outQueuePriority);
    outQueue->EgressDecrement(packetSize);
}

uint32_t
DcbPPfcTrafficControl::Buffer::GetSharedSize()
{
    uint32_t size = m_totalSize;
    for (uint32_t i = 1; i < m_ports.size(); i++) // 0 is LoopbackNetDev
    {
        const auto& port = m_ports[i];
        uint32_t qNum = port.GetFCMmuQueueSize();
        for (uint32_t j = 0; j < qNum; j++)
        {
            uint32_t exclusiveSize = m_ports[i].GetFCMmuQueue(j)->GetExclusiveBufferSize();
            NS_ASSERT_MSG(exclusiveSize <= size, "Exclusive buffer size is larger than total size");
            size -= exclusiveSize;
        }
    }
    return size;
}

uint32_t
DcbPPfcTrafficControl::Buffer::GetSharedUsed()
{
    uint32_t sum = 0;
    for (uint32_t i = 1; i < m_ports.size(); i++) // 0 is LoopbackNetDev
    {
        const auto& port = m_ports[i];
        uint32_t qNum = port.GetFCMmuQueueSize();
        for (uint32_t j = 0; j < qNum; j++)
        {
            sum += m_ports[i].GetFCMmuQueue(j)->GetExclusiveSharedBufferUsed();
        }
    }
    return sum;
}

uint32_t
DcbPPfcTrafficControl::Buffer::GetBufferUsed()
{
    uint32_t used = 0;
    for (uint32_t i = 1; i < m_ports.size(); i++) // 0 is LoopbackNetDev
    {
        const auto& port = m_ports[i];
        uint32_t qNum = port.GetFCMmuQueueSize();
        for (uint32_t j = 0; j < qNum; j++)
        {
            uint32_t qLen =
                DynamicCast<DcbPPfcMmuQueue>(m_ports[i].GetFCMmuQueue(j))->GetQueueLength();
            used += qLen;
        }
    }
    return used;
}

void
DcbPPfcTrafficControl::Buffer::SetPortFcMmuBufferCallback(uint32_t portIndex)
{
    const auto& port = m_ports[portIndex];
    uint32_t qNum = port.GetFCMmuQueueSize();
    for (uint32_t i = 0; i < qNum; i++)
    {
        Ptr<DcbFlowControlMmuQueue> queue = port.GetFCMmuQueue(i);
        queue->SetBufferCallback(MakeCallback(&DcbPPfcTrafficControl::Buffer::GetSharedSize, this),
                                 MakeCallback(&DcbPPfcTrafficControl::Buffer::GetSharedUsed, this));
    }
}
} // namespace ns3
