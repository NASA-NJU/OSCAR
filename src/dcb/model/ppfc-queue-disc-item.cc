/*
 * Copyright (c) 2016 Universita' degli Studi di Napoli Federico II
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

#include "ppfc-queue-disc-item.h"

#include "ns3/log.h"

namespace ns3
{

NS_LOG_COMPONENT_DEFINE("PPfcQueueDiscItem");

PPfcQueueDiscItem::PPfcQueueDiscItem(Ptr<Packet> p,
                                     const Address& addr,
                                     uint16_t protocol,
                                     const Ipv4Header& header,
                                     uint32_t qIdx)
    : Ipv4QueueDiscItem(p, addr, protocol, header),
      m_qIdx(qIdx)
{
}

PPfcQueueDiscItem::~PPfcQueueDiscItem()
{
    NS_LOG_FUNCTION(this);
}

uint32_t
PPfcQueueDiscItem::GetQidx() const
{
    NS_LOG_FUNCTION(this);
    return m_qIdx;
}

void
PPfcQueueDiscItem::SetQidx(uint32_t qIdx)
{
    NS_LOG_FUNCTION(this << qIdx);
    m_qIdx = qIdx;
    return;
}

} // namespace ns3
