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

#include "dcb-ppfc-mmu-queue.h"

namespace ns3
{

NS_LOG_COMPONENT_DEFINE("DcbPPfcMmuQueue");

NS_OBJECT_ENSURE_REGISTERED(DcbPPfcMmuQueue);

TypeId
DcbPPfcMmuQueue::GetTypeId()
{
    static TypeId tid = TypeId("ns3::DcbPPfcMmuQueue")
                            .SetParent<DcbFlowControlMmuQueue>()
                            .AddConstructor<DcbPPfcMmuQueue>();

    return tid;
}

TypeId
DcbPPfcMmuQueue::GetInstanceTypeId() const
{
    return GetTypeId();
}

DcbPPfcMmuQueue::DcbPPfcMmuQueue()
    : DcbFlowControlMmuQueue(),
      m_queueLength(0),
      m_reservedSize(0)
{
}

DcbPPfcMmuQueue::DcbPPfcMmuQueue(uint32_t reserveSize)
    : DcbFlowControlMmuQueue(),
      m_queueLength(0),
      m_reservedSize(reserveSize)
{
}

DcbPPfcMmuQueue::~DcbPPfcMmuQueue()
{
}

uint32_t DcbPPfcMmuQueue::GetQueueLength()
{
    return m_queueLength;
}

bool
DcbPPfcMmuQueue::CheckIngressAdmission(uint32_t packetSize)
{
    return true;
}

bool
DcbPPfcMmuQueue::CheckEgressAdmission(uint32_t packetSize)
{
    if(packetSize + m_totalSharedBufferUsed() <= m_totalSharedBufferSize())
    {
        return true;
    }
    return false;
}

void
DcbPPfcMmuQueue::IngressIncrement(uint32_t packetSize)
{
}

void
DcbPPfcMmuQueue::IngressDecrement(uint32_t packetSize)
{
}

void DcbPPfcMmuQueue::EgressIncrement(uint32_t packetSize)
{
    m_queueLength += packetSize;
}

void DcbPPfcMmuQueue::EgressDecrement(uint32_t packetSize)
{
    m_queueLength -= packetSize;
}


} // namespace ns3
