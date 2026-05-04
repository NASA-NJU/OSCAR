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
 * Author: F.Y. Xue <xue.fyang@foxmail.com>
 */

#include "dcb-hpcc-port.h"

#include "rocev2-congestion-ops.h"
#include "rocev2-dcqcn-int.h"
#include "rocev2-hpcc.h"
#include "rocev2-l4-protocol.h"
#include "rocev2-poseidon.h"
#include "rocev2-powertcp.h"

#include "ns3/ethernet-header.h"
#include "ns3/hpcc-header.h"
#include "ns3/poseidon-header.h"
#include "ns3/rocev2-header.h"
#include "ns3/simulator.h"

namespace ns3
{

NS_LOG_COMPONENT_DEFINE("DcbHpccPort");

NS_OBJECT_ENSURE_REGISTERED(DcbHpccPort);

TypeId
DcbHpccPort::GetTypeId()
{
    static TypeId tid = TypeId("ns3::DcbHpccPort")
                            .SetParent<DcbPfcPort>()
                            .SetGroupName("Dcb")
                            .AddConstructor<DcbHpccPort>();
    return tid;
}

DcbHpccPort::DcbHpccPort()
    : DcbPfcPort(),
      m_txBytes(0)
{
    NS_LOG_FUNCTION(this);

    EthernetHeader ethHeader;
    Ipv4Header ipv4Header;
    m_extraEgressHeaderSize = ethHeader.GetSerializedSize() + ipv4Header.GetSerializedSize();
}

DcbHpccPort::DcbHpccPort(Ptr<NetDevice> dev, Ptr<DcbTrafficControl> tc)
    : DcbPfcPort(dev, tc),
      m_txBytes(0)
{
    NS_LOG_FUNCTION(this);

    EthernetHeader ethHeader;
    Ipv4Header ipv4Header;
    m_extraEgressHeaderSize = ethHeader.GetSerializedSize() + ipv4Header.GetSerializedSize();
}

DcbHpccPort::~DcbHpccPort()
{
    NS_LOG_FUNCTION(this);
}

void
DcbHpccPort::DoEgressProcess(Ptr<Packet> packet)
{
    NS_LOG_FUNCTION(this << packet);

    // DcbPfcPort::DoEgressProcess(packet);

    // Add m_txBytes
    m_txBytes += packet->GetSize() + m_extraEgressHeaderSize;
    /**
     * considering that multiple CC algorithms are used at the same time, we should check current
     * packet's CC algorithm
     */
    CongestionTypeTag ctTag;
    IntStyle style = HPCC;
    if (packet->PeekPacketTag(ctTag))
    {
        if (ctTag.GetCongestionTypeId() == RoCEv2Hpcc::GetTypeId() ||
            ctTag.GetCongestionTypeId() == RoCEv2Powertcp::GetTypeId()||
            ctTag.GetCongestionTypeId() == RoCEv2DcqcnInt::GetTypeId())
        {
            style = HPCC;
        }
        else if (ctTag.GetCongestionTypeId() == RoCEv2Poseidon::GetTypeId())
        {
            style = POSEIDON;
        }
        else
        {
            return;
        }
    }

    // Check and Remove packet's RoCEv2 Header
    UdpHeader udpHeader;
    packet->PeekHeader(udpHeader);

    // Check if is RoCEv2
    if (udpHeader.GetSourcePort() == RoCEv2L4Protocol::PROT_NUMBER)
    {
        UdpRoCEv2Header udpRoCEheader;
        packet->RemoveHeader(udpRoCEheader);

        // Check if RoCEv2 Header is RC_SEND_ONLY
        // XXX maybe we should find a better way to check if it's RC_SEND_ONLY
        if (udpRoCEheader.GetRoCE().GetOpcode() == RoCEv2Header::Opcode::RC_SEND_ONLY)
        {
            // XXX since HPCC doesn't consider multi-priority queue, we simply calculate queue
            // length and txBytes by adding all queues together.
            Ptr<DcbNetDevice> device = DynamicCast<DcbNetDevice>(m_dev);
            uint64_t qLen = 0;
            size_t nQueue = device->GetQueueDisc()->GetNQueueDiscClasses();

            // Sum queue length of each priority queue
            for (size_t i = 0; i < nQueue; i++)
            {
                qLen += device->GetQueueDisc()->GetQueueDiscClass(i)->GetQueueDisc()->GetNBytes();
            }
            if (style == HPCC)
            {
                // Remove and Modify packet's HPCC header
                HpccHeader hpccHeader;
                packet->RemoveHeader(hpccHeader);

                // Push into HpccHeader
                hpccHeader.PushHop(Simulator::Now().GetNanoSeconds(),
                                   m_txBytes,
                                   qLen,
                                   device->GetDataRate());
                packet->AddHeader(hpccHeader);
            }
            else if (style == POSEIDON)
            {
                // Remove and Modify packet's Poseidon header
                PoseidonHeader poseidonHeader;
                packet->RemoveHeader(poseidonHeader);

                Time queueDelay = device->GetDataRate().CalculateBytesTxTime(qLen);
                poseidonHeader.UpdateMpd(queueDelay.GetMicroSeconds());
                packet->AddHeader(poseidonHeader);
            }
        }

        // Rebuild packet's UdpRocev2Header
        packet->AddHeader(udpRoCEheader);
    }
    // // Rebuild packet's Ipv4Header
    // packet->AddHeader(ipv4Header);
}

} // namespace ns3
