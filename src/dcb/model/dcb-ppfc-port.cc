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

#include "dcb-ppfc-port.h"

#include "dcb-channel.h"
#include "dcb-flow-control-port.h"
#include "dcb-ppfc-traffic-control.h"
#include "dcb-traffic-control.h"

#include "ns3/dc-topology.h"
#include "ns3/ppfc-frame.h"
#include "ns3/simulator.h"

namespace ns3
{

NS_LOG_COMPONENT_DEFINE("DcbPPfcPort");

NS_OBJECT_ENSURE_REGISTERED(DcbPPfcPort);

TypeId
DcbPPfcPort::GetTypeId()
{
    static TypeId tid =
        TypeId("ns3::DcbPPfcPort")
            .SetParent<DcbFlowControlPort>()
            .SetGroupName("Dcb")
            .AddTraceSource("PfcSent",
                            "PFC pause frame sent",
                            MakeTraceSourceAccessor(&DcbPPfcPort::m_tracePfcSent),
                            "ns3::Packet::TracedCallback")
            .AddTraceSource("PfcReceived",
                            "PFC pause frame received",
                            MakeTraceSourceAccessor(&DcbPPfcPort::m_tracePfcReceived),
                            "ns3::Packet::TracedCallback");
    return tid;
}

DcbPPfcPort::DcbPPfcPort(Ptr<NetDevice> dev, Ptr<DcbTrafficControl> tc)
    : DcbFlowControlPort(dev, tc),
      m_egressPort(dev->GetIfIndex())
{
    SetFcIngressEnabled(false);
    SetFcEgressEnabled(true);
    NS_LOG_FUNCTION(this);
}

DcbPPfcPort::~DcbPPfcPort()
{
    NS_LOG_FUNCTION(this);
}

void
DcbPPfcPort::DoIngressProcess(Ptr<NetDevice> outDev, Ptr<QueueDiscItem> item)
{
    NS_ASSERT_MSG(false, "shouldn't call this func!");
}

void
DcbPPfcPort::DoPacketOutCallbackProcess(uint32_t priority, Ptr<Packet> packet)
{
    NS_ASSERT_MSG(false, "shouldn't call this func!");
}

void DcbPPfcPort::DoEgressProcess(Ptr<Packet> packet)
{
}

// liuchangTODO: should be called by tc, after checking congested.
void
DcbPPfcPort::DoSendPause(uint32_t cate, uint32_t pauseQIdx, const Address& from, uint32_t type)
{
    NS_LOG_DEBUG("PFC: Send pause frame from node " << Simulator::GetContext() << " port "
                                                    << m_dev->GetIfIndex() << " qIdx " << pauseQIdx
                                                    << " type " << type);
    Ptr<Packet> ppfcFrame = PPfcFrame::GeneratePauseFrame(pauseQIdx, type, cate);
    // pause frames are sent directly to device without queueing in egress QueueDisc
    m_dev->Send(ppfcFrame, from, PPfcFrame::PROT_NUMBER);
    bool isPause = type == 0 ? true : false;
    // SetUpstreamPaused(cate, isPause); // mark this category as paused
    m_tracePfcSent(GetNodeAndPortId(), pauseQIdx, isPause);
}

void
DcbPPfcPort::DoSendRelayPause(uint32_t cate, uint32_t pauseQIdx, const Address& from, uint32_t type)
{
    NS_LOG_DEBUG("Realy PFC: Send pause frame from node " << Simulator::GetContext() << " port "
                                                          << m_dev->GetIfIndex() << " qIdx "
                                                          << pauseQIdx << " type " << type);
    Ptr<Packet> ppfcFrame = PPfcFrame::GeneratePauseFrame(pauseQIdx, type, cate);
    // pause frames are sent directly to device without queueing in egress QueueDisc
    m_dev->Send(ppfcFrame, from, PPfcFrame::PROT_NUMBER);
    bool isPause = type == 0 ? true : false;
    m_tracePfcSent(GetNodeAndPortId(), pauseQIdx, isPause);
}

void
DcbPPfcPort::ReceivePfc(Ptr<NetDevice> dev,
                        Ptr<const Packet> packet,
                        uint16_t protocol,
                        const Address& from,
                        const Address& to,
                        NetDevice::PacketType packetType)
{
    NS_LOG_FUNCTION(this << dev << protocol << from << to);

    Ptr<DcbNetDevice> device = DynamicCast<DcbNetDevice>(dev);
    const uint32_t index = device->GetIfIndex();
    PPfcFrame ppfcFrame;
    packet->PeekHeader(ppfcFrame);
    
    uint32_t qIdx = ppfcFrame.GetQIdx();
    uint32_t type = ppfcFrame.GetType();
    uint32_t cate = ppfcFrame.GetCategory();
    bool isPause = type == 0 ? true : false;
    // get the qDis in this node.
    Ptr<PausableQueueDisc> qDisc = device->GetQueueDisc();
    // the reaction in host and switch is different
    Ptr<DcbPPfcTrafficControl> tc = DynamicCast<DcbPPfcTrafficControl>(m_tc);
    Ptr<DcTopology> topo = tc->GetTopology();
    // when a host receive a pause/resume, may be need to recaculate the qidx
    if (topo->IsHost(dev->GetNode()->GetId()))
    {
        if (cate == DcTopology::SwicthFCCateory::NxtToDst)
        {
            uint32_t swPortNum = topo->switches_begin()->nodePtr->GetNDevices();
            uint32_t qOffSet = swPortNum - 1;
            qIdx = qIdx + qOffSet;
            // call tc func do the relay op. send this packet to upstream switch
            // get another netdevice in  this host (for now a host only have 2 ports)
            Ptr<Node> host = dev->GetNode();
            uint32_t anotherDevIdx = 0;
            for (uint32_t i = 1; i < host->GetNDevices(); i++) // 0 is the lookback dev
            {
                if (host->GetDevice(i) != dev)
                {
                    anotherDevIdx = i;
                    break;
                }
            }
            tc->RelayPFC(anotherDevIdx, cate, ppfcFrame.GetQIdx(), Address(), type);
        }
        else if (cate == DcTopology::SwicthFCCateory::NeedRelay)
        {
            // don't need to recaculate the qIdx
        }
        else
        {
            NS_ASSERT_MSG(false, "wrong category in pause/resume packet!");
        }
    }
    else // in switch
    {
        // don't need to recaculate the qIdx
    }

    qDisc->SetPaused(qIdx, isPause);
    m_tracePfcReceived(GetNodeAndPortId(), qIdx, isPause);
    NS_LOG_DEBUG("PFC: node " << Simulator::GetContext() << " port " << index << " qIdx " << qIdx
                              << " type " << type);
}

void
DcbPPfcPort::AddCategory(uint32_t xoff, uint32_t xon)
{
    NS_LOG_FUNCTION(this << xoff << xon);
    m_egressPort.AddEgressCategory(xoff, xon);
}

bool
DcbPPfcPort::CheckShouldSendPause(uint32_t cate) const
{
    const EgressPortInfo::EgressCategoryInfo& q = m_egressPort.getCategory(cate);
    Ptr<DcbPPfcTrafficControl> ppfcTc = DynamicCast<DcbPPfcTrafficControl>(m_tc);
    // bool upstreamPaused = q.isUpstreamPaused;
    // int comp = ppfcTc->CompareEgressQueueLength(m_egressPort.m_index, cate, q.xoff);
    // bool ans = (upstreamPaused == false && comp >= 0);
    // if(ans == true)
    // {
    //     SetUpstreamPaused(cate, true);
    // }
    // return ans;
    return !q.isUpstreamPaused &&
           ppfcTc->CompareEgressQueueLength(m_egressPort.m_index, cate, q.xoff) >= 0;
}

bool
DcbPPfcPort::CheckShouldSendResume(uint32_t cate) const
{
    const EgressPortInfo::EgressCategoryInfo& q = m_egressPort.getCategory(cate);
    Ptr<DcbPPfcTrafficControl> ppfcTc = DynamicCast<DcbPPfcTrafficControl>(m_tc);
    // bool upstreamPaused = q.isUpstreamPaused;
    // int comp = ppfcTc->CompareEgressQueueLength(m_egressPort.m_index, cate, q.xoff);
    // bool ans = (upstreamPaused == true && comp <= 0);
    // if(ans == true)
    // {
    //     SetUpstreamPaused(cate, false);
    // }
    // return ans;
    return q.isUpstreamPaused &&
           ppfcTc->CompareEgressQueueLength(m_egressPort.m_index, cate, q.xon) <= 0;
}

void
DcbPPfcPort::SetUpstreamPaused(uint32_t cate, bool paused)
{
    NS_ASSERT(m_egressPort.getCategory(cate).isUpstreamPaused != paused);
    m_egressPort.getCategory(cate).isUpstreamPaused = paused;
}

std::pair<uint32_t, uint32_t>
DcbPPfcPort::GetNodeAndPortId() const
{
    return std::make_pair(m_dev->GetNode()->GetId(), m_dev->GetIfIndex());
}

/**
 * class DcbPPfcPort::EgressPortInfo::EgressCategoryInfo implementation starts.
 */
DcbPPfcPort::EgressPortInfo::EgressCategoryInfo::EgressCategoryInfo()
    : xoff(0),
      xon(0),
      isUpstreamPaused(false),
      pauseEvent()
{
}

/**
 * class DcbPPfcPort::EgressPortInfo implementation starts.
 */

DcbPPfcPort::EgressPortInfo::EgressPortInfo(uint32_t index)
    : m_index(index)
{
}

inline const DcbPPfcPort::EgressPortInfo::EgressCategoryInfo&
DcbPPfcPort::EgressPortInfo::getCategory(uint32_t cate) const
{
    return m_egressCategories[cate];
}

inline DcbPPfcPort::EgressPortInfo::EgressCategoryInfo&
DcbPPfcPort::EgressPortInfo::getCategory(uint32_t cate)
{
    return m_egressCategories[cate];
}

void DcbPPfcPort::EgressPortInfo::AddEgressCategory(uint32_t off, uint32_t on)
{
    m_egressCategories.emplace_back(off, on);
}

} // namespace ns3
